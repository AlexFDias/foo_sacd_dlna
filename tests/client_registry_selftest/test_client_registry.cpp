// Unit test for client_registry.h (standard C++ only).
#include "../../client_registry.h"
#include <cstdio>
#include <thread>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL  line %d: %s\n", __LINE__, #cond); } } while (0)
using namespace clientreg;
using std::chrono::seconds;
using std::chrono::minutes;

int main() {
    const Clock::time_point t0 = Clock::now();

    // ---- empty
    { Registry r; const Counts c = r.counts(t0); CHECK(c.total == 0 && c.active == 0 && c.idle == 0 && c.seenSinceStart == 0); }

    // ---- a client that only browses is idle; total = active + idle
    { Registry r;
      r.touch("10.0.0.5", "T+A SDX", t0);
      Counts c = r.counts(t0); CHECK(c.total == 1 && c.active == 0 && c.idle == 1 && c.seenSinceStart == 1);
      r.touch("10.0.0.5", "", t0 + seconds(1)); r.touch("10.0.0.5", "", t0 + seconds(2));     // same client: still one
      c = r.counts(t0 + seconds(2)); CHECK(c.total == 1 && c.seenSinceStart == 1);
      r.touch("10.0.0.9", "BubbleUPnP", t0 + seconds(3));
      c = r.counts(t0 + seconds(3)); CHECK(c.total == 2 && c.idle == 2 && c.active == 0 && c.seenSinceStart == 2);
      CHECK(c.total == c.active + c.idle); }

    // ---- streaming makes a client active; two streams from one client are still one active client
    { Registry r;
      r.touch("10.0.0.5", "", t0); r.touch("10.0.0.9", "", t0);
      r.streamStarted("10.0.0.5", t0);
      Counts c = r.counts(t0); CHECK(c.total == 2 && c.active == 1 && c.idle == 1);
      r.streamStarted("10.0.0.5", t0);                                                        // second stream, same client
      c = r.counts(t0); CHECK(c.active == 1 && c.idle == 1);
      r.streamEnded("10.0.0.5", t0 + seconds(5));
      c = r.counts(t0 + seconds(5)); CHECK(c.active == 1);                                    // one stream left
      r.streamEnded("10.0.0.5", t0 + seconds(6));
      c = r.counts(t0 + seconds(6)); CHECK(c.active == 0 && c.idle == 2 && c.total == 2);
      r.streamEnded("10.0.0.5", t0 + seconds(7));                                             // extra end never goes negative
      c = r.counts(t0 + seconds(7)); CHECK(c.active == 0 && c.total == 2);
      r.streamEnded("203.0.113.1", t0);                                                       // unknown client: ignored
      CHECK(r.counts(t0 + seconds(7)).total == 2); }

    // ---- a stream event alone registers the client
    { Registry r; r.streamStarted("10.0.0.7", t0);
      const Counts c = r.counts(t0); CHECK(c.total == 1 && c.active == 1 && c.seenSinceStart == 1); }

    // ---- idle clients are forgotten after the expiry; active ones never are
    { Registry r(minutes(10));
      r.touch("10.0.0.5", "", t0); r.touch("10.0.0.9", "", t0); r.streamStarted("10.0.0.9", t0);
      Counts c = r.counts(t0 + minutes(9)); CHECK(c.total == 2);
      c = r.counts(t0 + minutes(11)); CHECK(c.total == 1 && c.active == 1 && c.idle == 0);   // idle one dropped, streaming one kept
      c = r.counts(t0 + minutes(600)); CHECK(c.total == 1 && c.active == 1);                 // a 10 hour stream is still there
      CHECK(c.seenSinceStart == 2);                                                           // history survives the pruning
      r.streamEnded("10.0.0.9", t0 + minutes(600));
      c = r.counts(t0 + minutes(600)); CHECK(c.total == 1 && c.idle == 1);                   // now idle, silent since the end of the stream
      c = r.counts(t0 + minutes(611)); CHECK(c.total == 0); }
    { Registry r(minutes(10)); r.touch("a", "", t0);
      r.touch("a", "", t0 + minutes(9));                                                      // activity refreshes the timer
      CHECK(r.counts(t0 + minutes(15)).total == 1);
      CHECK(r.counts(t0 + minutes(20)).total == 0); }

    // ---- snapshot: active first, then most recently seen; user agent kept from the first non-empty value
    { Registry r;
      r.touch("10.0.0.1", "", t0); r.touch("10.0.0.2", "Old", t0 + seconds(10)); r.touch("10.0.0.3", "", t0 + seconds(20));
      r.touch("10.0.0.2", "Newer", t0 + seconds(30)); r.streamStarted("10.0.0.1", t0 + seconds(40));
      const auto v = r.snapshot(t0 + seconds(60));
      CHECK(v.size() == 3);
      CHECK(v[0].ip == "10.0.0.1" && v[0].active && v[0].activeStreams == 1 && v[0].streamsServed == 1);
      CHECK(v[1].ip == "10.0.0.2" && !v[1].active && v[1].userAgent == "Old" && v[1].requests == 2);
      CHECK(v[2].ip == "10.0.0.3");
      CHECK(v[0].silentFor == seconds(20)); }

    // ---- empty ip ignored, reset clears everything
    { Registry r; r.touch("", "x", t0); r.streamStarted("", t0); CHECK(r.counts(t0).total == 0);
      r.touch("1.1.1.1", "", t0); r.reset(); const Counts c = r.counts(t0); CHECK(c.total == 0 && c.seenSinceStart == 0); }

    // ---- limiter: single thread
    { StreamLimiter l;
      CHECK(l.tryAcquire(2) && l.tryAcquire(2) && !l.tryAcquire(2));
      CHECK(l.current() == 2 && l.rejected() == 1);
      l.release(); CHECK(l.current() == 1 && l.tryAcquire(2));
      CHECK(!l.tryAcquire(2) && l.rejected() == 2);
      // lowering the limit never cuts streams that are running, but blocks new ones
      CHECK(!l.tryAcquire(1) && l.current() == 2);
      l.release(); CHECK(!l.tryAcquire(1));            // 1 running, limit 1: still full
      l.release(); CHECK(l.current() == 0 && l.tryAcquire(1));
      // raising the limit is effective immediately
      CHECK(!l.tryAcquire(1) && l.tryAcquire(3) && l.current() == 2);
      // release never underflows
      l.release(); l.release(); l.release(); l.release(); CHECK(l.current() == 0);
      // limit 0 is treated as 1
      CHECK(l.tryAcquire(0) && !l.tryAcquire(0));
      l.resetCounters(); CHECK(l.rejected() == 0); }

    // ---- limiter: 16 threads hammering a limit of 3; the number of streams running at once must never exceed 3
    for (uint32_t limit : {1u, 2u, 3u, 8u}) {
        StreamLimiter l; std::atomic<int> running{0}; std::atomic<int> maxSeen{0}; std::atomic<long> granted{0};
        std::vector<std::thread> ts;
        for (int t = 0; t < 16; ++t) ts.emplace_back([&] {
            for (int i = 0; i < 20000; ++i) {
                if (!l.tryAcquire(limit)) continue;
                const int now = ++running; granted.fetch_add(1);
                int prev = maxSeen.load(); while (now > prev && !maxSeen.compare_exchange_weak(prev, now)) {}
                --running; l.release();
            }
        });
        for (auto& th : ts) th.join();
        CHECK(maxSeen.load() <= static_cast<int>(limit));
        CHECK(l.current() == 0);
        CHECK(granted.load() > 0);
    }

    // ---- clampStreams
    CHECK(clampStreams(0) == 1 && clampStreams(1) == 1 && clampStreams(2) == 2 && clampStreams(16) == 16 && clampStreams(17) == 16 && clampStreams(4000000000u) == 16);
    CHECK(kDefaultStreams == 2);

    // ---- Registry is thread safe
    { Registry r; std::vector<std::thread> ts;
      for (int t = 0; t < 8; ++t) ts.emplace_back([&, t] {
          for (int i = 0; i < 5000; ++i) {
              const std::string ip = "10.0.0." + std::to_string((t * 7 + i) % 20);
              r.touch(ip, "ua", t0); r.streamStarted(ip, t0); r.counts(t0); r.streamEnded(ip, t0);
          }
      });
      for (auto& th : ts) th.join();
      const Counts c = r.counts(t0); CHECK(c.active == 0 && c.total == 20 && c.seenSinceStart == 20); }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
