#pragma once
// Client bookkeeping for the DLNA server: how many clients (renderers / control points) are known,
// how many are streaming right now and how many are idle, plus the limiter that caps concurrent
// audio streams. Standard C++ only (no foobar2000 SDK, no Windows) so it can be unit-tested anywhere:
// see tests/client_registry_selftest.
//
// Definitions (also shown in the Status UI):
//   client  = a remote peer IP that sent at least one HTTP request (Browse, artwork, media...).
//             Loopback / this machine's own address (the built-in self-test) is never counted.
//   active  = the client is being sent audio right now (at least one stream in progress). A request that
//             is only waiting for a SACD/DSP conversion is not a stream yet.
//   idle    = a known client with no stream in progress. It stays known until it has been silent
//             for `idleExpiry` (10 minutes by default), then it is forgotten.
//   total   = active + idle, i.e. every client currently known.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace clientreg {

using Clock = std::chrono::steady_clock;

struct Counts {
    uint32_t total = 0;
    uint32_t active = 0;
    uint32_t idle = 0;
    uint32_t seenSinceStart = 0;   // distinct clients seen since the last reset(), including forgotten ones
};

struct ClientView {
    std::string ip;
    std::string userAgent;
    bool active = false;
    uint32_t activeStreams = 0;
    uint64_t requests = 0;
    uint64_t streamsServed = 0;
    std::chrono::seconds silentFor{0};   // time since the last request / stream event
};

class Registry {
public:
    explicit Registry(std::chrono::seconds idleExpiry = std::chrono::minutes(10)) : m_expiry(idleExpiry) {}

    // Any HTTP request from `ip`.
    void touch(const std::string& ip, const std::string& userAgent, Clock::time_point now) {
        if (ip.empty()) return;
        std::lock_guard<std::mutex> g(m_mutex);
        Entry& e = entry(ip, now);
        ++e.requests;
        e.lastSeen = now;
        if (e.userAgent.empty() && !userAgent.empty()) e.userAgent = userAgent;
    }

    void streamStarted(const std::string& ip, Clock::time_point now) {
        if (ip.empty()) return;
        std::lock_guard<std::mutex> g(m_mutex);
        Entry& e = entry(ip, now);
        ++e.activeStreams;
        ++e.streamsServed;
        e.lastSeen = now;
    }

    void streamEnded(const std::string& ip, Clock::time_point now) {
        if (ip.empty()) return;
        std::lock_guard<std::mutex> g(m_mutex);
        const auto it = m_clients.find(ip);
        if (it == m_clients.end()) return;
        if (it->second.activeStreams) --it->second.activeStreams;
        it->second.lastSeen = now;
    }

    Counts counts(Clock::time_point now) const {
        std::lock_guard<std::mutex> g(m_mutex);
        prune(now);
        Counts c;
        c.total = static_cast<uint32_t>(m_clients.size());
        for (const auto& kv : m_clients) if (kv.second.activeStreams) ++c.active;
        c.idle = c.total - c.active;
        c.seenSinceStart = m_seenSinceStart;
        return c;
    }

    // Active clients first, then idle ones, most recently seen first within each group.
    std::vector<ClientView> snapshot(Clock::time_point now) {
        std::lock_guard<std::mutex> g(m_mutex);
        prune(now);
        std::vector<ClientView> out;
        out.reserve(m_clients.size());
        for (const auto& kv : m_clients) {
            ClientView v;
            v.ip = kv.first; v.userAgent = kv.second.userAgent;
            v.activeStreams = kv.second.activeStreams; v.active = kv.second.activeStreams != 0;
            v.requests = kv.second.requests; v.streamsServed = kv.second.streamsServed;
            v.silentFor = std::chrono::duration_cast<std::chrono::seconds>(now - kv.second.lastSeen);
            out.push_back(std::move(v));
        }
        std::sort(out.begin(), out.end(), [](const ClientView& a, const ClientView& b) {
            if (a.active != b.active) return a.active;
            if (a.silentFor != b.silentFor) return a.silentFor < b.silentFor;
            return a.ip < b.ip;
        });
        return out;
    }

    void reset() {
        std::lock_guard<std::mutex> g(m_mutex);
        m_clients.clear();
        m_seenSinceStart = 0;
    }

private:
    struct Entry {
        std::string userAgent;
        uint32_t activeStreams = 0;
        uint64_t requests = 0;
        uint64_t streamsServed = 0;
        Clock::time_point lastSeen{};
    };

    Entry& entry(const std::string& ip, Clock::time_point now) {
        auto it = m_clients.find(ip);
        if (it == m_clients.end()) {
            it = m_clients.emplace(ip, Entry{}).first;
            it->second.lastSeen = now;
            ++m_seenSinceStart;
        }
        return it->second;
    }

    // A client that is streaming is never dropped, however long the stream lasts.
    void prune(Clock::time_point now) const {
        for (auto it = m_clients.begin(); it != m_clients.end();) {
            if (it->second.activeStreams == 0 && now - it->second.lastSeen > m_expiry) it = m_clients.erase(it);
            else ++it;
        }
    }

    mutable std::mutex m_mutex;
    mutable std::unordered_map<std::string, Entry> m_clients;
    uint32_t m_seenSinceStart = 0;
    std::chrono::seconds m_expiry;
};

// Caps the number of audio streams served at the same time. tryAcquire() checks and takes a slot in one
// atomic step (compare-and-swap), so two requests that arrive together can never both slip past the limit.
// The limit is passed on every call, so changing it in Preferences takes effect immediately; streams that
// are already running are never cut, new ones simply wait for the count to drop below the new limit.
class StreamLimiter {
public:
    bool tryAcquire(uint32_t limit) {
        if (limit == 0) limit = 1;
        uint32_t cur = m_current.load(std::memory_order_relaxed);
        while (cur < limit) {
            if (m_current.compare_exchange_weak(cur, cur + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) return true;
        }
        m_rejected.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    void release() {
        uint32_t cur = m_current.load(std::memory_order_relaxed);
        while (cur > 0 && !m_current.compare_exchange_weak(cur, cur - 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {}
    }

    uint32_t current() const { return m_current.load(std::memory_order_relaxed); }
    uint32_t rejected() const { return m_rejected.load(std::memory_order_relaxed); }
    void resetCounters() { m_rejected.store(0, std::memory_order_relaxed); }

private:
    std::atomic<uint32_t> m_current{0};
    std::atomic<uint32_t> m_rejected{0};
};

// Preferences store the limit as a plain number; keep it in a range that makes sense.
constexpr uint32_t kMinStreams = 1;
constexpr uint32_t kMaxStreams = 16;
constexpr uint32_t kDefaultStreams = 2;
inline uint32_t clampStreams(uint32_t v) { return v < kMinStreams ? kMinStreams : (v > kMaxStreams ? kMaxStreams : v); }

}  // namespace clientreg
