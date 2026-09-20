#include "stdafx.h"
#include "dlna_server.h"
#include "sacd_decode.h"
#include "config.h"
#include "dsp_bridge.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <shellapi.h>
#include <sstream>

namespace fs = std::filesystem;

namespace {

constexpr const char* kUuid = "uuid:7e2f2d7e-7d89-4d3c-9f3e-3a7c09fd1234";
constexpr const char* kRootObject = "0";
constexpr const char* kArtistsObject = "artists";
constexpr const char* kArtistPrefix = "artist-";
constexpr const char* kAlbumPrefix = "album-";
constexpr const char* kTrackPrefix = "track-";
constexpr size_t kMaxHttpHeader = 128 * 1024;
constexpr size_t kMaxSoapBody = 2 * 1024 * 1024;
constexpr uint32_t kCacheFormatVersion = 2;
constexpr uint32_t kMaxConcurrentStreams = 2;

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trimCopy(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

std::mutex g_logMutex;

std::wstring networkLogPath() {
    std::wstring p = pfc::stringcvt::string_wide_from_utf8(core_api::get_profile_path());
    if (!p.empty() && p.back() != L'\\' && p.back() != L'/') p += L'\\';
    p += L"foo_sacd_dlna\\network.log";
    CreateDirectoryW((p.substr(0, p.rfind(L"\\"))).c_str(), nullptr);
    return p;
}

std::string utcStamp() {
    SYSTEMTIME st{}; GetSystemTime(&st);
    char b[64]{};
    snprintf(b, sizeof(b), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return b;
}

void networkLog(const std::string& message) {
    if (!static_cast<bool>(sacd_dlna_cfg::network_logging) && !static_cast<bool>(sacd_dlna_cfg::debug_diagnostics)) return;
    std::lock_guard<std::mutex> g(g_logMutex);
    FB2K_console_formatter() << "SACD DLNA [network]: " << message.c_str();
    try {
        const auto path = networkLogPath();
        std::error_code ec;
        if (fs::exists(path, ec) && fs::file_size(path, ec) > 10ull * 1024ull * 1024ull) {
            const auto old = path + L".1";
            DeleteFileW(old.c_str());
            MoveFileW(path.c_str(), old.c_str());
        }
        std::ofstream f(path, std::ios::app | std::ios::binary);
        if (f) f << utcStamp() << " | " << message << "\n";
    } catch (...) {}
}

void setLastError(const std::string& message) {
    if (!static_cast<bool>(sacd_dlna_cfg::network_logging) && !static_cast<bool>(sacd_dlna_cfg::debug_diagnostics)) return;
    std::lock_guard<std::mutex> g(g_logMutex);
    FB2K_console_formatter() << "SACD DLNA [error]: " << message.c_str();
    try {
        const auto path = networkLogPath();
        std::error_code ec;
        if (fs::exists(path, ec) && fs::file_size(path, ec) > 10ull * 1024ull * 1024ull) {
            const auto old = path + L".1";
            DeleteFileW(old.c_str());
            MoveFileW(path.c_str(), old.c_str());
        }
        std::ofstream f(path, std::ios::app | std::ios::binary);
        if (f) f << utcStamp() << " | ERROR | " << message << "\n";
    } catch (...) {}
}

void sendAll(SOCKET s, const char* p, size_t n, abort_callback* aborter = nullptr) {
    while (n) {
        if (aborter) aborter->check();
        const size_t chunkSize = (n < (1u << 20)) ? n : (1u << 20);
        const int chunk = static_cast<int>(chunkSize);
        const int r = send(s, p, chunk, 0);
        if (r <= 0) throw std::runtime_error("socket send failed");
        p += r;
        n -= static_cast<size_t>(r);
    }
}

std::wstring persistentCacheFolder() {
    std::wstring p = pfc::stringcvt::string_wide_from_utf8(core_api::get_profile_path());
    if (!p.empty() && p.back() != L'\\' && p.back() != L'/') p += L'\\';
    p += L"foo_sacd_dlna\\cache";
    CreateDirectoryW((p.substr(0, p.rfind(L"\\"))).c_str(), nullptr);
    CreateDirectoryW(p.c_str(), nullptr);
    return p;
}

uint64_t fnv1a64(const std::string& s) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

std::wstring utf8ToWide(const char* text) {
    if (!text) return {};
    const auto converted = pfc::stringcvt::string_wide_from_utf8(text);
    return std::wstring(converted.get_ptr());
}

std::string cacheKeyFor(const std::string& sourcePath, t_uint32 subsong) {
    std::error_code ec;
    const std::wstring ws = utf8ToWide(sourcePath.c_str());
    const fs::path filePath(ws);
    const uintmax_t size = fs::file_size(filePath, ec);
    std::error_code ec2;
    const auto wt = fs::last_write_time(filePath, ec2);
    const auto ticks = ec2 ? 0LL : static_cast<long long>(wt.time_since_epoch().count());
    pfc::string8 sacdVersion;
    sacd_plugin_installed(&sacdVersion);
    const std::string material = std::to_string(kCacheFormatVersion) + "#" +
        sourcePath + "#" + std::to_string(subsong) + "#" +
        std::to_string(static_cast<unsigned long long>(size)) + "#" +
        std::to_string(ticks) + "#" + sacdVersion.c_str() + "#DSP#" +
        (static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled) ? DsdProcessorBridge::preset_fingerprint() : "native");
    char hex[32]{};
    snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(fnv1a64(material)));
    return hex;
}


std::wstring cacheMetaPath(const std::wstring& dsfPath) { return dsfPath + L".json"; }

std::string cacheFileExtForMime(const std::string& mime) {
    const auto l = lowerCopy(mime);
    if (l == "image/png") return ".png";
    if (l == "image/webp") return ".webp";
    if (l == "image/gif") return ".gif";
    if (l == "image/bmp") return ".bmp";
    if (l == "image/tiff") return ".tiff";
    return ".jpg";
}

bool isRegularFile(const std::wstring& path) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(path), ec) && !ec;
}

uint64_t fileSizeSafe(const std::wstring& path) {
    std::error_code ec;
    const auto n = fs::file_size(fs::path(path), ec);
    return ec ? 0 : static_cast<uint64_t>(n);
}

bool getHeaderValue(const std::string& headers, const char* name, std::string& out) {
    const std::string lower = lowerCopy(headers);
    const std::string target = lowerCopy(name);
    size_t p = lower.find(target);
    while (p != std::string::npos && p != 0 && lower[p - 1] != '\n') p = lower.find(target, p + 1);
    if (p == std::string::npos) return false;
    const auto e = headers.find("\r\n", p);
    const auto colon = headers.find(':', p);
    if (colon == std::string::npos || (e != std::string::npos && colon > e)) return false;
    out = headers.substr(colon + 1, e == std::string::npos ? std::string::npos : e - colon - 1);
    out = trimCopy(out);
    return true;
}

bool recvHttpRequest(SOCKET s, std::string& request) {
    request.clear();
    char buf[8192];
    size_t headerEnd = std::string::npos;
    while (request.size() < kMaxHttpHeader && headerEnd == std::string::npos) {
        const int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        request.append(buf, buf + n);
        headerEnd = request.find("\r\n\r\n");
    }
    if (headerEnd == std::string::npos || headerEnd > kMaxHttpHeader) return false;

    std::string cl;
    uint64_t contentLength = 0;
    if (getHeaderValue(request.substr(0, headerEnd + 4), "Content-Length", cl)) {
        try { contentLength = std::stoull(trimCopy(cl)); }
        catch (...) { return false; }
    }
    if (contentLength > kMaxSoapBody) return false;

    const size_t bodyStart = headerEnd + 4;
    while (request.size() - bodyStart < contentLength) {
        const int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        request.append(buf, buf + n);
    }
    if (request.size() > bodyStart + contentLength) request.resize(bodyStart + static_cast<size_t>(contentLength));
    return true;
}

std::string xmlAttr(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (char c : value) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string xmlTextEscape(const std::string& value) {
    return xmlAttr(value);
}

std::string decodeXmlEntities(std::string s) {
    const std::pair<const char*, const char*> entities[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}
    };
    for (const auto& e : entities) {
        size_t pos = 0;
        while ((pos = s.find(e.first, pos)) != std::string::npos) { s.replace(pos, strlen(e.first), e.second); pos += strlen(e.second); }
    }
    return s;
}

bool parseRange(const std::string& headers, uint64_t size, uint64_t& begin, uint64_t& end, bool& partial) {
    std::string value;
    if (!getHeaderValue(headers, "Range", value)) return true;
    value = trimCopy(value);
    if (value.rfind("bytes=", 0) != 0 || size == 0) return true;
    const auto dash = value.find('-', 6);
    if (dash == std::string::npos) return true;
    try {
        if (dash == 6) {
            const uint64_t suffix = std::stoull(value.substr(7));
            if (suffix == 0) return false;
            begin = suffix >= size ? 0 : size - suffix;
            end = size - 1;
        } else {
            begin = std::stoull(value.substr(6, dash - 6));
            if (dash + 1 < value.size()) end = std::stoull(value.substr(dash + 1));
            else end = size - 1;
        }
    } catch (...) { return false; }
    if (begin >= size || begin > end) return false;
    if (end >= size) end = size - 1;
    partial = true;
    return true;
}

struct UrlParts {
    std::string scheme;
    std::string host;
    uint16_t port = 80;
    std::string path = "/";
};

bool parseUrl(const std::string& url, UrlParts& out) {
    const auto schemePos = url.find("://");
    if (schemePos == std::string::npos) return false;
    out.scheme = lowerCopy(url.substr(0, schemePos));
    const size_t hostStart = schemePos + 3;
    const size_t slash = url.find('/', hostStart);
    const std::string authority = url.substr(hostStart, slash == std::string::npos ? std::string::npos : slash - hostStart);
    out.path = slash == std::string::npos ? "/" : url.substr(slash);
    const auto colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(']') == std::string::npos) {
        out.host = authority.substr(0, colon);
        try { out.port = static_cast<uint16_t>(std::stoul(authority.substr(colon + 1))); } catch (...) { return false; }
    } else {
        out.host = authority;
        out.port = out.scheme == "https" ? 443 : 80;
    }
    return !out.host.empty();
}

bool httpRequestSimple(const std::string& method, const std::string& url, const std::string& body,
                       std::string& responseHeaders, std::string& responseBody, const char* soapAction = nullptr) {
    UrlParts u;
    if (!parseUrl(url, u) || u.scheme != "http") return false;
    SOCKET c = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c == INVALID_SOCKET) return false;
    DWORD timeout = 1800;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(u.port);
    if (inet_pton(AF_INET, u.host.c_str(), &addr.sin_addr) != 1) {
        addrinfo hints{}; hints.ai_family = AF_INET;
        addrinfo* res = nullptr;
        if (getaddrinfo(u.host.c_str(), nullptr, &hints, &res) != 0 || !res) { closesocket(c); return false; }
        addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }
    if (connect(c, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { closesocket(c); return false; }

    std::string req = method + " " + u.path + " HTTP/1.1\r\nHost: " + u.host + "\r\nConnection: close\r\n";
    if (!body.empty()) {
        req += "Content-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
        if (soapAction) req += "SOAPACTION: \"" + std::string(soapAction) + "\"\r\n";
    }
    req += "\r\n";
    req += body;
    if (send(c, req.data(), static_cast<int>(req.size()), 0) <= 0) { closesocket(c); return false; }

    std::string reply;
    char buf[16384];
    while (reply.size() < 4 * 1024 * 1024) {
        const int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        reply.append(buf, buf + n);
    }
    closesocket(c);

    const auto split = reply.find("\r\n\r\n");
    if (split == std::string::npos) return false;
    responseHeaders = reply.substr(0, split + 4);
    responseBody = reply.substr(split + 4);
    return true;
}

std::string soapEnvelope(const std::string& serviceType, const std::string& action, const std::string& args) {
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
           "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
           "<s:Body><u:" + action + " xmlns:u=\"" + serviceType + "\">" + args + "</u:" + action + ">"
           "</s:Body></s:Envelope>";
}

std::string toUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string formatDuration(double seconds) {
    if (seconds <= 0.0) return {};
    const uint64_t total = static_cast<uint64_t>(seconds + 0.5);
    const uint64_t h = total / 3600;
    const uint64_t m = (total % 3600) / 60;
    const uint64_t s = total % 60;
    char buf[32]{};
    snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu", static_cast<unsigned long long>(h), static_cast<unsigned long long>(m), static_cast<unsigned long long>(s));
    return buf;
}

std::vector<size_t> sortedIndicesByTitle(const std::vector<std::string>& titles, bool descending) {
    std::vector<size_t> idx(titles.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        const int cmp = _stricmp(titles[a].c_str(), titles[b].c_str());
        return descending ? cmp > 0 : cmp < 0;
    });
    return idx;
}

} // anonymous namespace

class library_refresh_callback : public main_thread_callback {
public:
    void callback_run() override { SacdDlnaServer::instance().share_music_library(); }
};

class library_tracker final : public library_callback_v2_dynamic_impl_base {
public:
    void on_items_added(metadb_handle_list_cref) override { request(); }
    void on_items_removed(metadb_handle_list_cref) override { request(); }
    void on_items_modified_v2(metadb_handle_list_cref, metadb_io_callback_v2_data&) override { request(); }
    void on_library_initialized() override { request(); }

private:
    void request() {
        if (!SacdDlnaServer::instance().get_status().sharingLibrary) return;
        SacdDlnaServer::instance().request_library_refresh();
    }
};

SacdDlnaServer& SacdDlnaServer::instance() {
    static SacdDlnaServer x;
    return x;
}

void SacdDlnaServer::set_enabled(bool enabled) {
    if (enabled) start(); else stop();
}

void SacdDlnaServer::start() {
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true)) return;

    if (!sacd_plugin_installed()) {
        m_running = false;
        console::print("SACD DLNA: Super Audio CD Decoder (foo_input_sacd.dll) is not installed");
        return;
    }

    m_port = static_cast<uint16_t>(std::clamp<uint32_t>(sacd_dlna_cfg::port.get(), 1024, 65535));

    {
        std::lock_guard<std::mutex> g(m_diagMutex);
        m_httpReady = false;
        m_ssdpReady = false;
        m_httpSelfTestOk = false;
        m_ssdpProbeOk = false;
        m_ssdpNotifyLoopbackOk = false;
        m_networkPresence = false;
        m_ssdpAliveSent = 0;
        m_ssdpDiscoverSent = 0;
        m_ssdpMSearchReceived = 0;
        m_ssdpResponsesSent = 0;
        m_ssdpDiscoverResponses = 0;
        m_httpRequests = 0;
        m_localIp.clear();
        m_ssdpLastPeer.clear();
        m_networkDiagnostic = "Starting network diagnostics";
        m_networkVisibility = "WAITING FOR REMOTE PEER";
        m_remoteHttpRequests = 0;
        m_remoteSsdpSearches = 0;
        m_remoteHttpSeen = false;
        m_remoteSsdpSeen = false;
        m_lastRemotePeer.clear();
    }

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        m_running = false;
        throw std::runtime_error("WSAStartup failed");
    }

    {
        std::lock_guard<std::mutex> g(m_diagMutex);
        m_localIp = localAddress();
        m_networkDiagnostic = "Server starting; waiting for HTTP/SSDP sockets";
    }

    m_libraryTracker = std::make_shared<library_tracker>();
    m_httpThread = std::thread([this] { httpLoop(); });
    m_ssdpThread = std::thread([this] { ssdpLoop(); });
    networkLog("server started on TCP " + std::to_string(m_port));
    console::print("SACD DLNA: BROADCASTING / ACTIVE");
}

void SacdDlnaServer::stop() {
    if (!m_running.exchange(false)) return;

    {
        std::lock_guard<std::mutex> g(m_diagMutex);
        m_networkDiagnostic = "Server stopping";
    }

    if (m_httpListen != INVALID_SOCKET) {
        closesocket(m_httpListen);
        m_httpListen = INVALID_SOCKET;
    }

    if (m_httpThread.joinable()) m_httpThread.join();

    {
        std::lock_guard<std::mutex> g(m_clientMutex);
        for (auto& client : m_clients) {
            if (client.aborter) client.aborter->set();
            if (client.socket != INVALID_SOCKET) shutdown(client.socket, SD_BOTH);
        }
    }
    {
        std::lock_guard<std::mutex> g(m_cacheMutex);
        for (auto& kv : m_cacheJobs) kv.second->aborter->set();
    }

    clearPrefetchThreads();
    for (auto& t : m_clientThreads) if (t.joinable()) t.join();
    m_clientThreads.clear();
    for (auto& t : m_prefetchThreads) if (t.joinable()) t.join();
    {
        std::lock_guard<std::mutex> g(m_prefetchMutex);
        m_prefetchThreads.clear();
        m_prefetchAborters.clear();
        m_prefetchKeys.clear();
        m_prefetchActive = false;
        m_prefetchState = "IDLE";
        m_prefetchTitle.clear();
    }
    {
        std::lock_guard<std::mutex> g(m_clientMutex);
        m_clients.clear();
    }

    if (m_ssdpThread.joinable()) m_ssdpThread.join();
    m_libraryTracker.reset();
    WSACleanup();
    networkLog("server stopped");
    console::print("SACD DLNA: broadcasting stopped");
}

size_t SacdDlnaServer::shared_count() const { return m_sharedCount.load(); }

SacdDlnaStatus SacdDlnaServer::get_status() const {
    SacdDlnaStatus s;
    s.broadcasting = is_running();
    s.sacdInstalled = sacd_plugin_installed(&s.sacdVersion);
    s.sharingLibrary = m_sharingLibrary.load();
    s.sharedCount = m_sharedCount.load();
    s.port = m_port;
    s.networkLogging = sacd_dlna_cfg::network_logging;
    s.dsdProcessorInstalled = dsd_processor_installed(&s.dsdProcessorVersion);
    s.dsdProcessorEnabled = static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled);

    {
        std::lock_guard<std::mutex> g(m_rateMutex);
        const auto now = std::chrono::steady_clock::now();
        if (m_lastRateTick.time_since_epoch().count() == 0) m_lastRateTick = now;
        else {
            const double seconds = std::chrono::duration<double>(now - m_lastRateTick).count();
            if (seconds >= 0.25) {
                const uint64_t delta = m_rateBytes - m_lastRateBytes;
                m_currentBps = static_cast<uint64_t>(static_cast<double>(delta) / seconds);
                m_lastRateBytes = m_rateBytes;
                m_lastRateTick = now;
            }
        }
        if (m_activeStreams && m_streamStartTick.time_since_epoch().count()) {
            s.streamElapsedMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - m_streamStartTick).count());
        }
        s.activeStreams = m_activeStreams;
        s.streamingActive = m_activeStreams != 0;
        s.bytesPerSecond = m_currentBps;
        s.totalBytesSent = m_totalBytes;
        s.streamBytesSent = m_streamBytes;
        s.cacheHits = m_cacheHits;
        s.cacheMisses = m_cacheMisses;
        s.dsdRate = m_streamDsdRate;
        s.nominalBitrate = m_streamDsdRate ? static_cast<uint64_t>(m_streamDsdRate) * 2ULL : 0ULL;
        s.requiredBytesPerSecond = m_streamDsdRate ? static_cast<uint64_t>(m_streamDsdRate) / 4ULL : 0ULL;
        s.clientIp = m_clientIp.c_str();
        s.clientName = m_clientName.c_str();
        s.clientModel = m_clientModel.c_str();
        s.streamTitle = m_streamTitle.c_str();
        s.streamArtist = m_streamArtist.c_str();
        s.streamAlbum = m_streamAlbum.c_str();
        s.sourceFormat = m_sourceFormat.c_str();
        s.outputFormat = m_outputFormat.c_str();
        s.pipelineState = m_pipelineState.c_str();
        s.conversionState = m_conversionState.c_str();
        s.sourceFileSize = m_sourceFileSize;
        s.outputFileSize = m_outputFileSize;
        s.sourceSampleRate = m_sourceSampleRate;
        s.sourceChannels = m_sourceChannels;
        s.sourceBitsPerSample = m_sourceBitsPerSample;
        s.streamChannels = m_streamChannels;
        s.streamBitsPerSample = m_streamBitsPerSample;
        s.streamDuration = m_streamDuration;
        s.sdxIp = m_sdxIp.c_str();
        s.sdxName = m_sdxName.c_str();
        s.sdxModel = m_sdxModel.c_str();
        s.sdxModelNumber = m_sdxModelNumber.c_str();
        s.sdxProtocolInfo = m_sdxProtocolInfo.c_str();
        s.sdxDetected = !m_sdxIp.empty();
        s.sdxStreaming = s.streamingActive && !m_sdxIp.empty() && !m_clientIp.empty() && m_clientIp == m_sdxIp;
        s.sdxProtocolNegotiated = !m_sdxProtocolInfo.empty();
        s.lastHttpRequest = m_lastHttpRequest.c_str();
    }
    { std::lock_guard<std::mutex> g(m_mutex); s.updateId = m_updateId; }
    {
        std::lock_guard<std::mutex> g(m_diagMutex);
        s.httpReady = m_httpReady;
        s.ssdpReady = m_ssdpReady;
        s.httpSelfTestOk = m_httpSelfTestOk;
        s.ssdpProbeOk = m_ssdpProbeOk;
        s.ssdpNotifyLoopbackOk = m_ssdpNotifyLoopbackOk;
        s.networkPresence = m_networkPresence;
        s.ssdpAliveSent = m_ssdpAliveSent;
        s.ssdpDiscoverSent = m_ssdpDiscoverSent;
        s.ssdpMSearchReceived = m_ssdpMSearchReceived;
        s.ssdpResponsesSent = m_ssdpResponsesSent;
        s.ssdpDiscoverResponses = m_ssdpDiscoverResponses;
        s.httpRequests = m_httpRequests;
        s.remoteHttpRequests = m_remoteHttpRequests;
        s.remoteSsdpSearches = m_remoteSsdpSearches;
        s.remoteHttpSeen = m_remoteHttpSeen;
        s.remoteSsdpSeen = m_remoteSsdpSeen;
        s.localIp = m_localIp.c_str();
        s.lastRemotePeer = m_lastRemotePeer.c_str();
        s.networkVisibility = m_networkVisibility.c_str();
        s.ssdpLastPeer = m_ssdpLastPeer.c_str();
        s.networkDiagnostic = m_networkDiagnostic.c_str();
    }

    s.stabilityMode = sacd_dlna_cfg::stability_mode;
    s.serverName = sacd_dlna_cfg::server_name;
    {
        std::lock_guard<std::mutex> g(m_rateMutex);
        s.conversionActive = m_conversionActive;
        s.conversionPercent = m_conversionPercent;
        s.prebufferBytes = m_prebufferBytes;
        s.prebufferTargetBytes = m_prebufferTargetBytes;
        s.prefetchTitle = m_prefetchTitle.c_str();
        s.prefetchState = m_prefetchState.c_str();
        if (!s.streamingActive || !s.prebufferTargetBytes) s.bufferState = s.streamingActive ? "STREAMING / NO READ-AHEAD" : "IDLE";
        else if (s.prebufferBytes == 0) s.bufferState = "DEPLETED / RISK OF UNDERRUN";
        else {
            const uint64_t pct = s.prebufferBytes * 100ULL / s.prebufferTargetBytes;
            if (pct >= 80) s.bufferState = "READY / FULL RESERVE";
            else if (pct >= 25) s.bufferState = "DRAINING / HEALTHY";
            else s.bufferState = "LOW / REFILL NOT AVAILABLE";
        }
        if (s.nominalBitrate && s.bytesPerSecond) s.networkHeadroom = static_cast<double>(s.bytesPerSecond) / static_cast<double>(s.nominalBitrate);
        if (s.requiredBytesPerSecond && s.bytesPerSecond) s.realtimeMultiplier = static_cast<double>(s.bytesPerSecond) / static_cast<double>(s.requiredBytesPerSecond);
    }
    s.cacheBytes = persistent_cache_bytes();
    return s;
}

void SacdDlnaServer::updateStreamStart(const std::string& peerIp, const DsdTrack& track,
                                       const std::string& sourceFormat, const std::string& outputFormat,
                                       const std::string& pipelineState, const std::string& conversionState,
                                       uint64_t sourceSize, uint64_t outputSize, uint32_t sourceSampleRate,
                                       uint32_t sourceChannels, uint32_t sourceBitsPerSample) {
    std::lock_guard<std::mutex> g(m_rateMutex);
    ++m_activeStreams;
    m_clientIp = peerIp;
    m_streamTitle = track.title;
    m_streamArtist = track.artist;
    m_streamAlbum = track.album;
    m_streamDsdRate = track.dsdRate;
    m_streamChannels = track.channels;
    m_streamBitsPerSample = track.bitsPerSample;
    m_streamDuration = track.duration;
    m_sourceFormat = sourceFormat;
    m_outputFormat = outputFormat;
    m_pipelineState = pipelineState;
    m_conversionState = conversionState;
    m_sourceFileSize = sourceSize;
    m_outputFileSize = outputSize;
    m_sourceSampleRate = sourceSampleRate;
    m_sourceChannels = sourceChannels;
    m_sourceBitsPerSample = sourceBitsPerSample;
    m_streamBytes = 0;
    m_rateBytes = 0;
    m_lastRateBytes = 0;
    m_currentBps = 0;
    m_lastRateTick = std::chrono::steady_clock::now();
    m_streamStartTick = m_lastRateTick;
}

void SacdDlnaServer::updateStreamBytes(uint64_t bytes) {
    std::lock_guard<std::mutex> g(m_rateMutex);
    m_rateBytes += bytes;
    m_totalBytes += bytes;
    m_streamBytes += bytes;
}

void SacdDlnaServer::updateStreamEnd() {
    std::lock_guard<std::mutex> g(m_rateMutex);
    if (m_activeStreams) --m_activeStreams;
    if (m_streamStartTick.time_since_epoch().count()) {
        m_streamElapsedMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_streamStartTick).count());
    }
    if (m_activeStreams == 0) {
        m_currentBps = 0;
        m_streamDsdRate = 0;
        m_streamTitle.clear();
        m_streamArtist.clear();
        m_streamAlbum.clear();
        m_sourceFormat.clear();
        m_outputFormat.clear();
        m_pipelineState.clear();
        m_conversionState.clear();
        m_sourceFileSize = 0;
        m_outputFileSize = 0;
        m_sourceSampleRate = 0;
        m_sourceChannels = 0;
        m_sourceBitsPerSample = 0;
        m_streamChannels = 0;
        m_streamBitsPerSample = 0;
        m_streamDuration = 0;
        m_prebufferBytes = 0;
        m_prebufferTargetBytes = 0;
        m_streamElapsedMs = 0;
    }
}

void SacdDlnaServer::updateLastHttpRequest(const std::string& request) {
    std::lock_guard<std::mutex> g(m_rateMutex);
    m_lastHttpRequest = request.substr(0, 240);
}

namespace {
std::string headerValueCI(const std::string& response, const char* header) {
    std::string lower = lowerCopy(response);
    std::string target = lowerCopy(header);
    if (!target.empty() && target.back() != ':') target += ':';
    size_t p = lower.find(target);
    while (p != std::string::npos && p != 0 && lower[p - 1] != '\n') p = lower.find(target, p + 1);
    if (p == std::string::npos) return {};
    const auto start = p + target.size();
    const auto end = lower.find("\r\n", start);
    return trimCopy(response.substr(start, end == std::string::npos ? std::string::npos : end - start));
}
}

std::string SacdDlnaServer::firstXmlTagText(const std::string& xml, const std::string& localName) {
    std::string lower = lowerCopy(xml);
    const std::string target = lowerCopy(localName);
    size_t pos = 0;
    while ((pos = lower.find('<', pos)) != std::string::npos) {
        if (pos + 1 >= lower.size() || lower[pos + 1] == '/' || lower[pos + 1] == '?' || lower[pos + 1] == '!') { ++pos; continue; }
        const auto gt = lower.find('>', pos + 1);
        if (gt == std::string::npos) break;
        std::string name = lower.substr(pos + 1, gt - pos - 1);
        const auto space = name.find_first_of(" \t\r\n");
        if (space != std::string::npos) name.resize(space);
        const auto colon = name.rfind(':');
        if (colon != std::string::npos) name = name.substr(colon + 1);
        if (name == target) {
            const std::string close = "</" + name + ">";
            const auto end = lower.find(close, gt + 1);
            if (end == std::string::npos) {
                const size_t pfx = lower.find(":" + name + ">", gt + 1);
                if (pfx == std::string::npos) return {};
                return xml.substr(gt + 1, pfx - (gt + 1));
            }
            return xml.substr(gt + 1, end - gt - 1);
        }
        pos = gt + 1;
    }
    return {};
}

std::string SacdDlnaServer::extractSoapArg(const std::string& soap, const char* localName, const std::string& fallback) {
    auto v = firstXmlTagText(soap, localName);
    return v.empty() ? fallback : decodeXmlEntities(v);
}

unsigned SacdDlnaServer::extractSoapUint(const std::string& soap, const char* localName, unsigned fallback) {
    auto v = extractSoapArg(soap, localName, {});
    if (v.empty()) return fallback;
    try { return static_cast<unsigned>(std::stoul(v)); } catch (...) { return fallback; }
}

std::string SacdDlnaServer::xmlLocalTagValueCI(const std::string& xml, const char* localName) {
    return firstXmlTagText(xml, localName);
}

std::string SacdDlnaServer::resolveUrl(const std::string& baseUrl, const std::string& relative) {
    if (relative.empty()) return {};
    if (relative.find("://") != std::string::npos) return relative;
    UrlParts base;
    if (!parseUrl(baseUrl, base)) return relative;
    const std::string authority = base.scheme + "://" + base.host + (base.port != 80 ? ":" + std::to_string(base.port) : "");
    if (relative.front() == '/') return authority + relative;
    std::string dir = base.path;
    const auto slash = dir.rfind('/');
    dir = slash == std::string::npos ? "/" : dir.substr(0, slash + 1);
    return authority + dir + relative;
}

std::string SacdDlnaServer::extractConnectionManagerControl(const std::string& deviceXml, const std::string& baseUrl) {
    const std::string lower = lowerCopy(deviceXml);
    size_t pos = 0;
    while ((pos = lower.find("<service", pos)) != std::string::npos) {
        const auto end = lower.find("</service>", pos);
        if (end == std::string::npos) break;
        const auto block = deviceXml.substr(pos, end + 10 - pos);
        const auto st = firstXmlTagText(block, "serviceType");
        if (lowerCopy(st).find("connectionmanager") != std::string::npos) {
            return resolveUrl(baseUrl, firstXmlTagText(block, "controlURL"));
        }
        pos = end + 10;
    }
    return {};
}

std::string SacdDlnaServer::extractProtocolList(const std::string& soap, const char* tag) {
    return trimCopy(decodeXmlEntities(firstXmlTagText(soap, tag)));
}

bool SacdDlnaServer::protocolListSupports(const std::string& protocols, const std::string& mime) {
    const auto lower = lowerCopy(protocols);
    const auto needle = ":" + lowerCopy(mime) + ":";
    return lower.find(needle) != std::string::npos || lower.find(":" + lowerCopy(mime)) != std::string::npos;
}

std::string SacdDlnaServer::chooseRendererMime(const std::string& defaultMime) const {
    std::lock_guard<std::mutex> g(m_rateMutex);
    const auto lowerDefault = lowerCopy(defaultMime);
    const auto protocols = lowerCopy(m_sdxProtocolInfo);
    if (!protocols.empty()) {
        if (lowerDefault.find("dsf") != std::string::npos) {
            if (protocolListSupports(protocols, "audio/x-dsf")) return "audio/x-dsf";
            if (protocolListSupports(protocols, "audio/dsf")) return "audio/dsf";
        } else if (lowerDefault.find("dff") != std::string::npos) {
            if (protocolListSupports(protocols, "audio/x-dff")) return "audio/x-dff";
            if (protocolListSupports(protocols, "audio/dff")) return "audio/dff";
        }
    }
    return defaultMime;
}

std::string SacdDlnaServer::chooseRendererProtocolInfo(const std::string& defaultMime) const {
    const std::string mime = chooseRendererMime(defaultMime);
    {
        std::lock_guard<std::mutex> g(m_rateMutex);
        const std::string protocols = m_sdxProtocolInfo.c_str();
        if (!protocols.empty()) {
            size_t start = 0;
            while (start < protocols.size()) {
                size_t end = protocols.find(',', start);
                if (end == std::string::npos) end = protocols.size();
                const std::string token = trimCopy(protocols.substr(start, end - start));
                const auto c1 = token.find(':');
                const auto c2 = c1 == std::string::npos ? std::string::npos : token.find(':', c1 + 1);
                const auto c3 = c2 == std::string::npos ? std::string::npos : token.find(':', c2 + 1);
                if (c3 != std::string::npos) {
                    const std::string tokenMime = token.substr(c2 + 1, c3 - c2 - 1);
                    if (_stricmp(tokenMime.c_str(), mime.c_str()) == 0) {
                        networkLog("T+A protocol match selected: " + token);
                        return token;
                    }
                }
                start = end + 1;
            }
        }
    }
    // No exact negotiated token was found; advertise the standard DLNA profile.
    return didlProtocolInfo(mime);
}

void SacdDlnaServer::probeSdxConnectionManager(const std::string& controlUrl, const std::string& peerIp) {
    if (controlUrl.empty()) return;
    const std::string service = "urn:schemas-upnp-org:service:ConnectionManager:1";
    const std::string body = soapEnvelope(service, "GetProtocolInfo", {});
    std::string headers, response;
    if (!httpRequestSimple("POST", controlUrl, body, headers, response, (service + "#GetProtocolInfo").c_str())) return;
    const std::string sink = extractProtocolList(response, "Sink");
    {
        std::lock_guard<std::mutex> g(m_rateMutex);
        m_sdxProtocolInfo = sink;
        m_sdxConnectionManagerControl = controlUrl;
        m_sdxIp = peerIp;
    }
    networkLog("T+A ConnectionManager Sink: " + sink);
}

void SacdDlnaServer::probeSdxDescription(const std::string& location, const std::string& peerIp) {
    std::string headers, xml;
    if (!httpRequestSimple("GET", location, {}, headers, xml)) return;

    const auto friendly = firstXmlTagText(xml, "friendlyName");
    const auto model = firstXmlTagText(xml, "modelName");
    const auto manufacturer = firstXmlTagText(xml, "manufacturer");
    const auto modelNumber = firstXmlTagText(xml, "modelNumber");
    const auto hay = lowerCopy(friendly + " " + model + " " + manufacturer + " " + xml);
    if (hay.find("t+a") == std::string::npos && hay.find("sdx") == std::string::npos && hay.find("sd 3100 hv") == std::string::npos) return;

    const auto control = extractConnectionManagerControl(xml, location);
    {
        std::lock_guard<std::mutex> g(m_rateMutex);
        m_sdxIp = peerIp;
        m_sdxName = friendly.empty() ? "T+A renderer" : friendly;
        m_sdxModel = model;
        m_sdxManufacturer = manufacturer;
        m_sdxModelNumber = modelNumber;
        m_sdxConnectionManagerControl = control;
    }
    networkLog("T+A SDX detected at " + peerIp + " / " + m_sdxName + " / model " + m_sdxModelNumber);
    if (!control.empty()) probeSdxConnectionManager(control, peerIp);
}

void SacdDlnaServer::discoverSdxResponse(const std::string& response, const std::string& peerIp) {
    const auto location = headerValueCI(response, "LOCATION");
    if (!location.empty()) probeSdxDescription(location, peerIp);
}

std::string SacdDlnaServer::xmlEscape(const std::string& s) { return xmlTextEscape(s); }

std::string SacdDlnaServer::urlPathDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + c - 'a';
                if (c >= 'A' && c <= 'F') return 10 + c - 'A';
                return -1;
            };
            const int a = hex(s[i + 1]), b = hex(s[i + 2]);
            if (a >= 0 && b >= 0) { out.push_back(static_cast<char>((a << 4) | b)); i += 2; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}

std::string SacdDlnaServer::normalizeKey(const std::string& s) { return lowerCopy(s); }

std::string SacdDlnaServer::mimeForExtension(const std::string& ext) {
    if (!_stricmp(ext.c_str(), ".dsf")) return "audio/x-dsf";
    if (!_stricmp(ext.c_str(), ".dff")) return "audio/x-dff";
    return "audio/x-dsf";
}

std::string SacdDlnaServer::detectImageMime(const void* data, size_t size) {
    if (!data || size < 4) return {};
    const auto* p = static_cast<const uint8_t*>(data);
    if (size >= 8 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G') return "image/png";
    if (p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return "image/jpeg";
    if (size >= 12 && !memcmp(p, "RIFF", 4) && !memcmp(p + 8, "WEBP", 4)) return "image/webp";
    if (size >= 6 && !memcmp(p, "GIF87a", 6)) return "image/gif";
    if (size >= 6 && !memcmp(p, "GIF89a", 6)) return "image/gif";
    if (size >= 2 && p[0] == 'B' && p[1] == 'M') return "image/bmp";
    if (size >= 4 && ((p[0] == 'I' && p[1] == 'I' && p[2] == 42 && p[3] == 0) || (p[0] == 'M' && p[1] == 'M' && p[2] == 0 && p[3] == 42))) return "image/tiff";
    return "application/octet-stream";
}

std::string SacdDlnaServer::didlProtocolInfo(const std::string& mime) {
    return "http-get:*:" + mime + ":DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000";
}

std::wstring SacdDlnaServer::cacheFolder() const { return persistentCacheFolder(); }

std::string SacdDlnaServer::localAddress() const {
    char host[256]{};
    if (gethostname(host, sizeof(host)) != 0) return "127.0.0.1";
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return "127.0.0.1";
    std::string fallback = "127.0.0.1";
    for (addrinfo* it = res; it; it = it->ai_next) {
        const auto* sin = reinterpret_cast<const sockaddr_in*>(it->ai_addr);
        if (!sin) continue;
        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
        const std::string value = ip;
        if (value != "127.0.0.1" && !value.empty() && value.rfind("169.254.", 0) != 0) { fallback = value; break; }
    }
    freeaddrinfo(res);
    return fallback;
}

std::string SacdDlnaServer::makeDeviceXml() const {
    const std::string name = xmlEscape(sacd_dlna_cfg::server_name.get().c_str());
    const std::string base = "http://" + localAddress() + ":" + std::to_string(m_port);
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<root xmlns=\"urn:schemas-upnp-org:device-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion>"
        "<device><deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>"
        "<friendlyName>" + name + "</friendlyName><manufacturer>foo_sacd_dlna</manufacturer>"
        "<manufacturerURL>https://www.foobar2000.org/</manufacturerURL><modelName>foobar2000 SACD DLNA</modelName>"
        "<modelDescription>Native DSD UPnP Media Server</modelDescription><modelNumber>0.8-alpha3-j</modelNumber>"
        "<serialNumber>foo-sacd-dlna</serialNumber><UDN>" + kUuid + "</UDN><presentationURL>" + base + "/status</presentationURL>"
        "<serviceList>"
        "<service><serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType><serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>"
        "<SCPDURL>/ContentDirectory.xml</SCPDURL><controlURL>/ctl/ContentDirectory</controlURL><eventSubURL>/evt/ContentDirectory</eventSubURL></service>"
        "<service><serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType><serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>"
        "<SCPDURL>/ConnectionManager.xml</SCPDURL><controlURL>/ctl/ConnectionManager</controlURL><eventSubURL>/evt/ConnectionManager</eventSubURL></service>"
        "</serviceList></device></root>";
}

std::string SacdDlnaServer::makeContentDirectoryScpd() const {
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion>"
        "<actionList>"
        "<action><name>Browse</name><argumentList>"
        "<argument><name>ObjectID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_ObjectID</relatedStateVariable></argument>"
        "<argument><name>BrowseFlag</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_BrowseFlag</relatedStateVariable></argument>"
        "<argument><name>Filter</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Filter</relatedStateVariable></argument>"
        "<argument><name>StartingIndex</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Index</relatedStateVariable></argument>"
        "<argument><name>RequestedCount</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>"
        "<argument><name>SortCriteria</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_SortCriteria</relatedStateVariable></argument>"
        "<argument><name>Result</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Result</relatedStateVariable></argument>"
        "<argument><name>NumberReturned</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>"
        "<argument><name>TotalMatches</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>"
        "<argument><name>UpdateID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_UpdateID</relatedStateVariable></argument>"
        "</argumentList></action>"
        "<action><name>GetSystemUpdateID</name><argumentList><argument><name>Id</name><direction>out</direction><relatedStateVariable>SystemUpdateID</relatedStateVariable></argument></argumentList></action>"
        "</actionList><serviceStateTable>"
        "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ObjectID</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_BrowseFlag</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Filter</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Index</name><dataType>ui4</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Count</name><dataType>ui4</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_SortCriteria</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Result</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>SystemUpdateID</name><dataType>ui4</dataType></stateVariable>"
        "</serviceStateTable></scpd>";
}

std::string SacdDlnaServer::makeConnectionManagerScpd() const {
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion>"
        "<actionList><action><name>GetProtocolInfo</name><argumentList>"
        "<argument><name>Source</name><direction>out</direction><relatedStateVariable>SourceProtocolInfo</relatedStateVariable></argument>"
        "<argument><name>Sink</name><direction>out</direction><relatedStateVariable>SinkProtocolInfo</relatedStateVariable></argument>"
        "</argumentList></action></actionList><serviceStateTable>"
        "<stateVariable sendEvents=\"no\"><name>SourceProtocolInfo</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>SinkProtocolInfo</name><dataType>string</dataType></stateVariable>"
        "</serviceStateTable></scpd>";
}

bool SacdDlnaServer::readDsfHeader(const std::wstring& path, uint32_t& rate, uint32_t& channels, uint64_t& samplesPerChannel, uint64_t& fileSize) {
    rate = 0; channels = 0; samplesPerChannel = 0; fileSize = 0;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    fileSize = static_cast<uint64_t>(f.tellg());
    if (fileSize < 92) return false;

    char dsdMagic[4]{};
    f.seekg(0);
    f.read(dsdMagic, 4);
    if (!f || memcmp(dsdMagic, "DSD ", 4) != 0) return false;

    uint64_t dsdChunkSize = 0, advertisedFileSize = 0;
    f.read(reinterpret_cast<char*>(&dsdChunkSize), sizeof(dsdChunkSize));
    f.read(reinterpret_cast<char*>(&advertisedFileSize), sizeof(advertisedFileSize));
    if (!f || dsdChunkSize != 28 || advertisedFileSize > fileSize) return false;

    char fmtMagic[4]{};
    f.seekg(28);
    f.read(fmtMagic, 4);
    if (!f || memcmp(fmtMagic, "fmt ", 4) != 0) return false;

    uint64_t fmtChunkSize = 0;
    uint32_t formatVersion = 0, formatId = 0, channelType = 0, bitsPerSample = 0, blockSize = 0, reserved = 0;
    f.read(reinterpret_cast<char*>(&fmtChunkSize), sizeof(fmtChunkSize));
    f.read(reinterpret_cast<char*>(&formatVersion), sizeof(formatVersion));
    f.read(reinterpret_cast<char*>(&formatId), sizeof(formatId));
    f.read(reinterpret_cast<char*>(&channelType), sizeof(channelType));
    f.read(reinterpret_cast<char*>(&channels), sizeof(channels));
    f.read(reinterpret_cast<char*>(&rate), sizeof(rate));
    f.read(reinterpret_cast<char*>(&bitsPerSample), sizeof(bitsPerSample));
    f.read(reinterpret_cast<char*>(&samplesPerChannel), sizeof(samplesPerChannel));
    f.read(reinterpret_cast<char*>(&blockSize), sizeof(blockSize));
    f.read(reinterpret_cast<char*>(&reserved), sizeof(reserved));
    if (!f || fmtChunkSize != 52 || formatVersion != 1 || formatId != 0 || channelType != 2 ||
        channels != 2 || bitsPerSample != 1 || blockSize != 4096 || reserved != 0) return false;
    if (rate != 2822400 && rate != 5644800 && rate != 11289600) return false;
    if (samplesPerChannel == 0) return false;

    char dataMagic[4]{};
    f.read(dataMagic, 4);
    if (!f || memcmp(dataMagic, "data", 4) != 0) return false;
    uint64_t dataChunkSize = 0;
    f.read(reinterpret_cast<char*>(&dataChunkSize), sizeof(dataChunkSize));
    if (!f || dataChunkSize < 12 || 92 > fileSize || dataChunkSize > fileSize - 80) return false;
    return true;
}

std::string SacdDlnaServer::browseDidl(const std::string& objectId, bool metadataOnly,
                                       unsigned startingIndex, unsigned requestedCount,
                                       unsigned& numberReturned, unsigned& totalMatches) const {
    std::vector<Item> items;
    std::vector<Artist> artists;
    std::vector<Album> albums;
    uint32_t updateId = 1;
    {
        std::lock_guard<std::mutex> g(m_mutex);
        items = m_items; artists = m_artists; albums = m_albums; updateId = m_updateId;
    }

    numberReturned = totalMatches = 0;
    const std::string ns = " xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\""
        " xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
        " xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\""
        " xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0/\"";
    std::string out = "<DIDL-Lite" + ns + ">";
    const std::string base = "http://" + localAddress() + ":" + std::to_string(m_port);

    auto findArtist = [&](uint32_t id) -> const Artist* { for (const auto& x : artists) if (x.id == id) return &x; return nullptr; };
    auto findAlbum = [&](uint32_t id) -> const Album* { for (const auto& x : albums) if (x.id == id) return &x; return nullptr; };
    auto findItem = [&](uint32_t id) -> const Item* { for (const auto& x : items) if (x.id == id) return &x; return nullptr; };

    auto artUri = [&](uint32_t albumId) { return base + "/art/" + std::to_string(albumId); };

    auto appendContainer = [&](const std::string& id, const std::string& parent, const std::string& title, const char* cls, size_t childCount, uint32_t artAlbumId = 0) {
        out += "<container id=\"" + xmlEscape(id) + "\" parentID=\"" + xmlEscape(parent) + "\" restricted=\"1\" childCount=\"" + std::to_string(childCount) + "\">";
        out += "<dc:title>" + xmlEscape(title) + "</dc:title><upnp:class>" + cls + "</upnp:class>";
        if (artAlbumId) out += "<upnp:albumArtURI>" + artUri(artAlbumId) + "</upnp:albumArtURI>";
        out += "</container>";
    };

    auto appendTrack = [&](const Item& item, const std::string& parentId, const std::string& objectIdForTrack) {
        const std::string title = item.track.title.empty() ? ("Track " + std::to_string(item.id)) : item.track.title;
        const std::string servedExt = (_stricmp(item.sourceExt.c_str(), ".iso") == 0) ? ".dsf" : item.sourceExt;
        const std::string defaultMime = mimeForExtension(servedExt);
        const std::string mime = chooseRendererMime(defaultMime);
        out += "<item id=\"" + xmlEscape(objectIdForTrack) + "\" parentID=\"" + xmlEscape(parentId) + "\" restricted=\"1\">";
        out += "<dc:title>" + xmlEscape(title) + "</dc:title>";
        if (!item.track.artist.empty()) {
            out += "<dc:creator>" + xmlEscape(item.track.artist) + "</dc:creator>";
            out += "<upnp:artist role=\"Performer\">" + xmlEscape(item.track.artist) + "</upnp:artist>";
        }
        if (!item.track.albumArtist.empty()) out += "<upnp:albumArtist>" + xmlEscape(item.track.albumArtist) + "</upnp:albumArtist>";
        else if (!item.track.artist.empty()) out += "<upnp:artist role=\"AlbumArtist\">" + xmlEscape(item.track.artist) + "</upnp:artist>";
        if (!item.track.album.empty()) out += "<upnp:album>" + xmlEscape(item.track.album) + "</upnp:album>";
        if (!item.track.genre.empty()) out += "<upnp:genre>" + xmlEscape(item.track.genre) + "</upnp:genre>";
        if (!item.track.trackNumber.empty()) out += "<upnp:originalTrackNumber>" + xmlEscape(item.track.trackNumber) + "</upnp:originalTrackNumber>";
        if (!item.track.discNumber.empty()) out += "<upnp:originalDiscNumber>" + xmlEscape(item.track.discNumber) + "</upnp:originalDiscNumber>";
        if (!item.track.date.empty()) out += "<dc:date>" + xmlEscape(item.track.date) + "</dc:date>";
        if (!item.track.composer.empty()) out += "<upnp:artist role=\"Composer\">" + xmlEscape(item.track.composer) + "</upnp:artist>";
        if (!item.track.publisher.empty()) out += "<dc:publisher>" + xmlEscape(item.track.publisher) + "</dc:publisher>";
        if (!item.track.comment.empty()) out += "<upnp:longDescription>" + xmlEscape(item.track.comment) + "</upnp:longDescription>";
        out += "<upnp:class>object.item.audioItem.musicTrack</upnp:class>";
        if (item.albumId) out += "<upnp:albumArtURI>" + artUri(item.albumId) + "</upnp:albumArtURI>";

        std::string res = "<res protocolInfo=\"" + chooseRendererProtocolInfo(mime) + "\"";
        if (item.track.fileSize) res += " size=\"" + std::to_string(item.track.fileSize) + "\"";
        if (item.track.duration > 0) res += " duration=\"" + formatDuration(item.track.duration) + "\"";
        if (item.track.dsdRate) {
            res += " sampleFrequency=\"" + std::to_string(item.track.dsdRate) + "\"";
            res += " bitsPerSample=\"1\"";
            res += " nrAudioChannels=\"" + std::to_string(item.track.channels ? item.track.channels : 2) + "\"";
            // DIDL-Lite res@bitrate is expressed in bytes/second, not bits/second.
            res += " bitrate=\"" + std::to_string(static_cast<uint64_t>(item.track.dsdRate) / 4ULL) + "\"";
        }
        res += ">" + base + "/media/" + std::to_string(item.id) + servedExt + "</res>";
        out += res;
        out += "</item>";
    };

    if (metadataOnly) {
        if (objectId == kRootObject) {
            appendContainer(kRootObject, "-1", "foobar2000 SACD DSD", "object.container", 1);
            numberReturned = totalMatches = 1;
        } else if (objectId == kArtistsObject) {
            appendContainer(kArtistsObject, kRootObject, "Artists", "object.container.person.musicArtist", artists.size());
            numberReturned = totalMatches = 1;
        } else if (objectId.rfind(kArtistPrefix, 0) == 0) {
            uint32_t id = 0; try { id = std::stoul(objectId.substr(strlen(kArtistPrefix))); } catch (...) {}
            if (const auto* a = findArtist(id)) { appendContainer(objectId, kArtistsObject, a->name, "object.container.person.musicArtist", a->albumIds.size()); numberReturned = totalMatches = 1; }
        } else if (objectId.rfind(kAlbumPrefix, 0) == 0) {
            uint32_t id = 0; try { id = std::stoul(objectId.substr(strlen(kAlbumPrefix))); } catch (...) {}
            if (const auto* a = findAlbum(id)) { appendContainer(objectId, std::string(kArtistPrefix) + std::to_string(a->artistId), a->title, "object.container.album.musicAlbum", a->itemIds.size(), a->id); numberReturned = totalMatches = 1; }
        } else if (objectId.rfind(kTrackPrefix, 0) == 0) {
            uint32_t id = 0; try { id = std::stoul(objectId.substr(strlen(kTrackPrefix))); } catch (...) {}
            if (const auto* i = findItem(id)) { const auto parent = std::string(kAlbumPrefix) + std::to_string(i->albumId); appendTrack(*i, parent, objectId); numberReturned = totalMatches = 1; }
        }
    } else if (objectId == kRootObject) {
        appendContainer(kArtistsObject, kRootObject, "Artists", "object.container.person.musicArtist", artists.size());
        numberReturned = totalMatches = 1;
    } else if (objectId == kArtistsObject) {
        totalMatches = static_cast<unsigned>(artists.size());
        const unsigned requestedEnd = startingIndex + requestedCount;
            const unsigned end = requestedCount ? (requestedEnd < totalMatches ? requestedEnd : totalMatches) : totalMatches;
        for (unsigned n = startingIndex; n < end; ++n) {
            const auto& a = artists[n];
            appendContainer(std::string(kArtistPrefix) + std::to_string(a.id), kArtistsObject, a.name, "object.container.person.musicArtist", a.albumIds.size());
            ++numberReturned;
        }
    } else if (objectId.rfind(kArtistPrefix, 0) == 0) {
        uint32_t id = 0; try { id = std::stoul(objectId.substr(strlen(kArtistPrefix))); } catch (...) {}
        const auto* a = findArtist(id);
        if (a) {
            totalMatches = static_cast<unsigned>(a->albumIds.size());
            const unsigned requestedEnd = startingIndex + requestedCount;
            const unsigned end = requestedCount ? (requestedEnd < totalMatches ? requestedEnd : totalMatches) : totalMatches;
            for (unsigned n = startingIndex; n < end; ++n) {
                const auto* alb = findAlbum(a->albumIds[n]); if (!alb) continue;
                appendContainer(std::string(kAlbumPrefix) + std::to_string(alb->id), objectId, alb->title, "object.container.album.musicAlbum", alb->itemIds.size(), alb->id);
                ++numberReturned;
            }
        }
    } else if (objectId.rfind(kAlbumPrefix, 0) == 0) {
        uint32_t id = 0; try { id = std::stoul(objectId.substr(strlen(kAlbumPrefix))); } catch (...) {}
        const auto* a = findAlbum(id);
        if (a) {
            totalMatches = static_cast<unsigned>(a->itemIds.size());
            const unsigned requestedEnd = startingIndex + requestedCount;
            const unsigned end = requestedCount ? (requestedEnd < totalMatches ? requestedEnd : totalMatches) : totalMatches;
            for (unsigned n = startingIndex; n < end; ++n) {
                const auto* item = findItem(a->itemIds[n]); if (!item) continue;
                appendTrack(*item, objectId, std::string(kTrackPrefix) + std::to_string(item->id));
                ++numberReturned;
            }
        }
    } else if (objectId.rfind(kTrackPrefix, 0) == 0) {
        uint32_t id = 0; try { id = std::stoul(objectId.substr(strlen(kTrackPrefix))); } catch (...) {}
        if (const auto* i = findItem(id)) { appendTrack(*i, std::string(kAlbumPrefix) + std::to_string(i->albumId), objectId); numberReturned = totalMatches = 1; }
    }

    out += "</DIDL-Lite>";
    (void)updateId;
    return out;
}

std::string SacdDlnaServer::browseResponse(const std::string& objectId, const std::string& browseFlag,
                                           unsigned startingIndex, unsigned requestedCount,
                                           const std::string&, const std::string&) const {
    const bool metadataOnly = _stricmp(browseFlag.c_str(), "BrowseMetadata") == 0;
    unsigned numberReturned = 0, totalMatches = 0;
    const std::string didl = browseDidl(objectId, metadataOnly, startingIndex, requestedCount, numberReturned, totalMatches);
    const auto st = get_status();
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
        "<Result>" + xmlEscape(didl) + "</Result><NumberReturned>" + std::to_string(numberReturned) + "</NumberReturned>"
        "<TotalMatches>" + std::to_string(totalMatches) + "</TotalMatches><UpdateID>" + std::to_string(st.updateId) + "</UpdateID>"
        "</u:BrowseResponse></s:Body></s:Envelope>";
}

std::string SacdDlnaServer::systemUpdateIdResponse() const {
    std::lock_guard<std::mutex> g(m_mutex);
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
        "<u:GetSystemUpdateIDResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Id>" + std::to_string(m_updateId) + "</Id>"
        "</u:GetSystemUpdateIDResponse></s:Body></s:Envelope>";
}

void SacdDlnaServer::setConversionStatus(bool active, uint32_t percent) {
    std::lock_guard<std::mutex> g(m_rateMutex);
    m_conversionActive = active;
    m_conversionPercent = percent > 100 ? 100 : percent;
    if (active) {
        if (static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled)) m_conversionState = "DSP CONVERTING";
        else m_conversionState = "SACD DECODE / CACHE";
    } else if (percent == 0 && m_activeStreams == 0) {
        m_conversionState.clear();
        m_pipelineState.clear();
    }
}

void SacdDlnaServer::setPreparingTrack(const Item& item, const std::string& outputFormat,
                                       const std::string& pipelineState, const std::string& conversionState) {
    std::lock_guard<std::mutex> g(m_rateMutex);
    m_streamTitle = item.track.title;
    m_streamArtist = item.track.artist;
    m_streamAlbum = item.track.album;
    m_streamDsdRate = item.track.dsdRate;
    m_streamChannels = item.track.channels;
    m_streamBitsPerSample = item.track.bitsPerSample;
    m_streamDuration = item.track.duration;
    m_sourceFormat = item.sourceExt;
    if (!m_sourceFormat.empty() && m_sourceFormat.front() == '.') m_sourceFormat.erase(m_sourceFormat.begin());
    std::transform(m_sourceFormat.begin(), m_sourceFormat.end(), m_sourceFormat.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    m_outputFormat = outputFormat;
    m_pipelineState = pipelineState;
    m_conversionState = conversionState;
    m_sourceFileSize = item.sourceSize;
    m_outputFileSize = 0;
    m_sourceSampleRate = item.sourceSampleRate;
    m_sourceChannels = item.sourceChannels;
    m_sourceBitsPerSample = item.sourceBitsPerSample;
}


bool SacdDlnaServer::ensureProcessedDsf(const Item& item, std::wstring& cachePath, DsdTrack& track, abort_callback& abort, bool reportProgress) {
    if (!static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled)) return false;
    if (!DsdProcessorBridge::installed()) return false;

    const auto key = cacheKeyFor(item.sourcePath, item.subsong);
    const auto path = cacheFolder() + L"\\dsp-" + utf8ToWide((key + ".dsf").c_str());
    const auto manifest = cacheMetaPath(path);
    uint32_t rate = 0, channels = 0; uint64_t samples = 0, fileSize = 0;

    auto cacheValid = [&]() {
        if (!readDsfHeader(path, rate, channels, samples, fileSize)) return false;
        if (!isRegularFile(manifest)) return false;
        std::ifstream mf(manifest, std::ios::binary);
        std::string line((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
        const auto decoder = std::string("\"decoderVersion\":\"");
        pfc::string8 dspVersion;
        dsd_processor_installed(&dspVersion);
        const auto dspMarker = std::string("\"dspPreset\":\"") + DsdProcessorBridge::preset_fingerprint() + "\"";
        return line.find("\"cacheVersion\":" + std::to_string(kCacheFormatVersion)) != std::string::npos &&
            line.find("\"subsong\":" + std::to_string(item.subsong)) != std::string::npos &&
            line.find("\"sourceSize\":" + std::to_string(item.sourceSize)) != std::string::npos &&
            line.find("\"sourceWriteTime\":" + std::to_string(item.sourceWriteTime)) != std::string::npos &&
            line.find("\"dspVersion\":\"" + std::string(dspVersion.c_str()) + "\"") != std::string::npos &&
            line.find(dspMarker) != std::string::npos && fileSize > 84;
    };

    if (cacheValid()) {
        { std::lock_guard<std::mutex> g(m_rateMutex); ++m_cacheHits; }
        cachePath = path; track = item.track; track.path = path; track.dsdRate = rate;
        track.channels = channels; track.bitsPerSample = 1; track.dsdSamplesPerChannel = samples;
        track.fileSize = fileSize; track.duration = rate ? static_cast<double>(samples) / static_cast<double>(rate) : track.duration;
        return true;
    }

    { std::lock_guard<std::mutex> g(m_rateMutex); ++m_cacheMisses; }
    std::shared_ptr<CacheJob> job;
    bool leader = false;
    const std::string jobKey = std::string("dsp-") + key;
    {
        std::lock_guard<std::mutex> g(m_cacheMutex);
        auto it = m_cacheJobs.find(jobKey);
        if (it != m_cacheJobs.end()) job = it->second;
        else { job = std::make_shared<CacheJob>(); m_cacheJobs.emplace(jobKey, job); leader = true; }
    }

    if (!leader) {
        std::unique_lock<std::mutex> lk(m_cacheMutex);
        while (!job->done) { abort.check(); job->cv.wait_for(lk, std::chrono::milliseconds(100)); }
        if (!job->success || !cacheValid()) return false;
        cachePath = path; track = item.track; track.path = path; track.dsdRate = rate; track.channels = channels;
        track.bitsPerSample = 1; track.dsdSamplesPerChannel = samples; track.fileSize = fileSize;
        track.duration = rate ? static_cast<double>(samples) / static_cast<double>(rate) : track.duration;
        return true;
    }

    bool success = false;
    try {
        const auto tmp = path + L".partial";
        const auto tmpManifest = manifest + L".partial";
        DeleteFileW(tmp.c_str()); DeleteFileW(tmpManifest.c_str());
        if (reportProgress) setConversionStatus(true, 0);

        dsp_preset_impl preset;
        if (!DsdProcessorBridge::load_saved_preset(preset)) throw std::runtime_error("unable to load DSD Processor preset");
        dsp_chain_config_impl chain;
        chain.add_item(preset);
        dsp_manager dspmgr(dsp_entry::flag_conversion);
        dspmgr.set_config(chain);

        const char* pathUtf8 = item.sourcePath.c_str();
        service_ptr_t<input_info_reader> infoReader;
        input_entry::g_open_for_info_read(infoReader, nullptr, pathUtf8, abort);
        file_info_impl info; infoReader->get_info(item.subsong, info, abort);
        const double duration = info.get_length();

        service_ptr_t<input_decoder> decoder;
        bool sourceDsd = !_stricmp(item.sourceExt.c_str(), ".iso") || !_stricmp(item.sourceExt.c_str(), ".dsf") || !_stricmp(item.sourceExt.c_str(), ".dff");
        if (!_stricmp(item.sourceExt.c_str(), ".iso")) {
            auto sacdInput = SacdDecoder::findFooSacd(pathUtf8);
            if (!sacdInput.is_valid()) throw std::runtime_error("foo_input_sacd is not installed or does not handle this SACD ISO");
            sacdInput->open_for_decoding(decoder, nullptr, pathUtf8, abort);
        } else {
            input_entry::g_open_for_decoding(decoder, nullptr, pathUtf8, abort);
        }
        const unsigned flags = input_flag_no_seeking | input_flag_no_looping | input_flag_playback | (sourceDsd ? input_flag_dop : 0);
        decoder->initialize(item.subsong, flags, abort);

        DsfWriter writer(tmp);
        bool started = false;
        uint32_t dsdRate = 0; uint64_t samplesPerChannel = 0;
        const auto handle = item.handle;
        for (;;) {
            abort.check();
            audio_chunk_impl_temporary in;
            if (!decoder->run(in, abort)) break;
            std::vector<audio_chunk_impl> outputs;
            DsdProcessorBridge::process_chunk(dspmgr, handle, in, outputs, abort);
            for (const auto& out : outputs) {
                std::vector<std::vector<uint8_t>> dsd;
                uint32_t thisRate = 0;
                if (!SacdDecoder::unpackDop(out, dsd, thisRate))
                    throw std::runtime_error("DSD Processor did not output supported DoP/DSD; configure DSD Processor for DSD output (DSD64/128/256)");
                if (!started) { dsdRate = thisRate; writer.begin(dsdRate, 2); started = true; }
                else if (thisRate != dsdRate) throw std::runtime_error("DSD Processor changed DSD rate within a track");
                samplesPerChannel += static_cast<uint64_t>(dsd[0].size()) * 8ULL;
                writer.append(dsd);
                if (duration > 0) {
                    const double seconds = static_cast<double>(samplesPerChannel) / static_cast<double>(dsdRate);
                    if (reportProgress) setConversionStatus(true, static_cast<uint32_t>(std::clamp(seconds / duration * 100.0, 0.0, 100.0)));
                }
            }
        }
        std::vector<audio_chunk_impl> tail;
        DsdProcessorBridge::flush(dspmgr, handle, tail, abort);
        for (const auto& out : tail) {
            std::vector<std::vector<uint8_t>> dsd; uint32_t thisRate = 0;
            if (!SacdDecoder::unpackDop(out, dsd, thisRate)) throw std::runtime_error("DSD Processor final output was not valid DoP");
            if (!started) { dsdRate = thisRate; writer.begin(dsdRate, 2); started = true; }
            else if (thisRate != dsdRate) throw std::runtime_error("DSD Processor changed DSD rate in final data");
            samplesPerChannel += static_cast<uint64_t>(dsd[0].size()) * 8ULL;
            writer.append(dsd);
        }
        if (!started) throw std::runtime_error("DSD Processor produced no DSD output");
        writer.finish(samplesPerChannel);

        uint32_t verifyRate=0, verifyCh=0; uint64_t verifySamples=0, verifySize=0;
        if (!readDsfHeader(tmp, verifyRate, verifyCh, verifySamples, verifySize) || verifyCh != 2 || verifyRate == 0 || verifySize <= 84)
            throw std::runtime_error("generated DSP DSF failed structural validation");
        if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("unable to finalize DSP DSF cache");
        std::ofstream mf(tmpManifest, std::ios::binary | std::ios::trunc);
        if (!mf) throw std::runtime_error("unable to create DSP cache manifest");
        pfc::string8 sacdVersion, dspVersion; sacd_plugin_installed(&sacdVersion); dsd_processor_installed(&dspVersion);
        mf << "{\"cacheVersion\":" << kCacheFormatVersion
           << ",\"subsong\":" << item.subsong
           << ",\"sourceSize\":" << item.sourceSize
           << ",\"sourceWriteTime\":" << item.sourceWriteTime
           << ",\"decoderVersion\":\"" << xmlAttr(sacdVersion.c_str()) << "\""
           << ",\"dspVersion\":\"" << xmlAttr(dspVersion.c_str()) << "\""
           << ",\"dspPreset\":\"" << DsdProcessorBridge::preset_fingerprint() << "\""
           << ",\"dsdRate\":" << dsdRate
           << ",\"samplesPerChannel\":" << samplesPerChannel << "}\n";
        mf.close();
        if (!MoveFileExW(tmpManifest.c_str(), manifest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("unable to finalize DSP cache manifest");
        if (reportProgress) setConversionStatus(false, 100); success = true;
        track = item.track; track.path = path; track.dsdRate = dsdRate; track.dsdSamplesPerChannel = samplesPerChannel;
        track.channels = 2; track.bitsPerSample = 1; track.fileSize = verifySize;
        track.duration = dsdRate ? static_cast<double>(samplesPerChannel) / static_cast<double>(dsdRate) : track.duration;
    } catch (const exception_aborted&) {
        if (reportProgress) setConversionStatus(false, 0); DeleteFileW((path + L".partial").c_str()); DeleteFileW((manifest + L".partial").c_str());
        networkLog("DSP processing aborted: " + item.sourcePath);
    } catch (std::exception const& e) {
        if (reportProgress) setConversionStatus(false, 0); DeleteFileW((path + L".partial").c_str()); DeleteFileW((manifest + L".partial").c_str());
        setLastError(std::string("DSP processing failed: ") + e.what());
    }
    {
        std::lock_guard<std::mutex> g(m_cacheMutex); job->success = success; job->done = true; m_cacheJobs.erase(jobKey);
    }
    job->cv.notify_all();
    if (!success) return false;
    cachePath = path; return true;
}

bool SacdDlnaServer::ensureCached(const Item& item, std::wstring& cachePath, DsdTrack& track, abort_callback& abort, bool reportProgress) {
    if (_stricmp(item.sourceExt.c_str(), ".iso") != 0) return false;
    const auto key = cacheKeyFor(item.sourcePath, item.subsong);
    const auto path = cacheFolder() + L"\\" + utf8ToWide((key + ".dsf").c_str());
    const auto manifest = cacheMetaPath(path);
    uint32_t rate = 0, channels = 0; uint64_t samples = 0, fileSize = 0;

    auto cacheValid = [&]() {
        if (!readDsfHeader(path, rate, channels, samples, fileSize)) return false;
        if (!isRegularFile(manifest)) return false;
        std::ifstream mf(manifest, std::ios::binary);
        std::string line((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
        const auto version = std::string("\"cacheVersion\":") + std::to_string(kCacheFormatVersion);
        const auto source = std::string("\"subsong\":") + std::to_string(item.subsong);
        const auto sourceSize = std::string("\"sourceSize\":") + std::to_string(item.sourceSize);
        const auto sourceTime = std::string("\"sourceWriteTime\":") + std::to_string(item.sourceWriteTime);
        return line.find(version) != std::string::npos && line.find(source) != std::string::npos &&
            line.find(sourceSize) != std::string::npos && line.find(sourceTime) != std::string::npos && fileSize > 84;
    };

    if (cacheValid()) {
        { std::lock_guard<std::mutex> g(m_rateMutex); ++m_cacheHits; }
        cachePath = path; track = item.track; track.path = path; track.dsdRate = rate; track.channels = channels;
        track.bitsPerSample = 1; track.dsdSamplesPerChannel = samples; track.fileSize = fileSize;
        track.duration = rate ? static_cast<double>(samples) / static_cast<double>(rate) : track.duration;
        return true;
    }

    { std::lock_guard<std::mutex> g(m_rateMutex); ++m_cacheMisses; }
    std::shared_ptr<CacheJob> job;
    bool leader = false;
    {
        std::lock_guard<std::mutex> g(m_cacheMutex);
        auto it = m_cacheJobs.find(key);
        if (it != m_cacheJobs.end()) job = it->second;
        else { job = std::make_shared<CacheJob>(); m_cacheJobs.emplace(key, job); leader = true; }
    }

    if (!leader) {
        std::unique_lock<std::mutex> lk(m_cacheMutex);
        while (!job->done) { abort.check(); job->cv.wait_for(lk, std::chrono::milliseconds(100)); }
        if (!job->success || !cacheValid()) return false;
        cachePath = path; track = item.track; track.path = path; track.dsdRate = rate; track.channels = channels;
        track.bitsPerSample = 1; track.dsdSamplesPerChannel = samples; track.fileSize = fileSize;
        track.duration = rate ? static_cast<double>(samples) / static_cast<double>(rate) : track.duration;
        return true;
    }

    bool success = false;
    try {
        const auto tmp = path + L".partial";
        const auto tmpManifest = manifest + L".partial";
        DeleteFileW(tmp.c_str()); DeleteFileW(tmpManifest.c_str());
        if (reportProgress) setConversionStatus(true, 0);
        const auto decoded = SacdDecoder::decodeToDsf(item.sourcePath.c_str(), item.subsong, tmp, job->aborter ? *job->aborter : abort,
            [this, reportProgress](uint32_t pct) { if (reportProgress) setConversionStatus(true, pct); });
        abort.check();
        uint32_t verifyRate=0, verifyCh=0; uint64_t verifySamples=0, verifySize=0;
        if (!readDsfHeader(tmp, verifyRate, verifyCh, verifySamples, verifySize) || verifyCh != 2 || verifyRate == 0 || verifySize <= 84)
            throw std::runtime_error("generated DSF failed structural validation");
        if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("unable to finalize DSF cache");
        std::ofstream mf(tmpManifest, std::ios::binary | std::ios::trunc);
        if (!mf) throw std::runtime_error("unable to create cache manifest");
        pfc::string8 sacdVersion; sacd_plugin_installed(&sacdVersion);
        mf << "{\"cacheVersion\":" << kCacheFormatVersion
           << ",\"subsong\":" << item.subsong
           << ",\"sourceSize\":" << item.sourceSize
           << ",\"sourceWriteTime\":" << item.sourceWriteTime
           << ",\"decoderVersion\":\"" << xmlAttr(sacdVersion.c_str()) << "\""
           << ",\"dsdRate\":" << decoded.dsdRate
           << ",\"samplesPerChannel\":" << decoded.dsdSamplesPerChannel << "}\n";
        mf.close();
        if (!MoveFileExW(tmpManifest.c_str(), manifest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("unable to finalize cache manifest");
        if (reportProgress) setConversionStatus(false, 100); success = true;
        track = item.track; track.path = path; track.dsdRate = decoded.dsdRate; track.dsdSamplesPerChannel = decoded.dsdSamplesPerChannel;
        track.channels = 2; track.bitsPerSample = 1; track.fileSize = decoded.fileSize; track.duration = decoded.duration > 0 ? decoded.duration : track.duration;
    } catch (const exception_aborted&) {
        if (reportProgress) setConversionStatus(false, 0); DeleteFileW((path + L".partial").c_str()); DeleteFileW((manifest + L".partial").c_str());
        networkLog("SACD cache conversion aborted: " + item.sourcePath);
    } catch (std::exception const& e) {
        if (reportProgress) setConversionStatus(false, 0); DeleteFileW((path + L".partial").c_str()); DeleteFileW((manifest + L".partial").c_str());
        setLastError(std::string("cache generation failed: ") + e.what());
    }
    {
        std::lock_guard<std::mutex> g(m_cacheMutex); job->success = success; job->done = true; m_cacheJobs.erase(key);
    }
    job->cv.notify_all();
    if (!success) return false;
    cachePath = path; return true;
}

void SacdDlnaServer::clearPrefetchThreads() {
    std::lock_guard<std::mutex> g(m_prefetchMutex);
    for (auto& a : m_prefetchAborters) if (a) a->set();
}

void SacdDlnaServer::prefetchNextTrack(uint32_t itemId) {
    if (!m_running.load()) return;
    Item current;
    uint32_t nextId = 0;
    {
        std::lock_guard<std::mutex> g(m_mutex);
        for (const auto& x : m_items) {
            if (x.id == itemId) { current = x; break; }
        }
        if (!current.id || !current.albumId) return;
        for (const auto& album : m_albums) {
            if (album.id != current.albumId) continue;
            for (size_t i = 0; i + 1 < album.itemIds.size(); ++i) {
                if (album.itemIds[i] == itemId) { nextId = album.itemIds[i + 1]; break; }
            }
            break;
        }
    }
    if (!nextId) return;
    Item next;
    {
        std::lock_guard<std::mutex> g(m_mutex);
        for (const auto& x : m_items) if (x.id == nextId) { next = x; break; }
    }
    if (!next.id) return;

    const bool needsCache = static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled) || _stricmp(next.sourceExt.c_str(), ".iso") == 0;
    if (!needsCache) {
        std::lock_guard<std::mutex> g(m_prefetchMutex);
        m_prefetchTitle = next.track.title;
        m_prefetchState = "NATIVE DSD / NO CACHE NEEDED";
        m_prefetchActive = false;
        return;
    }
    const std::string key = (static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled) ? "dsp-" : "native-") + cacheKeyFor(next.sourcePath, next.subsong);
    {
        std::lock_guard<std::mutex> g(m_prefetchMutex);
        if (!m_prefetchKeys.insert(key).second) return;
        m_prefetchTitle = next.track.title;
        m_prefetchState = "PREPARING NEXT TRACK";
        m_prefetchActive = true;
        auto aborter = std::make_shared<abort_callback_impl>();
        m_prefetchAborters.push_back(aborter);
        m_prefetchThreads.emplace_back([this, next, aborter, key] {
            bool ok = false;
            try {
                std::wstring path; DsdTrack track = next.track;
                if (static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled)) ok = ensureProcessedDsf(next, path, track, *aborter, false);
                else if (_stricmp(next.sourceExt.c_str(), ".iso") == 0) ok = ensureCached(next, path, track, *aborter, false);
                else ok = true;
                networkLog(std::string("next-track prefetch ") + (ok ? "ready: " : "failed: ") + next.track.title);
            } catch (const std::exception& e) {
                networkLog(std::string("next-track prefetch aborted/failed: ") + next.track.title + " / " + e.what());
            } catch (...) {
                networkLog(std::string("next-track prefetch failed: ") + next.track.title);
            }
            {
                std::lock_guard<std::mutex> g(m_prefetchMutex);
                m_prefetchKeys.erase(key);
                m_prefetchActive = false;
                m_prefetchState = ok ? "READY" : "NOT READY";
            }
        });
    }
}

bool SacdDlnaServer::serveMedia(SOCKET s, uint32_t itemId, const std::string& requestLine,
                                const std::string& requestHeaders, const std::string& peerIp,
                                abort_callback_impl& aborter) {
    Item item;
    bool found = false;
    {
        std::lock_guard<std::mutex> g(m_mutex);
        for (const auto& x : m_items) if (x.id == itemId) { item = x; found = true; break; }
    }
    if (!found || !item.handle.is_valid()) return false;

    std::wstring path;
    std::string ext;
    DsdTrack track = item.track;
    const bool dspEnabledForItem = static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled);
    const bool isoItem = _stricmp(item.sourceExt.c_str(), ".iso") == 0;
    if (dspEnabledForItem || isoItem) {
        const std::string prepPipeline = dspEnabledForItem
            ? "PCM/DSD source -> DSD Processor -> DSF/DSD -> DLNA"
            : "SACD ISO -> foo_input_sacd -> DSF/DSD -> DLNA";
        const std::string prepConversion = dspEnabledForItem ? "DSP CONVERTING" : "SACD DECODE / CACHE";
        setPreparingTrack(item, "DSF", prepPipeline, prepConversion);
    } else {
        setPreparingTrack(item, item.sourceExt.empty() ? "UNKNOWN" : item.sourceExt.substr(1),
                          "Native DSD -> DLNA (no conversion)", "NO CONVERSION");
    }
    if (static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled)) {
        if (!ensureProcessedDsf(item, path, track, aborter)) return false;
        ext = ".dsf";
    } else if (_stricmp(item.sourceExt.c_str(), ".iso") == 0) {
        if (!ensureCached(item, path, track, aborter)) return false;
        ext = ".dsf";
    } else {
        path = utf8ToWide(item.sourcePath.c_str());
        ext = item.sourceExt;
    }

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const uint64_t size = static_cast<uint64_t>(f.tellg());
    if (track.fileSize == 0) track.fileSize = size;
    if (track.dsdRate == 0 && _stricmp(ext.c_str(), ".dsf") == 0) {
        uint32_t channels = 0; uint64_t samples = 0; uint64_t fileSize = 0;
        readDsfHeader(path, track.dsdRate, channels, samples, fileSize);
        track.channels = channels;
        track.dsdSamplesPerChannel = samples;
        if (track.duration <= 0 && track.dsdRate) track.duration = static_cast<double>(samples) / static_cast<double>(track.dsdRate);
    }

    uint64_t begin = 0, end = size ? size - 1 : 0; bool partial = false;
    if (!parseRange(requestHeaders, size, begin, end, partial)) {
        const std::string hdr = "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" + std::to_string(size) + "\r\nConnection: close\r\n\r\n";
        sendAll(s, hdr.data(), hdr.size(), &aborter);
        return true;
    }
    const uint64_t length = size ? end - begin + 1 : 0;
    f.seekg(static_cast<std::streamoff>(begin));

    const std::string mime = chooseRendererMime(mimeForExtension(ext));
    std::string hdr = partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
    hdr += "Content-Type: " + mime + "\r\nContent-Length: " + std::to_string(length) + "\r\nAccept-Ranges: bytes\r\n";
    hdr += "transferMode.dlna.org: Streaming\r\ncontentFeatures.dlna.org: DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000\r\n";
    hdr += "Cache-Control: no-cache\r\n";
    if (partial) hdr += "Content-Range: bytes " + std::to_string(begin) + "-" + std::to_string(end) + "/" + std::to_string(size) + "\r\n";
    hdr += "Connection: close\r\n\r\n";
    sendAll(s, hdr.data(), hdr.size(), &aborter);

    if (requestLine.rfind("HEAD ", 0) == 0) return true;

    const int sndbuf = sacd_dlna_cfg::stability_mode && track.dsdRate
        ? static_cast<int>(std::clamp<uint64_t>(static_cast<uint64_t>(track.dsdRate) / 4ULL * std::clamp<uint32_t>(sacd_dlna_cfg::prebuffer_seconds.get(), 5, 60), 512ull * 1024ull, 32ull * 1024ull * 1024ull))
        : 4 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sndbuf), sizeof(sndbuf));

    {
        std::lock_guard<std::mutex> g(m_rateMutex);
        if (m_activeStreams >= kMaxConcurrentStreams) {
            const std::string hdr503 = "HTTP/1.1 503 Service Unavailable\r\nRetry-After: 1\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            sendAll(s, hdr503.data(), hdr503.size(), &aborter);
            networkLog("stream rejected: maximum concurrent streams reached");
            return true;
        }
    }
    std::string sourceFormat = item.sourceExt.empty() ? "UNKNOWN" : item.sourceExt.substr(1);
    std::transform(sourceFormat.begin(), sourceFormat.end(), sourceFormat.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    std::string outputFormat = ext.empty() ? "UNKNOWN" : ext.substr(1);
    std::transform(outputFormat.begin(), outputFormat.end(), outputFormat.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const bool dspEnabled = static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled);
    const bool needsGeneratedOutput = dspEnabled || _stricmp(item.sourceExt.c_str(), ".iso") == 0;
    const std::string pipeline = dspEnabled
        ? "PCM/DSD source -> DSD Processor -> DSF/DSD -> DLNA"
        : (_stricmp(item.sourceExt.c_str(), ".iso") == 0
            ? "SACD ISO -> foo_input_sacd -> DSF/DSD -> DLNA"
            : "Native DSD -> DLNA (no conversion)");
    const std::string conversion = dspEnabled
        ? "DSP OUTPUT / CACHED"
        : (_stricmp(item.sourceExt.c_str(), ".iso") == 0 ? "SACD DECODE / CACHE" : "NO CONVERSION");
    const uint64_t outputSize = fileSizeSafe(path);
    updateStreamStart(peerIp, track, sourceFormat, outputFormat, pipeline, conversion, item.sourceSize, outputSize,
                      item.sourceSampleRate, item.sourceChannels, item.sourceBitsPerSample);
    prefetchNextTrack(itemId);
    {
        std::lock_guard<std::mutex> g(m_rateMutex);
        if (sacd_dlna_cfg::stability_mode && track.dsdRate) {
            const uint32_t prebufferSeconds = std::clamp<uint32_t>(sacd_dlna_cfg::prebuffer_seconds.get(), 5, 60);
            const uint64_t requestedBytes = static_cast<uint64_t>(track.dsdRate) / 4ULL * prebufferSeconds;
            m_prebufferTargetBytes = length < requestedBytes ? length : requestedBytes;
            m_prebufferBytes = m_prebufferTargetBytes < 256ull * 1024ull * 1024ull ? m_prebufferTargetBytes : 256ull * 1024ull * 1024ull;
        } else {
            m_prebufferTargetBytes = m_prebufferBytes = 0;
        }
    }

    bool ok = false;
    try {
        char buf[128 * 1024];
        uint64_t remaining = length;
        while (remaining) {
            aborter.check();
            const uint64_t maxRead = static_cast<uint64_t>(sizeof(buf));
            const size_t want = static_cast<size_t>(remaining < maxRead ? remaining : maxRead);
            f.read(buf, static_cast<std::streamsize>(want));
            const auto n = f.gcount();
            if (n <= 0) break;
            sendAll(s, buf, static_cast<size_t>(n), &aborter);
            updateStreamBytes(static_cast<uint64_t>(n));
            remaining -= static_cast<uint64_t>(n);
            {
                std::lock_guard<std::mutex> g(m_rateMutex);
                if (m_prebufferBytes > static_cast<uint64_t>(n)) m_prebufferBytes -= static_cast<uint64_t>(n);
                else m_prebufferBytes = 0;
            }
        }
        ok = remaining == 0;
    } catch (const exception_aborted&) {
        networkLog("stream aborted for " + peerIp + " / " + track.title);
    } catch (std::exception const& e) {
        setLastError(std::string("stream failed: ") + e.what());
    }
    updateStreamEnd();
    return ok;
}

bool SacdDlnaServer::serveAlbumArt(SOCKET s, uint32_t albumId, const std::string& requestLine, abort_callback_impl& aborter) {
    const bool isHead = requestLine.rfind("HEAD ", 0) == 0;
    {
        std::lock_guard<std::mutex> g(m_artMutex);
        auto it = m_artCache.find(albumId);
        if (it != m_artCache.end()) {
            const auto& a = it->second;
            const std::string hdr = "HTTP/1.1 200 OK\r\nContent-Type: " + a.mime + "\r\nContent-Length: " + std::to_string(a.bytes.size()) + "\r\nCache-Control: max-age=86400\r\nConnection: close\r\n\r\n";
            sendAll(s, hdr.data(), hdr.size(), &aborter);
            if (!isHead && !a.bytes.empty()) sendAll(s, reinterpret_cast<const char*>(a.bytes.data()), a.bytes.size(), &aborter);
            return true;
        }
    }

    metadb_handle_ptr handle;
    {
        std::lock_guard<std::mutex> g(m_mutex);
        for (const auto& album : m_albums) if (album.id == albumId) {
            for (const auto& item : m_items) if (item.id == album.representativeItemId) { handle = item.handle; break; }
            break;
        }
    }
    if (!handle.is_valid()) return false;

    uint64_t sourceSize = 0; int64_t sourceTime = 0;
    {
        std::error_code ec; const std::wstring ws = utf8ToWide(handle->get_path());
        const fs::path filePath(ws);
        const auto sz = fs::file_size(filePath, ec);
        sourceSize = ec ? 0 : static_cast<uint64_t>(sz);
        std::error_code ec2; const auto wt = fs::last_write_time(filePath, ec2); sourceTime = ec2 ? 0 : static_cast<int64_t>(wt.time_since_epoch().count());
    }
    const std::string keyMaterial = std::string(handle->get_path()) + "#" + std::to_string(sourceSize) + "#" + std::to_string(sourceTime) + "#art";
    char keyHex[32]{}; snprintf(keyHex, sizeof(keyHex), "%016llX", static_cast<unsigned long long>(fnv1a64(keyMaterial)));
    const auto folder = cacheFolder();
    const auto stem = folder + L"\\art-" + std::wstring(pfc::stringcvt::string_wide_from_utf8(keyHex));

    try {
        abort_callback_dummy abort;
        const auto tryDisk = [&](const char* ext, const char* mime) -> bool {
            const std::wstring path = stem + std::wstring(pfc::stringcvt::string_wide_from_utf8(ext));
            if (!isRegularFile(path) || fileSizeSafe(path) == 0) return false;
            std::ifstream f(path, std::ios::binary | std::ios::ate); const auto n = static_cast<size_t>(f.tellg()); f.seekg(0);
            ArtCache cache; cache.mime = mime; cache.path = path; cache.bytes.resize(n); f.read(reinterpret_cast<char*>(cache.bytes.data()), static_cast<std::streamsize>(n));
            { std::lock_guard<std::mutex> g(m_artMutex); m_artCache[albumId] = cache; }
            const std::string hdr = "HTTP/1.1 200 OK\r\nContent-Type: " + cache.mime + "\r\nContent-Length: " + std::to_string(cache.bytes.size()) + "\r\nCache-Control: max-age=86400\r\nConnection: close\r\n\r\n";
            sendAll(s, hdr.data(), hdr.size(), &aborter); if (!isHead) sendAll(s, reinterpret_cast<const char*>(cache.bytes.data()), cache.bytes.size(), &aborter); return true;
        };
        if (tryDisk(".jpg", "image/jpeg") || tryDisk(".png", "image/png") || tryDisk(".webp", "image/webp") || tryDisk(".gif", "image/gif") || tryDisk(".bmp", "image/bmp") || tryDisk(".tiff", "image/tiff")) return true;

        metadb_handle_list group; group += handle; pfc::list_t<GUID> ids; ids.add_item(album_art_ids::cover_front);
        auto extractor = album_art_manager_v2::get()->open(group, ids, abort);
        auto art = extractor->query(album_art_ids::cover_front, abort);
        if (!art.is_valid() || !art->data() || !art->size()) return false;
        const std::string mime = detectImageMime(art->data(), art->size());
        const std::string ext = cacheFileExtForMime(mime);
        const auto diskPath = stem + utf8ToWide(ext.c_str());
        {
            std::ofstream f(diskPath, std::ios::binary | std::ios::trunc);
            if (f) f.write(reinterpret_cast<const char*>(art->data()), static_cast<std::streamsize>(art->size()));
        }
        ArtCache cache; cache.mime = mime; cache.path = diskPath; cache.bytes.resize(art->size()); memcpy(cache.bytes.data(), art->data(), art->size());
        { std::lock_guard<std::mutex> g(m_artMutex); m_artCache[albumId] = cache; }
        const std::string hdr = "HTTP/1.1 200 OK\r\nContent-Type: " + cache.mime + "\r\nContent-Length: " + std::to_string(cache.bytes.size()) + "\r\nCache-Control: max-age=86400\r\nConnection: close\r\n\r\n";
        sendAll(s, hdr.data(), hdr.size(), &aborter); if (!isHead) sendAll(s, reinterpret_cast<const char*>(cache.bytes.data()), cache.bytes.size(), &aborter); return true;
    } catch (...) { return false; }
}

void SacdDlnaServer::clientThread(SOCKET s, std::shared_ptr<abort_callback_impl> aborter) {
    try { handleClient(s, *aborter); } catch (...) {}
    shutdown(s, SD_BOTH);
    closesocket(s);
    closeClientState(s);
}

void SacdDlnaServer::closeClientState(SOCKET s) {
    std::lock_guard<std::mutex> g(m_clientMutex);
    m_clients.erase(std::remove_if(m_clients.begin(), m_clients.end(), [s](const ClientState& c) { return c.socket == s; }), m_clients.end());
}

void SacdDlnaServer::handleClient(SOCKET s, abort_callback_impl& aborter) {
    std::string peerIp = "unknown";
    sockaddr_in peer{}; int peerLen = sizeof(peer);
    if (getpeername(s, reinterpret_cast<sockaddr*>(&peer), &peerLen) == 0) {
        char ip[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip)); peerIp = ip;
    }

    std::string req;
    if (!recvHttpRequest(s, req)) return;
    const auto eol = req.find("\r\n"); if (eol == std::string::npos) return;
    const std::string line = req.substr(0, eol);
    {
        std::lock_guard<std::mutex> g(m_diagMutex);
        ++m_httpRequests;
        if (!peerIp.empty() && peerIp != m_localIp && peerIp != "127.0.0.1") {
            m_networkPresence = true;
            m_remoteHttpSeen = true;
            ++m_remoteHttpRequests;
            m_lastRemotePeer = peerIp;
            m_ssdpLastPeer = peerIp;
            m_networkVisibility = "CONFIRMED / REMOTE HTTP";
        }
    }
    updateLastHttpRequest(peerIp + " -> " + line);
    networkLog(peerIp + " -> " + line);

    std::string userAgent;
    getHeaderValue(req, "User-Agent", userAgent);
    const std::string uaLower = lowerCopy(userAgent);
    if (uaLower.find("t+a") != std::string::npos || uaLower.find("sdx") != std::string::npos) {
        std::lock_guard<std::mutex> g(m_rateMutex);
        m_clientName = userAgent.empty() ? "T+A renderer" : userAgent;
        m_clientModel = uaLower.find("sdx") != std::string::npos ? "T+A SDX renderer" : "T+A renderer";
        m_clientIp = peerIp;
        m_sdxIp = peerIp;
        m_sdxName = m_clientModel;
    }

    auto sendXml = [&](const std::string& body) {
        const std::string hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/xml; charset=utf-8\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
        sendAll(s, hdr.data(), hdr.size(), &aborter);
        sendAll(s, body.data(), body.size(), &aborter);
    };

    if (line.rfind("GET /device.xml", 0) == 0) { sendXml(makeDeviceXml()); return; }
    if (line.rfind("GET /ContentDirectory.xml", 0) == 0) { sendXml(makeContentDirectoryScpd()); return; }
    if (line.rfind("GET /ConnectionManager.xml", 0) == 0) { sendXml(makeConnectionManagerScpd()); return; }
    if (line.rfind("GET /status", 0) == 0) {
        const auto st = get_status();
        auto mbps = [](uint64_t bps) { return static_cast<double>(bps) * 8.0 / 1000000.0; };
        std::string body = "<!doctype html><html><head><meta charset=\"utf-8\"><title>SACD DLNA</title></head><body>";
        body += "<h1>foo_sacd_dlna</h1><p>DLNA discovery: <b>" + std::string(st.broadcasting ? "BROADCASTING / ACTIVE" : "STOPPED") + "</b></p>";
        body += "<p>Audio stream: <b>" + std::string(st.streamingActive ? "ACTIVE / TRANSMITTING" : "IDLE") + "</b></p>";
        body += "<p>TX speed: <b>" + std::to_string(mbps(st.bytesPerSecond)) + " Mbit/s</b></p>";
        body += "<p>Stream elapsed: <b>" + std::to_string(st.streamElapsedMs / 1000) + " s</b></p>";
        body += "<p>DSD: " + (st.dsdRate ? std::to_string(st.dsdRate / 2822400) + "x (" + std::to_string(st.dsdRate) + " Hz)" : "-") + "</p>";
        body += "<p>T+A SDX: <b>" + std::string(st.sdxDetected ? (st.sdxStreaming ? "DETECTED / STREAMING" : "DETECTED / IDLE") : "NOT DETECTED") + "</b>";
        if (!st.sdxIp.is_empty()) body += " — " + std::string(st.sdxIp.c_str());
        if (!st.sdxName.is_empty()) body += " — " + std::string(st.sdxName.c_str());
        if (!st.sdxModelNumber.is_empty()) body += " — modelNumber " + std::string(st.sdxModelNumber.c_str());
        body += "</p><p>Negotiated Sink: <code>" + xmlEscape(st.sdxProtocolInfo.c_str()) + "</code></p>";
        body += "<p>foo_input_sacd: " + std::string(st.sacdInstalled ? "INSTALLED" : "NOT INSTALLED") + " " + st.sacdVersion.c_str() + "</p>";
        body += "<p>Music Library: " + std::string(st.sharingLibrary ? "SHARING" : "NOT SHARING") + " (" + std::to_string(st.sharedCount) + " DSD tracks)</p>";
        body += "<p>Cache: " + std::to_string(st.cacheHits) + " hits / " + std::to_string(st.cacheMisses) + " misses / " + std::to_string(st.cacheBytes / 1048576.0) + " MB</p>";
        body += "<p>HTTP ready: <b>" + std::string(st.httpReady ? "YES" : "NO") + "</b> | SSDP ready: <b>" + std::string(st.ssdpReady ? "YES" : "NO") + "</b></p>";
        body += "<p>Network visibility: <b>" + xmlEscape(st.networkVisibility.c_str()) + "</b> | remote HTTP=" + std::to_string(st.remoteHttpRequests) + " | remote SSDP M-SEARCH=" + std::to_string(st.remoteSsdpSearches) + " | last remote peer=" + xmlEscape(st.lastRemotePeer.c_str()) + "</p>";
        body += "<p>Network self-test: HTTP=" + std::string(st.httpSelfTestOk ? "OK" : "-") + " | SSDP=" + std::string(st.ssdpProbeOk ? "OK" : "-") + " | NOTIFY loopback=" + std::string(st.ssdpNotifyLoopbackOk ? "OK" : "-") + "</p>";
        body += "<p>SSDP counters: NOTIFY=" + std::to_string(st.ssdpAliveSent) + " M-SEARCH-rx=" + std::to_string(st.ssdpMSearchReceived) + " responses=" + std::to_string(st.ssdpResponsesSent) + " renderer-responses=" + std::to_string(st.ssdpDiscoverResponses) + "</p>";
        body += "<p>Network diagnostic: <code>" + xmlEscape(st.networkDiagnostic.c_str()) + "</code></p>";
        body += "<p>Last HTTP: <code>" + xmlEscape(st.lastHttpRequest.c_str()) + "</code></p>";
        body += "<p>UpdateID: " + std::to_string(st.updateId) + "</p></body></html>";
        const auto hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
        sendAll(s, hdr.data(), hdr.size(), &aborter); sendAll(s, body.data(), body.size(), &aborter); return;
    }

    if (line.rfind("POST /ctl/ContentDirectory", 0) == 0) {
        const size_t bodyStart = req.find("\r\n\r\n");
        const std::string soap = bodyStart == std::string::npos ? std::string{} : req.substr(bodyStart + 4);
        const auto action = headerValueCI(req, "SOAPACTION");
        if (lowerCopy(action).find("getsystemupdateid") != std::string::npos || lowerCopy(soap).find("<u:getsystemupdateid") != std::string::npos) {
            sendXml(systemUpdateIdResponse()); return;
        }
        const std::string objectId = urlPathDecode(extractSoapArg(soap, "ObjectID", kRootObject));
        const std::string browseFlag = extractSoapArg(soap, "BrowseFlag", "BrowseDirectChildren");
        const unsigned start = extractSoapUint(soap, "StartingIndex", 0);
        const unsigned count = extractSoapUint(soap, "RequestedCount", 0);
        const std::string filter = extractSoapArg(soap, "Filter", "*");
        const std::string sort = extractSoapArg(soap, "SortCriteria", "");
        networkLog("Browse object=" + objectId + " flag=" + browseFlag + " start=" + std::to_string(start) + " count=" + std::to_string(count));
        sendXml(browseResponse(objectId, browseFlag, start, count, filter, sort)); return;
    }

    if (line.rfind("POST /ctl/ConnectionManager", 0) == 0) {
        const std::string body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><u:GetProtocolInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\"><Source>http-get:*:audio/dsf:DLNA.ORG_OP=01;DLNA.ORG_CI=0,http-get:*:audio/x-dsf:DLNA.ORG_OP=01;DLNA.ORG_CI=0,http-get:*:audio/dff:DLNA.ORG_OP=01;DLNA.ORG_CI=0,http-get:*:audio/x-dff:DLNA.ORG_OP=01;DLNA.ORG_CI=0</Source><Sink></Sink></u:GetProtocolInfoResponse></s:Body></s:Envelope>";
        sendXml(body); return;
    }

    const bool isHead = line.rfind("HEAD ", 0) == 0;
    const size_t mediaPos = line.find("/media/");
    if (mediaPos != std::string::npos && (line.rfind("GET ", 0) == 0 || isHead)) {
        const size_t pathStart = mediaPos + 7;
        const size_t pathEnd = line.find('.', pathStart);
        if (pathEnd != std::string::npos) {
            try { const uint32_t id = std::stoul(line.substr(pathStart, pathEnd - pathStart)); if (serveMedia(s, id, line, req, peerIp, aborter)) return; } catch (...) {}
        }
    }

    const size_t artPos = line.find("/art/");
    if (artPos != std::string::npos && (line.rfind("GET ", 0) == 0 || isHead)) {
        const size_t pathStart = artPos + 5;
        size_t pathEnd = line.find(' ', pathStart);
        if (pathEnd == std::string::npos) pathEnd = line.size();
        const size_t dot = line.find('.', pathStart);
        if (dot != std::string::npos && dot < pathEnd) pathEnd = dot;
        try { const uint32_t id = std::stoul(line.substr(pathStart, pathEnd - pathStart)); if (serveAlbumArt(s, id, line, aborter)) return; } catch (...) {}
    }

    const char* body = "Not Found";
    const std::string hdr = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\n";
    sendAll(s, hdr.data(), hdr.size(), &aborter); sendAll(s, body, 9, &aborter);
}

void SacdDlnaServer::httpLoop() {
    m_httpListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_httpListen == INVALID_SOCKET) { m_running = false; return; }
    BOOL reuse = TRUE; setsockopt(m_httpListen, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons(m_port);
    if (bind(m_httpListen, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR || listen(m_httpListen, 16) == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        closesocket(m_httpListen); m_httpListen = INVALID_SOCKET; m_running = false;
        { std::lock_guard<std::mutex> g(m_diagMutex); m_httpReady = false; m_networkDiagnostic = "HTTP bind/listen failed: " + std::to_string(err); }
        console::print("SACD DLNA: HTTP bind/listen failed"); return;
    }
    { std::lock_guard<std::mutex> g(m_diagMutex); m_httpReady = true; m_networkDiagnostic = "HTTP ready on TCP " + std::to_string(m_port); }
    while (m_running) {
        sockaddr_in client{}; int len = sizeof(client);
        const SOCKET clientSocket = accept(m_httpListen, reinterpret_cast<sockaddr*>(&client), &len);
        if (clientSocket == INVALID_SOCKET) { if (m_running) continue; break; }
        auto aborter = std::make_shared<abort_callback_impl>();
        {
            std::lock_guard<std::mutex> g(m_clientMutex);
            m_clients.push_back({ clientSocket, aborter });
            m_clientThreads.emplace_back([this, clientSocket, aborter] { clientThread(clientSocket, aborter); });
        }
    }
}

void SacdDlnaServer::ssdpLoop() {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        std::lock_guard<std::mutex> g(m_diagMutex);
        m_ssdpReady = false;
        m_networkDiagnostic = "SSDP socket creation failed: " + std::to_string(WSAGetLastError());
        return;
    }
    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in local{}; local.sin_family = AF_INET; local.sin_addr.s_addr = htonl(INADDR_ANY); local.sin_port = htons(1900);
    if (bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        closesocket(s);
        std::lock_guard<std::mutex> g(m_diagMutex);
        m_ssdpReady = false;
        m_networkDiagnostic = "SSDP bind UDP 1900 failed: " + std::to_string(err);
        return;
    }
    BOOL loopback = TRUE;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&loopback), sizeof(loopback));
    const std::string lanIp = localAddress();
    if (!lanIp.empty()) {
        in_addr iface{};
        if (InetPtonA(AF_INET, lanIp.c_str(), &iface) == 1)
            setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&iface), sizeof(iface));
    }
    ip_mreq mreq{};
    InetPtonA(AF_INET, "239.255.255.250", &mreq.imr_multiaddr);
    InetPtonA(AF_INET, lanIp.c_str(), &mreq.imr_interface);
    const int joinRc = setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof(mreq));
    sockaddr_in multicast{}; multicast.sin_family = AF_INET; InetPtonA(AF_INET, "239.255.255.250", &multicast.sin_addr); multicast.sin_port = htons(1900);
    const std::string location = "http://" + lanIp + ":" + std::to_string(m_port) + "/device.xml";
    const std::string uuidUsn = std::string(kUuid) + "::upnp:rootdevice";
    const std::string deviceUsn = std::string(kUuid) + "::urn:schemas-upnp-org:device:MediaServer:1";
    {
        std::lock_guard<std::mutex> g(m_diagMutex);
        m_ssdpReady = joinRc == 0;
        m_localIp = lanIp;
        m_networkDiagnostic = joinRc == 0 ? "SSDP multicast ready on 239.255.255.250:1900" : "SSDP multicast join failed: " + std::to_string(WSAGetLastError());
        if (joinRc == 0 && m_networkVisibility.empty()) m_networkVisibility = "LOCAL SSDP READY / WAITING FOR REMOTE PEER";
    }
    if (joinRc != 0) networkLog("SSDP multicast join failed; NOTIFY may still be sent");

    auto sendMulticast = [&](const std::string& msg) {
        const int rc = sendto(s, msg.data(), static_cast<int>(msg.size()), 0, reinterpret_cast<sockaddr*>(&multicast), sizeof(multicast));
        return rc == static_cast<int>(msg.size());
    };
    auto notifyAlive = [&]() {
        const char* nts[] = { "upnp:rootdevice", kUuid, "urn:schemas-upnp-org:device:MediaServer:1" };
        const char* usns[] = { uuidUsn.c_str(), kUuid, deviceUsn.c_str() };
        for (int i = 0; i < 3; ++i) {
            const std::string msg = "NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nCACHE-CONTROL: max-age=1800\r\nLOCATION: " + location +
                "\r\nNT: " + nts[i] + "\r\nNTS: ssdp:alive\r\nSERVER: Windows/10 UPnP/1.1 foo_sacd_dlna/0.8-alpha3-j\r\nUSN: " + usns[i] + "\r\n\r\n";
            if (sendMulticast(msg)) { std::lock_guard<std::mutex> g(m_diagMutex); ++m_ssdpAliveSent; }
        }
        networkLog("SSDP NOTIFY ssdp:alive sent to 239.255.255.250:1900");
    };
    auto notifyByeBye = [&]() {
        const char* nts[] = { "upnp:rootdevice", kUuid, "urn:schemas-upnp-org:device:MediaServer:1" };
        const char* usns[] = { uuidUsn.c_str(), kUuid, deviceUsn.c_str() };
        for (int i = 0; i < 3; ++i) {
            const std::string msg = "NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nNT: " + std::string(nts[i]) +
                "\r\nNTS: ssdp:byebye\r\nUSN: " + std::string(usns[i]) + "\r\n\r\n";
            sendMulticast(msg);
        }
    };
    auto discover = [&]() {
        const std::string search = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n\r\n";
        if (sendMulticast(search)) { std::lock_guard<std::mutex> g(m_diagMutex); ++m_ssdpDiscoverSent; }
    };
    notifyAlive(); discover();
    auto nextAlive = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto nextDiscover = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (m_running) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextAlive) { notifyAlive(); nextAlive = now + std::chrono::seconds(30); }
        if (now >= nextDiscover) { discover(); nextDiscover = now + std::chrono::seconds(20); }
        fd_set set; FD_ZERO(&set); FD_SET(s, &set); timeval tv{1,0};
        if (select(0, &set, nullptr, nullptr, &tv) <= 0) continue;
        char buf[4096]{}; sockaddr_in from{}; int fromLen = sizeof(from);
        const int n = recvfrom(s, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) continue;
        buf[n] = 0; const std::string msg(buf);
        char peerIp[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET, &from.sin_addr, peerIp, sizeof(peerIp));
        const bool externalPeer = std::string(peerIp) != lanIp && std::string(peerIp) != "127.0.0.1";
        if (msg.rfind("HTTP/1.1 200", 0) == 0) {
            {
                std::lock_guard<std::mutex> g(m_diagMutex);
                ++m_ssdpDiscoverResponses;
                if (externalPeer) { m_ssdpLastPeer = peerIp; }
            }
            discoverSdxResponse(msg, peerIp); continue;
        }
        if (msg.find("M-SEARCH") == std::string::npos) continue;
        {
            std::lock_guard<std::mutex> g(m_diagMutex);
            ++m_ssdpMSearchReceived;
        }
        std::string st = trimCopy(headerValueCI(msg, "ST"));
        if (st.empty()) st = "ssdp:all";
        if (st != "ssdp:all" && st != "upnp:rootdevice" && st != kUuid && st != "urn:schemas-upnp-org:device:MediaServer:1") continue;
        if (externalPeer) {
            std::lock_guard<std::mutex> g(m_diagMutex);
            m_networkPresence = true;
            m_remoteSsdpSeen = true;
            ++m_remoteSsdpSearches;
            m_lastRemotePeer = peerIp;
            m_ssdpLastPeer = peerIp;
            m_networkVisibility = "CONFIRMED / REMOTE SSDP M-SEARCH";
        }
        const std::string usn = (st == "upnp:rootdevice") ? uuidUsn : (st == kUuid ? std::string(kUuid) : deviceUsn);
        const std::string response = "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=1800\r\nEXT:\r\nLOCATION: " + location +
            "\r\nSERVER: Windows/10 UPnP/1.1 foo_sacd_dlna/0.8-alpha3-j\r\nST: " + st + "\r\nUSN: " + usn + "\r\n\r\n";
        const int rc = sendto(s, response.data(), static_cast<int>(response.size()), 0, reinterpret_cast<sockaddr*>(&from), fromLen);
        if (rc == static_cast<int>(response.size())) { std::lock_guard<std::mutex> g(m_diagMutex); ++m_ssdpResponsesSent; }
    }
    notifyByeBye();
    {
        std::lock_guard<std::mutex> g(m_diagMutex);
        m_ssdpReady = false;
    }
    closesocket(s);
}

bool SacdDlnaServer::run_network_diagnostics() {
    if (!is_running()) {
        std::lock_guard<std::mutex> g(m_diagMutex);
        m_networkDiagnostic = "Server is stopped; enable SACD DLNA before probing";
        return false;
    }
    const std::string ip = localAddress();
    bool httpOk = true;
    const char* paths[] = { "/device.xml", "/ContentDirectory.xml", "/ConnectionManager.xml" };
    for (const char* path : paths) {
        std::string headers, body;
        const bool ok = httpRequestSimple("GET", "http://" + ip + ":" + std::to_string(m_port) + path, {}, headers, body);
        if (!ok || headers.rfind("HTTP/1.1 200", 0) != 0 || body.empty()) { httpOk = false; break; }
    }
    if (httpOk) {
        std::string headers, body;
        const std::string cd = soapEnvelope("urn:schemas-upnp-org:service:ContentDirectory:1", "GetSystemUpdateID", "");
        const std::string cm = soapEnvelope("urn:schemas-upnp-org:service:ConnectionManager:1", "GetProtocolInfo", "");
        if (!httpRequestSimple("POST", "http://" + ip + ":" + std::to_string(m_port) + "/ctl/ContentDirectory", cd, headers, body) ||
            headers.rfind("HTTP/1.1 200", 0) != 0 || body.find("GetSystemUpdateIDResponse") == std::string::npos) httpOk = false;
        if (!httpRequestSimple("POST", "http://" + ip + ":" + std::to_string(m_port) + "/ctl/ConnectionManager", cm, headers, body) ||
            headers.rfind("HTTP/1.1 200", 0) != 0 || body.find("GetProtocolInfoResponse") == std::string::npos) httpOk = false;
    }
    bool ssdpSelf = false;
    bool ssdpNotifyLoopback = false;
    uint32_t externalServers = 0;
    SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe != INVALID_SOCKET) {
        BOOL loop = TRUE; setsockopt(probe, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&loop), sizeof(loop));
        DWORD timeout = 1800; setsockopt(probe, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        sockaddr_in bindAddr{}; bindAddr.sin_family = AF_INET; bindAddr.sin_addr.s_addr = htonl(INADDR_ANY); bindAddr.sin_port = 0;
        if (bind(probe, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) == 0) {
            sockaddr_in multicast{}; multicast.sin_family = AF_INET; InetPtonA(AF_INET, "239.255.255.250", &multicast.sin_addr); multicast.sin_port = htons(1900);
            const std::string search = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: urn:schemas-upnp-org:device:MediaServer:1\r\n\r\n";
            if (sendto(probe, search.data(), static_cast<int>(search.size()), 0, reinterpret_cast<sockaddr*>(&multicast), sizeof(multicast)) == static_cast<int>(search.size())) {
                auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1600);
                while (std::chrono::steady_clock::now() < deadline) {
                    fd_set set; FD_ZERO(&set); FD_SET(probe, &set);
                    const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
                    timeval tv{ static_cast<long>(remain / 1000), static_cast<long>((remain % 1000) * 1000) };
                    if (select(0, &set, nullptr, nullptr, &tv) <= 0) break;
                    char buf[8192]{}; sockaddr_in from{}; int fromLen = sizeof(from);
                    const int n = recvfrom(probe, buf, sizeof(buf)-1, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
                    if (n <= 0) continue;
                    buf[n] = 0; std::string response(buf);
                    char peer[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET, &from.sin_addr, peer, sizeof(peer));
                    const std::string location = headerValueCI(response, "LOCATION");
                    const std::string usn = headerValueCI(response, "USN");
                    if (response.rfind("NOTIFY * HTTP/1.1", 0) == 0) {
                        const std::string nts = lowerCopy(headerValueCI(response, "NTS"));
                        if (nts == "ssdp:alive" && location.find(ip + ":" + std::to_string(m_port)) != std::string::npos) ssdpNotifyLoopback = true;
                        continue;
                    }
                    if (response.rfind("HTTP/1.1 200", 0) != 0) continue;
                    if (usn.find(kUuid) != std::string::npos || location.find(ip + ":" + std::to_string(m_port)) != std::string::npos) ssdpSelf = true;
                    else ++externalServers;
                }
            }
        }
        closesocket(probe);
    }
    {
        std::lock_guard<std::mutex> g(m_diagMutex);
        m_httpSelfTestOk = httpOk;
        m_ssdpProbeOk = ssdpSelf;
        m_ssdpNotifyLoopbackOk = ssdpNotifyLoopback;
        m_networkDiagnostic = std::string("HTTP UPnP validation ") + (httpOk ? "OK" : "FAILED") + "; SSDP self-probe " + (ssdpSelf ? "OK" : "NO RESPONSE") +
            "; NOTIFY loopback " + (ssdpNotifyLoopback ? "OK" : "NOT OBSERVED") +
            "; external MediaServers seen: " + std::to_string(externalServers) +
            "; remote visibility: " + (m_networkPresence ? "CONFIRMED" : "NOT CONFIRMED");
    }
    networkLog("network probe: HTTP-UPnP=" + std::string(httpOk ? "OK" : "FAILED") + ", SSDP-self=" + (ssdpSelf ? "OK" : "NO RESPONSE") + ", NOTIFY-loopback=" + (ssdpNotifyLoopback ? "OK" : "NOT OBSERVED") + ", external MediaServers=" + std::to_string(externalServers));
    return httpOk && ssdpSelf;
}

uint64_t SacdDlnaServer::persistent_cache_bytes() const {
    uint64_t total = 0;
    const auto folder = cacheFolder();
    std::error_code ec;
    if (!fs::exists(folder, ec)) return 0;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const auto ext = lowerCopy(entry.path().extension().string());
        if (ext == ".dsf" || ext == ".jpg" || ext == ".png" || ext == ".webp" || ext == ".gif" || ext == ".bmp" || ext == ".tif" || ext == ".tiff") {
            total += static_cast<uint64_t>(entry.file_size(ec));
        }
    }
    return total;
}

void SacdDlnaServer::clear_persistent_cache() {
    const auto folder = cacheFolder();
    std::error_code ec;
    if (!fs::exists(folder, ec)) return;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const auto ext = lowerCopy(entry.path().extension().string());
        if (ext == ".dsf" || ext == ".json" || ext == ".partial" || ext == ".jpg" || ext == ".png" || ext == ".webp" || ext == ".gif" || ext == ".bmp" || ext == ".tif" || ext == ".tiff") {
            fs::remove(entry.path(), ec);
        }
    }
    std::lock_guard<std::mutex> g(m_artMutex);
    m_artCache.clear();
}

void SacdDlnaServer::clear_shared_library() {
    std::lock_guard<std::mutex> g(m_mutex);
    m_items.clear(); m_artists.clear(); m_albums.clear(); m_sharedCount = 0; m_sharingLibrary = false; ++m_updateId;
    { std::lock_guard<std::mutex> a(m_artMutex); m_artCache.clear(); }
}

void SacdDlnaServer::publish(const metadb_handle_list& items) {
    std::vector<Item> newItems; newItems.reserve(items.get_count());
    for (size_t i = 0; i < items.get_count(); ++i) {
        const auto& handle = items[i]; const char* path = handle->get_path(); if (!path || !*path) continue;
        const char* ext = strrchr(path, '.'); if (!ext) continue;
        const bool isDsd = !_stricmp(ext, ".iso") || !_stricmp(ext, ".dsf") || !_stricmp(ext, ".dff");
        const bool isPcm = !_stricmp(ext, ".flac") || !_stricmp(ext, ".wav") || !_stricmp(ext, ".aif") || !_stricmp(ext, ".aiff") ||
            !_stricmp(ext, ".wv") || !_stricmp(ext, ".tta") || !_stricmp(ext, ".ape") || !_stricmp(ext, ".mp3") ||
            !_stricmp(ext, ".m4a") || !_stricmp(ext, ".mp4") || !_stricmp(ext, ".ogg") || !_stricmp(ext, ".opus") || !_stricmp(ext, ".aac");
        if (!isDsd && !(static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled) && isPcm)) continue;
        if (static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled) && !DsdProcessorBridge::installed()) continue;
        Item x; x.id = m_nextId++; x.sourcePath = path; x.sourceExt = ext; x.subsong = handle->get_subsong_index(); x.handle = handle;
        {
            std::error_code ec; const std::wstring ws = utf8ToWide(path);
            const fs::path filePath(ws);
            const auto sz = fs::file_size(filePath, ec);
            x.sourceSize = ec ? 0 : static_cast<uint64_t>(sz);
            std::error_code ec2; const auto wt = fs::last_write_time(filePath, ec2);
            x.sourceWriteTime = ec2 ? 0 : static_cast<int64_t>(wt.time_since_epoch().count());
        }
        try {
            abort_callback_dummy abort; service_ptr_t<input_info_reader> infoReader; input_entry::g_open_for_info_read(infoReader, nullptr, path, abort); file_info_impl info; infoReader->get_info(x.subsong, info, abort);
            auto meta = [&](const char* key) { const char* v = info.meta_get(key, 0); return v ? std::string(v) : std::string{}; };
            auto metaFirst = [&](std::initializer_list<const char*> names) { for (auto n : names) { auto v = meta(n); if (!v.empty()) return v; } return std::string{}; };
            x.track.title = meta("title"); x.track.artist = meta("artist"); x.track.album = meta("album"); x.track.albumArtist = metaFirst({"album artist", "albumartist"});
            x.track.genre = meta("genre"); x.track.date = metaFirst({"date", "year"}); x.track.composer = meta("composer"); x.track.publisher = metaFirst({"publisher", "label"}); x.track.comment = metaFirst({"comment", "comments"}); x.track.trackNumber = metaFirst({"tracknumber", "track number", "track"}); x.track.discNumber = metaFirst({"discnumber", "disc number", "disc"}); x.track.totalTracks = metaFirst({"totaltracks", "total tracks", "tracktotal"}); x.track.totalDiscs = metaFirst({"totaldiscs", "total discs"});
            if (x.track.title.empty()) x.track.title = "Track " + std::to_string(x.subsong + 1);
            if (x.track.artist.empty()) x.track.artist = "Unknown Artist"; if (x.track.album.empty()) x.track.album = "Unknown Album";
            x.track.duration = info.get_length();
            x.sourceSampleRate = static_cast<uint32_t>(std::max<t_int64>(0, info.info_get_int("samplerate")));
            x.sourceChannels = static_cast<uint32_t>(std::max<t_int64>(1, info.info_get_int("channels")));
            x.sourceBitsPerSample = static_cast<uint32_t>(std::max<t_int64>(0, info.info_get_int("bitspersample")));
            x.track.dsdRate = x.sourceSampleRate;
            x.track.channels = x.sourceChannels;
            x.track.bitsPerSample = x.sourceBitsPerSample ? x.sourceBitsPerSample : 1;
            if (x.track.dsdRate != 2822400 && x.track.dsdRate != 5644800 && x.track.dsdRate != 11289600) x.track.dsdRate = 0;
            if (x.sourceExt == ".dsf" || x.sourceExt == ".dff") {
                std::error_code ec; const std::wstring ws = utf8ToWide(x.sourcePath.c_str()); const fs::path filePath(ws); x.track.fileSize = fs::file_size(filePath, ec);
            }
            x.track.path = x.sourceExt == ".iso" ? std::wstring{} : utf8ToWide(x.sourcePath.c_str());
            newItems.push_back(std::move(x));
        } catch (std::exception const& e) { setLastError(std::string("unable to index ") + path + ": " + e.what()); }
    }

    std::vector<Artist> newArtists; std::vector<Album> newAlbums;
    auto findArtist = [&](const std::string& key) -> Artist* { for (auto& x : newArtists) if (x.key == key) return &x; return nullptr; };
    auto findAlbum = [&](uint32_t artistId, const std::string& key) -> Album* { for (auto& x : newAlbums) if (x.artistId == artistId && x.key == key) return &x; return nullptr; };
    for (auto& item : newItems) {
        const std::string artistKey = normalizeKey(item.track.artist); Artist* artist = findArtist(artistKey);
        if (!artist) { Artist a; a.id = m_nextArtistId++; a.name = item.track.artist; a.key = artistKey; newArtists.push_back(std::move(a)); artist = &newArtists.back(); }
        item.artistId = artist->id;
        const std::string albumKey = normalizeKey(item.track.album); Album* album = findAlbum(artist->id, albumKey);
        if (!album) { Album a; a.id = m_nextAlbumId++; a.artistId = artist->id; a.title = item.track.album; a.key = albumKey; a.representativeItemId = item.id; newAlbums.push_back(std::move(a)); album = &newAlbums.back(); artist->albumIds.push_back(album->id); }
        item.albumId = album->id; album->itemIds.push_back(item.id);
    }

    auto ciLess = [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; };
    auto numberPrefix = [](const std::string& s) -> unsigned {
        const char* p = s.c_str();
        while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
        if (!*p || !isdigit(static_cast<unsigned char>(*p))) return UINT_MAX;
        return static_cast<unsigned>(strtoul(p, nullptr, 10));
    };
    std::sort(newArtists.begin(), newArtists.end(), [&](const Artist& a, const Artist& b) { return ciLess(a.name, b.name); });
    std::sort(newAlbums.begin(), newAlbums.end(), [&](const Album& a, const Album& b) {
        const auto aa = std::find_if(newArtists.begin(), newArtists.end(), [&](const Artist& x) { return x.id == a.artistId; });
        const auto bb = std::find_if(newArtists.begin(), newArtists.end(), [&](const Artist& x) { return x.id == b.artistId; });
        const std::string an = aa != newArtists.end() ? aa->name : std::string{}, bn = bb != newArtists.end() ? bb->name : std::string{};
        if (_stricmp(an.c_str(), bn.c_str()) != 0) return ciLess(an, bn); return ciLess(a.title, b.title);
    });
    for (auto& album : newAlbums) std::sort(album.itemIds.begin(), album.itemIds.end(), [&](uint32_t a, uint32_t b) {
        const auto ia = std::find_if(newItems.begin(), newItems.end(), [&](const Item& x) { return x.id == a; });
        const auto ib = std::find_if(newItems.begin(), newItems.end(), [&](const Item& x) { return x.id == b; });
        if (ia == newItems.end() || ib == newItems.end()) return a < b;
        const unsigned da = numberPrefix(ia->track.discNumber), db = numberPrefix(ib->track.discNumber);
        if (da != db) return da < db;
        const unsigned ta = numberPrefix(ia->track.trackNumber), tb = numberPrefix(ib->track.trackNumber);
        if (ta != tb) return ta < tb;
        if (_stricmp(ia->track.title.c_str(), ib->track.title.c_str()) != 0) return ciLess(ia->track.title, ib->track.title);
        return ia->id < ib->id;
    });
    for (auto& artist : newArtists) std::sort(artist.albumIds.begin(), artist.albumIds.end(), [&](uint32_t a, uint32_t b) {
        const auto aa = std::find_if(newAlbums.begin(), newAlbums.end(), [&](const Album& x) { return x.id == a; });
        const auto bb = std::find_if(newAlbums.begin(), newAlbums.end(), [&](const Album& x) { return x.id == b; });
        return (aa == newAlbums.end() || bb == newAlbums.end()) ? a < b : ciLess(aa->title, bb->title);
    });

    {
        std::lock_guard<std::mutex> g(m_mutex);
        m_items = std::move(newItems); m_artists = std::move(newArtists); m_albums = std::move(newAlbums); m_sharedCount = m_items.size(); ++m_updateId;
    }
    { std::lock_guard<std::mutex> g(m_artMutex); m_artCache.clear(); }
    FB2K_console_formatter() << "SACD DLNA: indexed " << static_cast<unsigned>(m_sharedCount.load()) << " DSD tracks, UpdateID " << m_updateId;
}

void SacdDlnaServer::share_music_library() {
    m_libraryRefreshPending = false;
    if (!sacd_plugin_installed()) {
        popup_message::g_show("The Super Audio CD Decoder (foo_input_sacd.dll) is required before the DSD Music Library can be shared.", "SACD DLNA");
        return;
    }
    if (!library_manager::get()->is_library_enabled()) {
        library_manager::get()->show_preferences();
        console::print("SACD DLNA: Media Library is not enabled; opened foobar2000 Music Library preferences");
        return;
    }
    pfc::list_t<metadb_handle_ptr> all; library_manager::get()->get_all_items(all);
    metadb_handle_list dsd;
    for (size_t i = 0; i < all.get_count(); ++i) {
        const char* path = all[i]->get_path(); if (!path) continue; const char* ext = strrchr(path, '.'); if (!ext) continue;
        const bool isDsd = !_stricmp(ext, ".iso") || !_stricmp(ext, ".dsf") || !_stricmp(ext, ".dff");
        const bool isPcm = !_stricmp(ext, ".flac") || !_stricmp(ext, ".wav") || !_stricmp(ext, ".aif") || !_stricmp(ext, ".aiff") ||
            !_stricmp(ext, ".wv") || !_stricmp(ext, ".tta") || !_stricmp(ext, ".ape") || !_stricmp(ext, ".mp3") ||
            !_stricmp(ext, ".m4a") || !_stricmp(ext, ".mp4") || !_stricmp(ext, ".ogg") || !_stricmp(ext, ".opus") || !_stricmp(ext, ".aac");
        if (isDsd || (static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled) && isPcm)) dsd += all[i];
    }
    publish(dsd); m_sharingLibrary = true;
    console::printf("SACD DLNA: Music Library SHARING / %u DSD tracks", static_cast<unsigned>(m_sharedCount.load()));
}

void SacdDlnaServer::request_library_refresh() {
    if (!m_sharingLibrary.load() || !m_running.load()) return;
    bool expected = false;
    if (m_libraryRefreshPending.compare_exchange_strong(expected, true)) main_thread_callback_spawn<library_refresh_callback>();
}
