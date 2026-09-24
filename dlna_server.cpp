#include "stdafx.h"
#include "dlna_server.h"
#include "sacd_decode.h"
#include "config.h"
#include "dsp_bridge.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <shellapi.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
// Mirrors dvd_audio_flac.cpp's own DLL search order (component directory
// first, then the default LoadLibrary search path) without touching any of
// its state, so this is safe to call from start() purely as a diagnostic.
bool probeLibFlacDllPresent(std::wstring& triedPathOut) {
    wchar_t path[MAX_PATH]{};
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&probeLibFlacDllPresent), &self)) {
        const DWORD n = GetModuleFileNameW(self, path, static_cast<DWORD>(std::size(path)));
        if (n && n < std::size(path)) {
            std::wstring dir(path, path + n);
            const auto slash = dir.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                dir.resize(slash);
                triedPathOut = dir + L"\\libFLAC.dll";
                HMODULE probe = LoadLibraryExW(triedPathOut.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
                if (probe) { FreeLibrary(probe); return true; }
            }
        }
    }
    HMODULE fallback = LoadLibraryExW(L"libFLAC.dll", nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (fallback) { FreeLibrary(fallback); return true; }
    return false;
}
}

namespace {

constexpr const char* kUuid = "uuid:7e2f2d7e-7d89-4d3c-9f3e-3a7c09fd1234";
constexpr const char* kRootObject = "0";
constexpr const char* kArtistsObject = "artists";
constexpr const char* kArtistPrefix = "artist-";
constexpr const char* kAlbumPrefix = "album-";
constexpr const char* kTrackPrefix = "track-";
constexpr const char* kAlbumsObject = "albums";
constexpr const char* kGenresObject = "genres";
constexpr const char* kFoldersObject = "folders";
constexpr const char* kAllTracksObject = "alltracks";
constexpr const char* kPlaylistsObject = "playlists";
constexpr const char* kPlaylistPrefix = "playlist-";
constexpr const char* kGenrePrefix = "genre-";
constexpr const char* kFolderPrefix = "folder-";
constexpr size_t kMaxHttpHeader = 128 * 1024;
constexpr size_t kMaxSoapBody = 2 * 1024 * 1024;
constexpr uint32_t kCacheFormatVersion = 4; // invalidates caches produced by the removed hand-written FLAC writer
constexpr const char* kVersion = "1.0.0";

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Name of the UPnP action in a SOAP request ("Browse", "GetSortCapabilities"...), or "" when it
// cannot be determined. SOAPACTION looks like "urn:schemas-upnp-org:service:ContentDirectory:1#Browse"
// (quotes optional). Without the header, the first element inside the SOAP body is the action, whatever
// namespace prefix the control point chose (<u:Browse>, <ns0:Browse>, ...).
std::string soapActionName(const std::string& soapActionHeader, const std::string& body) {
    const size_t hash = soapActionHeader.rfind('#');
    if (hash != std::string::npos) {
        std::string name = soapActionHeader.substr(hash + 1);
        while (!name.empty() && (name.back() == '"' || name.back() == ' ' || name.back() == '\r' || name.back() == '\n' || name.back() == '\t')) name.pop_back();
        return name;
    }
    size_t pos = 0;
    while ((pos = body.find('<', pos)) != std::string::npos) {
        ++pos;
        if (pos >= body.size() || body[pos] == '?' || body[pos] == '/' || body[pos] == '!') continue;
        size_t end = pos;
        while (end < body.size() && !std::isspace(static_cast<unsigned char>(body[end])) && body[end] != '>' && body[end] != '/') ++end;
        const std::string tag = body.substr(pos, end - pos);
        const size_t colon = tag.find(':');
        const std::string local = colon == std::string::npos ? tag : tag.substr(colon + 1);
        if (!local.empty() && local != "Envelope" && local != "Body" && local != "Header") return local;
        pos = end;
    }
    return std::string();
}

std::string trimCopy(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

std::mutex g_logMutex;

std::wstring utf8ToWide(const char* text);   // defined further down
uint64_t fileSizeSafe(const std::wstring& path);

// foobar2000 hands out paths in its canonical form: local files look like
// "file://D:\Music\album.dsf", and core_api::get_profile_path() is a file:// URL
// too. Win32 and std::filesystem need the native form. Returns an empty string for
// anything that is not a plain local file (unpack://, cdda://, http://...). This
// uses the SDK's own parser, so the two can never disagree.
std::wstring nativePathFromFb2k(const char* fb2kPath) {
    if (!fb2kPath || !*fb2kPath) return {};
    pfc::string8 native;
    if (!foobar2000_io::extract_native_path(fb2kPath, native)) return {};
    return utf8ToWide(native.c_str());
}

// Native profile directory, with a trailing backslash; empty when unavailable.
std::wstring profileFolderNative() {
    std::wstring p = nativePathFromFb2k(core_api::get_profile_path());
    if (!p.empty() && p.back() != L'\\' && p.back() != L'/') p += L'\\';
    return p;
}

std::wstring networkLogPath() {
    std::wstring p = profileFolderNative();
    if (p.empty()) return {};
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
        if (path.empty()) return;
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
    SacdDlnaServer::instance().record_error(message);
    if (!static_cast<bool>(sacd_dlna_cfg::network_logging) && !static_cast<bool>(sacd_dlna_cfg::debug_diagnostics)) return;
    std::lock_guard<std::mutex> g(g_logMutex);
    FB2K_console_formatter() << "SACD DLNA [error]: " << message.c_str();
    try {
        const auto path = networkLogPath();
        if (path.empty()) return;
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
    std::wstring p = profileFolderNative();
    if (p.empty()) {
        // No native profile directory (should not happen): use the temp directory
        // rather than ever writing relative to the current working directory.
        wchar_t tmp[MAX_PATH + 2]{};
        const DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
        p.assign(tmp, n);
        if (!p.empty() && p.back() != L'\\' && p.back() != L'/') p += L'\\';
    }
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
    const std::wstring ws = nativePathFromFb2k(sourcePath.c_str());
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


// Structural validation for generated DVD-A FLAC.  This deliberately avoids
// interpreting FLAC frame internals: frame headers/subframes/CRCs are owned by
// libFLAC.  We only verify the native fLaC marker, STREAMINFO and that at least
// one frame starts after the complete metadata chain.
bool validateFlacFile(const std::wstring& path, uint32_t expectedRate = 0, uint32_t expectedChannels = 0, uint32_t expectedBits = 0, uint64_t expectedSamples = 0) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char signature[4]{};
    f.read(signature, sizeof(signature));
    if (f.gcount() != static_cast<std::streamsize>(sizeof(signature)) || memcmp(signature, "fLaC", 4) != 0) return false;

    bool haveStreamInfo = false;
    bool lastMetadata = false;
    uint32_t rate = 0;
    uint32_t channels = 0;
    uint32_t bits = 0;
    uint64_t samples = 0;

    while (!lastMetadata) {
        uint8_t header[4]{};
        f.read(reinterpret_cast<char*>(header), sizeof(header));
        if (f.gcount() != static_cast<std::streamsize>(sizeof(header))) return false;

        lastMetadata = (header[0] & 0x80) != 0;
        const uint8_t type = header[0] & 0x7F;
        const uint32_t length = (static_cast<uint32_t>(header[1]) << 16) |
                                (static_cast<uint32_t>(header[2]) << 8) |
                                static_cast<uint32_t>(header[3]);
        if (type == 0) {
            if (haveStreamInfo || length != 34) return false;
            std::array<uint8_t, 34> info{};
            f.read(reinterpret_cast<char*>(info.data()), static_cast<std::streamsize>(info.size()));
            if (f.gcount() != static_cast<std::streamsize>(info.size())) return false;

            rate = (static_cast<uint32_t>(info[10]) << 12) |
                   (static_cast<uint32_t>(info[11]) << 4) |
                   (static_cast<uint32_t>(info[12]) >> 4);
            channels = ((static_cast<uint32_t>(info[12]) & 0x0E) >> 1) + 1;
            bits = (((static_cast<uint32_t>(info[12]) & 0x01) << 4) |
                    (static_cast<uint32_t>(info[13]) >> 4)) + 1;
            samples = (static_cast<uint64_t>(info[13] & 0x0F) << 32) |
                      (static_cast<uint64_t>(info[14]) << 24) |
                      (static_cast<uint64_t>(info[15]) << 16) |
                      (static_cast<uint64_t>(info[16]) << 8) |
                      static_cast<uint64_t>(info[17]);
            if (!rate || !channels || !bits || !samples) return false;
            haveStreamInfo = true;
        } else {
            f.seekg(static_cast<std::streamoff>(length), std::ios::cur);
            if (!f) return false;
        }
    }

    if (!haveStreamInfo) return false;
    if (expectedRate && rate != expectedRate) return false;
    if (expectedChannels && channels != expectedChannels) return false;
    if (expectedBits && bits != expectedBits) return false;
    if (expectedSamples && samples != expectedSamples) return false;

    // A native FLAC stream must contain encoded audio after the metadata.
    uint8_t framePrefix[2]{};
    f.read(reinterpret_cast<char*>(framePrefix), sizeof(framePrefix));
    if (f.gcount() != static_cast<std::streamsize>(sizeof(framePrefix))) return false;
    if (framePrefix[0] != 0xFF || (framePrefix[1] & 0xFC) != 0xF8) return false;

    return fileSizeSafe(path) > 42;
}

bool jsonNumberFieldEquals(const std::string& json, const char* name, uint64_t value) {
    const std::string needle = std::string("\"") + name + "\":" + std::to_string(value);
    size_t pos = json.find(needle);
    while (pos != std::string::npos) {
        const size_t end = pos + needle.size();
        if (end == json.size() || json[end] == ',' || json[end] == '}') return true;
        pos = json.find(needle, pos + 1);
    }
    return false;
}
bool jsonNumberFieldEquals(const std::string& json, const char* name, int64_t value) {
    const std::string needle = std::string("\"") + name + "\":" + std::to_string(value);
    size_t pos = json.find(needle);
    while (pos != std::string::npos) {
        const size_t end = pos + needle.size();
        if (end == json.size() || json[end] == ',' || json[end] == '}') return true;
        pos = json.find(needle, pos + 1);
    }
    return false;
}

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
    size_t lineStart = 0;
    while (lineStart < headers.size()) {
        const auto lineEnd = headers.find("\r\n", lineStart);
        const size_t end = lineEnd == std::string::npos ? headers.size() : lineEnd;
        const auto colon = headers.find(':', lineStart);
        if (colon != std::string::npos && colon <= end) {
            std::string headerName = lower.substr(lineStart, colon - lineStart);
            if (trimCopy(headerName) == target) {
                out = trimCopy(headers.substr(colon + 1, end - colon - 1));
                return true;
            }
        }
        if (lineEnd == std::string::npos) break;
        lineStart = lineEnd + 2;
    }
    return false;
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
    if (value.find(',', 6) != std::string::npos) return false; // multipart/byteranges not implemented
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
        try {
            const unsigned long parsedPort = std::stoul(authority.substr(colon + 1));
            if (parsedPort == 0 || parsedPort > 65535) return false;
            out.port = static_cast<uint16_t>(parsedPort);
        } catch (...) { return false; }
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
    size_t sent = 0;
    while (sent < req.size()) {
        const int n = send(c, req.data() + sent, static_cast<int>(std::min<size_t>(req.size() - sent, 1u << 20)), 0);
        if (n <= 0) { closesocket(c); return false; }
        sent += static_cast<size_t>(n);
    }

    std::string reply;
    char buf[16384];
    size_t expectedTotal = 0;
    while (reply.size() < 4 * 1024 * 1024) {
        const int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        reply.append(buf, buf + n);
        if (!expectedTotal) {
            const auto splitNow = reply.find("\r\n\r\n");
            if (splitNow != std::string::npos) {
                std::string cl;
                if (getHeaderValue(reply.substr(0, splitNow + 4), "Content-Length", cl)) {
                    try {
                        expectedTotal = splitNow + 4 + static_cast<size_t>(std::stoull(cl));
                    } catch (...) { expectedTotal = 0; }
                }
                if (expectedTotal && reply.size() >= expectedTotal) break;
            }
        } else if (reply.size() >= expectedTotal) break;
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
    // request_library_refresh() already returns immediately unless the library is
    // being shared, so there is no need to build a full status snapshot per callback.
    void request() { SacdDlnaServer::instance().request_library_refresh(); }
};

SacdDlnaServer& SacdDlnaServer::instance() {
    static SacdDlnaServer x;
    return x;
}

void SacdDlnaServer::record_error(const std::string& message) {
    std::lock_guard<std::mutex> g(m_diagMutex);
    m_lastError = message;
}

void SacdDlnaServer::set_enabled(bool enabled) {
    if (enabled) start(); else stop();
}

void SacdDlnaServer::start() {
    std::lock_guard<std::mutex> lifecycle(m_lifecycleMutex);
    if (m_running.load()) return;

    // Recover cleanly from a previous failed start where worker thread objects
    // are still joinable even though m_running became false.
    if (m_httpThread.joinable() || m_ssdpThread.joinable() || !m_clientThreads.empty() || !m_prefetchThreads.empty() || m_wsaStarted.load()) {
        m_running = false;
        stopUnlocked();
    }

    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true)) return;

    if (!sacd_plugin_installed()) {
        m_running = false;
        console::print("SACD DLNA: Super Audio CD Decoder (foo_input_sacd.dll) is not installed");
        return;
    }

    // Non-fatal: DVD-Audio -> FLAC conversion needs libFLAC.dll next to
    // foo_sacd_dlna.dll (see FLAC_RUNTIME.md). Missing it does not stop DSD/SACD
    // sharing, but every DVD-Audio track will otherwise fail silently, one at a
    // time, as "DVDA_CACHE_FAILED" 503s that only show up in the DLNA renderer
    // and in dlna_dvda_flac.log -- easy to miss for days. Say it once, loudly,
    // right when the component starts, with the exact path that was tried.
    {
        std::wstring triedPath;
        if (!probeLibFlacDllPresent(triedPath)) {
            if (!triedPath.empty()) {
                console::printf(
                    "SACD DLNA: libFLAC.dll was not found at \"%s\" -- DVD-Audio to FLAC "
                    "conversion will fail for every track until it is copied there "
                    "(see FLAC_RUNTIME.md). DSD/SACD sharing is not affected.",
                    pfc::stringcvt::string_utf8_from_wide(triedPath.c_str()).get_ptr());
            } else {
                console::print(
                    "SACD DLNA: libFLAC.dll was not found -- DVD-Audio to FLAC conversion "
                    "will fail for every track until it is copied next to foo_sacd_dlna.dll "
                    "(see FLAC_RUNTIME.md). DSD/SACD sharing is not affected.");
            }
        }
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
        m_lastError.clear();
    }
    m_clientRegistry.reset();
    m_streamLimiter.resetCounters();

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        m_running = false;
        std::lock_guard<std::mutex> g(m_diagMutex);
        m_networkDiagnostic = "WSAStartup failed";
        return;
    }
    m_wsaStarted = true;

    {
        std::lock_guard<std::mutex> g(m_diagMutex);
        m_localIp = localAddress();
        m_networkDiagnostic = "Server starting; waiting for HTTP/SSDP sockets";
    }

    try {
        m_libraryTracker = std::make_shared<library_tracker>();
        m_httpThread = std::thread([this] { httpLoop(); });
        m_ssdpThread = std::thread([this] { ssdpLoop(); });
    } catch (const std::exception& e) {
        m_running = false;
        { std::lock_guard<std::mutex> g(m_diagMutex); m_networkDiagnostic = std::string("Unable to start worker threads: ") + e.what(); }
        stopUnlocked();
        return;
    } catch (...) {
        m_running = false;
        { std::lock_guard<std::mutex> g(m_diagMutex); m_networkDiagnostic = "Unable to start worker threads"; }
        stopUnlocked();
        return;
    }

    networkLog("server started on TCP " + std::to_string(m_port));
    console::print("SACD DLNA: BROADCASTING / ACTIVE");
}

void SacdDlnaServer::stopUnlocked() {
    m_running = false;

    if (m_httpListen != INVALID_SOCKET) {
        shutdown(m_httpListen, SD_BOTH);
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
        for (auto& kv : m_cacheJobs) if (kv.second && kv.second->aborter) kv.second->aborter->set();
    }

    clearPrefetchThreads();
    for (auto& t : m_clientThreads) if (t.joinable()) t.join();
    m_clientThreads.clear();
    m_finishedClientThreads.clear();
    for (auto& t : m_prefetchThreads) if (t.joinable()) t.join();
    {
        std::lock_guard<std::mutex> g(m_prefetchMutex);
        m_prefetchThreads.clear();
        m_finishedPrefetchThreads.clear();
        m_prefetchAborters.clear();
        m_prefetchKeys.clear();
        m_prefetchActive = false;
        m_prefetchActiveCount = 0;
        m_prefetchState = "IDLE";
        m_prefetchTitle.clear();
    }
    {
        std::lock_guard<std::mutex> g(m_clientMutex);
        m_clients.clear();
    }

    if (m_ssdpThread.joinable()) m_ssdpThread.join();
    m_libraryTracker.reset();

    {
        std::lock_guard<std::mutex> g(m_diagMutex);
        m_httpReady = false;
        m_ssdpReady = false;
        m_networkDiagnostic = "Stopped";
        m_networkVisibility = "SERVER STOPPED";
    }

    if (m_wsaStarted.exchange(false)) WSACleanup();
    networkLog("server stopped");
}

void SacdDlnaServer::stop() {
    std::lock_guard<std::mutex> lifecycle(m_lifecycleMutex);
    stopUnlocked();
    console::print("SACD DLNA: broadcasting stopped");
}

size_t SacdDlnaServer::shared_count() const { return m_sharedCount.load(); }

uint64_t SacdDlnaServer::cached_persistent_cache_bytes() const {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> g(m_cacheStatsMutex);
    if (m_cacheStatsTick.time_since_epoch().count() == 0 ||
        std::chrono::duration_cast<std::chrono::seconds>(now - m_cacheStatsTick).count() >= 2) {
        m_cachedCacheBytes = persistent_cache_bytes();
        m_cacheStatsTick = now;
    }
    return m_cachedCacheBytes;
}

SacdDlnaStatus SacdDlnaServer::get_status() const {
    SacdDlnaStatus s;
    s.broadcasting = is_running();
    s.sacdInstalled = sacd_plugin_installed(&s.sacdVersion);
    s.dvdaInstalled = dvda_plugin_installed();
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
        s.lastError = m_lastError.c_str();
    }

    s.stabilityMode = sacd_dlna_cfg::stability_mode;
    s.serverName = sacd_dlna_cfg::server_name;
    {
        std::lock_guard<std::mutex> g(m_rateMutex);
        s.conversionActive = m_conversionActive;
        s.conversionPercent = m_conversionPercent;
        s.prebufferBytes = m_prebufferBytes;
        s.prebufferTargetBytes = m_prebufferTargetBytes;
        if (!s.streamingActive || !s.prebufferTargetBytes) s.bufferState = s.streamingActive ? "STREAMING / NO READ-AHEAD" : "IDLE";
        else if (s.prebufferBytes == 0) s.bufferState = "READ-AHEAD CONSUMED / STREAMING";
        else {
            const uint64_t pct = s.prebufferBytes * 100ULL / s.prebufferTargetBytes;
            if (pct >= 80) s.bufferState = "READY / FULL RESERVE";
            else if (pct >= 25) s.bufferState = "DRAINING / HEALTHY";
            else s.bufferState = "LOW / REFILL NOT AVAILABLE";
        }
        if (s.nominalBitrate && s.bytesPerSecond) s.networkHeadroom = static_cast<double>(s.bytesPerSecond) / static_cast<double>(s.nominalBitrate);
        if (s.requiredBytesPerSecond && s.bytesPerSecond) s.realtimeMultiplier = static_cast<double>(s.bytesPerSecond) / static_cast<double>(s.requiredBytesPerSecond);
    }
    {
        std::lock_guard<std::mutex> g(m_prefetchMutex);
        s.prefetchTitle = m_prefetchTitle.c_str();
        s.prefetchState = m_prefetchState.c_str();
    }
    s.cacheBytes = cached_persistent_cache_bytes();
    {
        const clientreg::Counts cc = m_clientRegistry.counts(clientreg::Clock::now());
        s.clientsTotal = cc.total; s.clientsActive = cc.active; s.clientsIdle = cc.idle; s.clientsSeenSinceStart = cc.seenSinceStart;
        s.streamLimit = sacd_dlna_max_streams();
        s.streamSlotsUsed = m_streamLimiter.current();
        s.streamsRejected = m_streamLimiter.rejected();
    }
    return s;
}

bool SacdDlnaServer::isClientPeer(const std::string& ip) const {
    if (ip.empty() || ip == "unknown" || ip == "127.0.0.1") return false;
    std::lock_guard<std::mutex> g(m_diagMutex);
    return ip != m_localIp;
}

void SacdDlnaServer::updateStreamStart(const std::string& peerIp, const DsdTrack& track,
                                       const std::string& sourceFormat, const std::string& outputFormat,
                                       const std::string& pipelineState, const std::string& conversionState,
                                       uint64_t sourceSize, uint64_t outputSize, uint32_t sourceSampleRate,
                                       uint32_t sourceChannels, uint32_t sourceBitsPerSample) {
    std::lock_guard<std::mutex> g(m_rateMutex);
    ++m_activeStreams;
    if (m_activeStreams == 1) {
        m_streamBytes = 0;
        m_rateBytes = 0;
        m_lastRateBytes = 0;
        m_currentBps = 0;
        m_lastRateTick = std::chrono::steady_clock::now();
        m_streamStartTick = m_lastRateTick;

        // Status/diagnostics only track one "now playing" set of fields, but
        // serveMedia() supports several simultaneous HTTP clients (the limit is the
        // "Max streams" preference). Only updating these on the 0->1 transition means the Status
        // UI consistently shows the stream that has been running the longest,
        // instead of flickering to whichever client most recently connected
        // (and, on that client's disconnect, snapping back to stale/blank
        // fields even though an earlier stream is still active). A full
        // per-connection breakdown is tracked separately - see ROADMAP.md.
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
    }
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
    std::string out;
    return getHeaderValue(response, header, out) ? out : std::string{};
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
        } else if (lowerDefault == "audio/flac") {
            // Different UPnP renderers use both MIME spellings for FLAC.
            // Prefer the exact standard spelling when the renderer advertises it,
            // but fall back to the legacy x-flac spelling when that is what the
            // renderer's ConnectionManager actually accepts.
            if (protocolListSupports(protocols, "audio/flac")) return "audio/flac";
            if (protocolListSupports(protocols, "audio/x-flac")) return "audio/x-flac";
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

bool SacdDlnaServer::isDvdAudioInput(const char* path) {
    if (!path || !*path) return false;
    pfc::list_t<input_entry::ptr> inputs;
    if (!input_entry::g_find_inputs_by_path(inputs, path, false)) return false;
    for (auto const& entry : inputs) {
        input_entry_v2::ptr v2;
        if (v2 &= entry) {
            const char* name = v2->get_name();
            if (name && (strstr(name, "DVD-Audio") || strstr(name, "DVD Audio") || strstr(name, "DVD-A"))) return true;
        }
    }
    return false;
}

std::string SacdDlnaServer::mimeForExtension(const std::string& ext) {
    if (!_stricmp(ext.c_str(), ".dsf")) return "audio/x-dsf";
    if (!_stricmp(ext.c_str(), ".dff")) return "audio/x-dff";
    if (!_stricmp(ext.c_str(), ".flac")) return "audio/flac";
    if (!_stricmp(ext.c_str(), ".wav")) return "audio/wav";
    if (!_stricmp(ext.c_str(), ".aif") || !_stricmp(ext.c_str(), ".aiff")) return "audio/aiff";
    if (!_stricmp(ext.c_str(), ".mp3")) return "audio/mpeg";
    if (!_stricmp(ext.c_str(), ".m4a") || !_stricmp(ext.c_str(), ".mp4")) return "audio/mp4";
    if (!_stricmp(ext.c_str(), ".ogg")) return "audio/ogg";
    if (!_stricmp(ext.c_str(), ".opus")) return "audio/opus";
    if (!_stricmp(ext.c_str(), ".aac")) return "audio/aac";
    if (!_stricmp(ext.c_str(), ".wv")) return "audio/wavpack";
    if (!_stricmp(ext.c_str(), ".ape")) return "audio/x-ape";
    if (!_stricmp(ext.c_str(), ".tta")) return "audio/x-tta";
    return "application/octet-stream";
}



// The "Shared formats" filter as a set of lower-case extensions with a leading dot. It is parsed ONCE per
// publish()/share_music_library(); formatAllowed() used to re-parse (two string copies plus tokenising) for
// every single track.
static std::unordered_set<std::string> parseSharedFormats(const std::string& listText) {
    std::unordered_set<std::string> out;
    const std::string list = lowerCopy(listText);
    size_t pos = 0;
    while (pos <= list.size()) {
        const size_t sep = list.find_first_of(",; \t\r\n", pos);
        std::string token = list.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos);
        if (!token.empty() && token != ".") {
            if (token[0] != '.') token.insert(token.begin(), '.');
            out.insert(token);
        }
        if (sep == std::string::npos) break;
        pos = sep + 1;
    }
    return out;
}

bool SacdDlnaServer::formatAllowed(const std::string& ext) {
    const std::string needle = lowerCopy(ext.empty() || ext[0] == '.' ? ext : "." + ext);
    return parseSharedFormats(std::string(sacd_dlna_cfg::shared_formats.get())).count(needle) != 0;
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
    // FLAC has a registered DLNA media-format profile. Advertising the profile
    // is important for strict renderers: MIME alone does not always identify
    // the media format profile they are willing to render.
    if (!_stricmp(mime.c_str(), "audio/flac") || !_stricmp(mime.c_str(), "audio/x-flac")) {
        return "http-get:*:" + mime + ":DLNA.ORG_PN=FLAC;DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=03700000000000000000000000000000";
    }
    return "http-get:*:" + mime + ":DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000";
}

std::wstring SacdDlnaServer::cacheFolder() const { return persistentCacheFolder(); }

std::string SacdDlnaServer::localAddress() const {
    ULONG size = 16 * 1024;
    std::vector<uint8_t> storage(size);
    IP_ADAPTER_ADDRESSES* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    ULONG rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, adapters, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        storage.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, adapters, &size);
    }
    if (rc == NO_ERROR) {
        std::string best;
        int bestScore = -1;
        for (auto* a = adapters; a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || !a->FirstUnicastAddress) continue;
            const bool hasGateway = a->FirstGatewayAddress != nullptr;
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
                if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
                const auto* sin = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
                char ip[INET_ADDRSTRLEN]{};
                inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
                const std::string value = ip;
                if (value.empty() || value == "127.0.0.1" || value.rfind("169.254.", 0) == 0) continue;
                const uint32_t addr = ntohl(sin->sin_addr.s_addr);
                const bool private10 = (addr & 0xFF000000u) == 0x0A000000u;
                const bool private172 = (addr & 0xFFF00000u) == 0xAC100000u;
                const bool private192 = (addr & 0xFFFF0000u) == 0xC0A80000u;
                int score = 0;
                if (private10 || private172 || private192) score += 100;
                if (hasGateway) score += 50;
                if (a->IfType == IF_TYPE_ETHERNET_CSMACD) score += 20;
                if (a->IfType == IF_TYPE_IEEE80211) score += 20;
                if (a->IfType == IF_TYPE_PPP) score -= 40;
                if (a->IfType == IF_TYPE_TUNNEL) score -= 60;
                if (score > bestScore) { bestScore = score; best = value; }
            }
        }
        if (!best.empty()) return best;
    }
    // Conservative fallback: resolve the host name only when adapter enumeration fails.
    char host[256]{};
    if (gethostname(host, sizeof(host)) == 0) {
        addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host, nullptr, &hints, &res) == 0 && res) {
            std::string fallback = "127.0.0.1";
            for (addrinfo* it = res; it; it = it->ai_next) {
                const auto* sin = reinterpret_cast<const sockaddr_in*>(it->ai_addr);
                if (!sin) continue;
                char ip[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
                const std::string value = ip;
                if (value != "127.0.0.1" && !value.empty() && value.rfind("169.254.", 0) != 0) { fallback = value; break; }
            }
            freeaddrinfo(res);
            return fallback;
        }
    }
    return "127.0.0.1";
}

std::string SacdDlnaServer::makeDeviceXml() const {
    const std::string name = xmlEscape(sacd_dlna_cfg::server_name.get().c_str());
    std::string lanIp;
    { std::lock_guard<std::mutex> g(m_diagMutex); lanIp = m_localIp; }
    if (lanIp.empty()) lanIp = localAddress();
    const std::string base = "http://" + lanIp + ":" + std::to_string(m_port);
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<root xmlns=\"urn:schemas-upnp-org:device-1-0\" xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion>"
        "<device><deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>"
        "<dlna:X_DLNADOC>DMS-1.50</dlna:X_DLNADOC>"
        "<friendlyName>" + name + "</friendlyName><manufacturer>foo_sacd_dlna</manufacturer>"
        "<manufacturerURL>https://www.foobar2000.org/</manufacturerURL><modelName>foobar2000 SACD DLNA</modelName>"
        "<modelDescription>Native DSD UPnP Media Server</modelDescription><modelNumber>" + std::string(kVersion) + "</modelNumber>"
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
        "<action><name>GetSearchCapabilities</name><argumentList><argument><name>SearchCaps</name><direction>out</direction><relatedStateVariable>SearchCapabilities</relatedStateVariable></argument></argumentList></action>"
        "<action><name>GetSortCapabilities</name><argumentList><argument><name>SortCaps</name><direction>out</direction><relatedStateVariable>SortCapabilities</relatedStateVariable></argument></argumentList></action>"
        "<action><name>GetSystemUpdateID</name><argumentList><argument><name>Id</name><direction>out</direction><relatedStateVariable>SystemUpdateID</relatedStateVariable></argument></argumentList></action>"
        "</actionList><serviceStateTable>"
        "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ObjectID</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_BrowseFlag</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Filter</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Index</name><dataType>ui4</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Count</name><dataType>ui4</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_SortCriteria</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Result</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>SearchCapabilities</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>SortCapabilities</name><dataType>string</dataType></stateVariable>"
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
    // localAddress() may resolve the host name (blocking), so do it BEFORE taking the
    // library lock. The response is built straight from the shared data under the lock
    // (no per-request copy of the library).
    const std::string base = "http://" + localAddress() + ":" + std::to_string(m_port);
    std::lock_guard<std::mutex> libraryLock(m_mutex);
    const std::vector<Item>& items = m_items;
    const std::vector<Artist>& artists = m_artists;
    const std::vector<Album>& albums = m_albums;
    const std::vector<Genre>& genres = m_genres;
    const std::vector<Folder>& folders = m_folders;
    const std::vector<Playlist>& playlists = m_playlists;
    const uint32_t updateId = m_updateId;

    numberReturned = totalMatches = 0;
    const std::string ns = " xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\""
        " xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
        " xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\""
        " xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0/\"";
    std::string out = "<DIDL-Lite" + ns + ">";

    // id -> object lookups (hash maps rebuilt by publish()).
    auto findArtist = [&](uint32_t id) -> const Artist* { const auto it = m_artistIndex.find(id); return it == m_artistIndex.end() ? nullptr : &artists[it->second]; };
    auto findAlbum = [&](uint32_t id) -> const Album* { const auto it = m_albumIndex.find(id); return it == m_albumIndex.end() ? nullptr : &albums[it->second]; };
    auto findGenre = [&](uint32_t id) -> const Genre* { const auto it = m_genreIndex.find(id); return it == m_genreIndex.end() ? nullptr : &genres[it->second]; };
    auto findFolder = [&](uint32_t id) -> const Folder* { const auto it = m_folderIndex.find(id); return it == m_folderIndex.end() ? nullptr : &folders[it->second]; };
    auto findPlaylist = [&](uint32_t id) -> const Playlist* {
        for (const auto& p : playlists) if (p.id == id) return &p;
        return nullptr;
    };
    auto findItem = [&](uint32_t id) -> const Item* { const auto it = m_itemIndex.find(id); return it == m_itemIndex.end() ? nullptr : &items[it->second]; };
    auto isPrefixed = [](const std::string& id, const char* prefix) { return id.rfind(prefix, 0) == 0; };
    auto parseId = [](const std::string& id, const char* prefix) -> uint32_t {
        try { return static_cast<uint32_t>(std::stoul(id.substr(strlen(prefix)))); } catch (...) { return 0; }
    };

    auto artUri = [&](uint32_t albumId) { return base + "/art/" + std::to_string(albumId); };

    auto appendContainer = [&](const std::string& id, const std::string& parent, const std::string& title, const char* cls, size_t childCount,
                               uint32_t artAlbumId = 0, const std::string& creator = std::string()) {
        out += "<container id=\"" + xmlEscape(id) + "\" parentID=\"" + xmlEscape(parent) + "\" restricted=\"1\" childCount=\"" + std::to_string(childCount) + "\">";
        out += "<dc:title>" + xmlEscape(title) + "</dc:title><upnp:class>" + cls + "</upnp:class>";
        if (!creator.empty()) out += "<dc:creator>" + xmlEscape(creator) + "</dc:creator>";
        if (artAlbumId) out += "<upnp:albumArtURI>" + artUri(artAlbumId) + "</upnp:albumArtURI>";
        out += "</container>";
    };

    auto appendTrack = [&](const Item& item, const std::string& parentId, const std::string& objectIdForTrack) {
        const std::string title = item.track.title.empty() ? ("Track " + std::to_string(item.id)) : item.track.title;
        const std::string servedExt = item.dvdAudio ? ".flac" : (static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled) || (_stricmp(item.sourceExt.c_str(), ".iso") == 0) ? ".dsf" : item.sourceExt);
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
        if (item.dvdAudio) out += "<dc:format>audio/flac</dc:format>";
        else if (item.track.dsdRate || _stricmp(item.sourceExt.c_str(), ".iso") == 0)
            out += "<dc:format>audio/dsd</dc:format>";
        else
            out += "<dc:format>" + xmlEscape(defaultMime) + "</dc:format>";
        if (item.albumId) out += "<upnp:albumArtURI>" + artUri(item.albumId) + "</upnp:albumArtURI>";

        std::string res = "<res protocolInfo=\"" + chooseRendererProtocolInfo(mime) + "\"";
        // For DVD-Audio the source file size is not the served FLAC size.
        // Do not publish a false res@size value: the renderer may use it for
        // HTTP range requests and reject a resource whose advertised length
        // does not match Content-Length.
        if (item.track.fileSize && !item.dvdAudio) res += " size=\"" + std::to_string(item.track.fileSize) + "\"";
        if (item.track.duration > 0) res += " duration=\"" + formatDuration(item.track.duration) + "\"";
        if (item.track.dsdRate) {
            res += " sampleFrequency=\"" + std::to_string(item.track.dsdRate) + "\"";
            res += " bitsPerSample=\"1\"";
            res += " nrAudioChannels=\"" + std::to_string(item.track.channels ? item.track.channels : 2) + "\"";
            // DIDL-Lite res@bitrate is expressed in bytes/second, not bits/second.
            res += " bitrate=\"" + std::to_string(static_cast<uint64_t>(item.track.dsdRate) / 4ULL) + "\"";
        } else if (item.dvdAudio && item.sourceSampleRate) {
            res += " sampleFrequency=\"" + std::to_string(item.sourceSampleRate) + "\"";
            if (item.sourceBitsPerSample) res += " bitsPerSample=\"" + std::to_string(item.sourceBitsPerSample) + "\"";
            if (item.sourceChannels) res += " nrAudioChannels=\"" + std::to_string(item.sourceChannels) + "\"";
        }
        res += ">" + base + "/media/" + std::to_string(item.id) + servedExt + "</res>";
        out += res;
        out += "</item>";
    };

    // ---- the container tree ----
    // 0 (root) -> Artists -> artist -> album -> tracks
    //          -> Albums  -> album -> tracks
    //          -> Genres  -> genre -> tracks
    //          -> Folders -> folder -> (folders and tracks)
    //          -> All Tracks -> tracks
    const Folder* rootFolder = findFolder(m_folderRootId);
    struct RootEntry { const char* id; const char* title; const char* cls; size_t count; };
    const RootEntry rootEntries[] = {
        { kArtistsObject,  "Artists",    "object.container.person.musicArtist", artists.size() },
        { kAlbumsObject,   "Albums",     "object.container.album.musicAlbum",   m_albumsByTitle.size() },
        { kGenresObject,   "Genres",     "object.container.genre.musicGenre",   genres.size() },
        { kFoldersObject,  "Folders",    "object.container.storageFolder",      rootFolder ? rootFolder->folderIds.size() + rootFolder->itemIds.size() : 0 },
        { kAllTracksObject, "All Tracks", "object.container",                   m_allTrackIds.size() },
        { kPlaylistsObject, "Playlists", "object.container.playlistContainer",     playlists.size() },
    };
    constexpr size_t kRootEntryCount = sizeof(rootEntries) / sizeof(rootEntries[0]);
    auto findRootEntry = [&](const std::string& id) -> const RootEntry* { for (const auto& e : rootEntries) if (id == e.id) return &e; return nullptr; };

    const auto artistContainerId = [&](uint32_t id) { return std::string(kArtistPrefix) + std::to_string(id); };
    const auto albumContainerId = [&](uint32_t id) { return std::string(kAlbumPrefix) + std::to_string(id); };
    const auto genreContainerId = [&](uint32_t id) { return std::string(kGenrePrefix) + std::to_string(id); };
    const auto folderContainerId = [&](uint32_t id) { return std::string(kFolderPrefix) + std::to_string(id); };
    const auto playlistContainerId = [&](uint32_t id) { return std::string(kPlaylistPrefix) + std::to_string(id); };
    const auto trackObjectId = [&](uint32_t id) { return std::string(kTrackPrefix) + std::to_string(id); };
    // A folder's parent is the "Folders" root entry when it hangs directly off the (collapsed) root.
    const auto folderParentId = [&](const Folder& f) { return f.parentId == m_folderRootId ? std::string(kFoldersObject) : folderContainerId(f.parentId); };

    auto appendAlbumContainer = [&](const Album& alb, const std::string& parent, bool withCreator) {
        const Artist* artist = withCreator ? findArtist(alb.artistId) : nullptr;
        appendContainer(albumContainerId(alb.id), parent, alb.title, "object.container.album.musicAlbum", alb.itemIds.size(), alb.id, artist ? artist->name : std::string());
    };

    // Applies StartingIndex/RequestedCount to a list of `total` children. emit(n) appends child n and
    // returns true when it produced an element. StartingIndex + RequestedCount is done in 64 bits
    // (some renderers send RequestedCount = 0xFFFFFFFF).
    auto page = [&](unsigned total, auto&& emit) {
        totalMatches = total;
        const unsigned requestedEnd = static_cast<unsigned>(std::min<uint64_t>(static_cast<uint64_t>(startingIndex) + requestedCount, UINT_MAX));
        const unsigned end = requestedCount ? (requestedEnd < total ? requestedEnd : total) : total;
        for (unsigned n = startingIndex; n < end; ++n) if (emit(n)) ++numberReturned;
    };

    auto emitTrackList = [&](const std::vector<uint32_t>& ids, const std::string& parent) {
        page(static_cast<unsigned>(ids.size()), [&](unsigned n) {
            const Item* item = findItem(ids[n]);
            if (!item) return false;
            appendTrack(*item, parent, trackObjectId(item->id));
            return true;
        });
    };

    auto emitFolderChildren = [&](const Folder& f, const std::string& parent) {
        const size_t subfolders = f.folderIds.size();
        page(static_cast<unsigned>(subfolders + f.itemIds.size()), [&](unsigned n) {
            if (n < subfolders) {
                const Folder* sub = findFolder(f.folderIds[n]);
                if (!sub) return false;
                appendContainer(folderContainerId(sub->id), parent, sub->name, "object.container.storageFolder", sub->folderIds.size() + sub->itemIds.size());
                return true;
            }
            const Item* item = findItem(f.itemIds[n - subfolders]);
            if (!item) return false;
            appendTrack(*item, parent, trackObjectId(item->id));
            return true;
        });
    };

    if (metadataOnly) {
        if (objectId == kRootObject) {
            appendContainer(kRootObject, "-1", "foobar2000 SACD DSD", "object.container", kRootEntryCount);
            numberReturned = totalMatches = 1;
        } else if (const RootEntry* entry = findRootEntry(objectId)) {
            appendContainer(entry->id, kRootObject, entry->title, entry->cls, entry->count);
            numberReturned = totalMatches = 1;
        } else if (isPrefixed(objectId, kArtistPrefix)) {
            if (const Artist* a = findArtist(parseId(objectId, kArtistPrefix))) {
                appendContainer(objectId, kArtistsObject, a->name, "object.container.person.musicArtist", a->albumIds.size());
                numberReturned = totalMatches = 1;
            }
        } else if (isPrefixed(objectId, kAlbumPrefix)) {
            if (const Album* a = findAlbum(parseId(objectId, kAlbumPrefix))) {
                appendContainer(objectId, artistContainerId(a->artistId), a->title, "object.container.album.musicAlbum", a->itemIds.size(), a->id);
                numberReturned = totalMatches = 1;
            }
        } else if (isPrefixed(objectId, kGenrePrefix)) {
            if (const Genre* g = findGenre(parseId(objectId, kGenrePrefix))) {
                appendContainer(objectId, kGenresObject, g->name, "object.container.genre.musicGenre", g->itemIds.size());
                numberReturned = totalMatches = 1;
            }
        } else if (isPrefixed(objectId, kFolderPrefix)) {
            if (const Folder* f = findFolder(parseId(objectId, kFolderPrefix))) {
                appendContainer(objectId, folderParentId(*f), f->name, "object.container.storageFolder", f->folderIds.size() + f->itemIds.size());
                numberReturned = totalMatches = 1;
            }
        } else if (isPrefixed(objectId, kPlaylistPrefix)) {
            if (const Playlist* p = findPlaylist(parseId(objectId, kPlaylistPrefix))) {
                appendContainer(objectId, kPlaylistsObject, p->name, "object.container.playlistContainer", p->itemIds.size());
                numberReturned = totalMatches = 1;
            }
        } else if (isPrefixed(objectId, kTrackPrefix)) {
            if (const Item* i = findItem(parseId(objectId, kTrackPrefix))) {
                appendTrack(*i, albumContainerId(i->albumId), objectId);
                numberReturned = totalMatches = 1;
            }
        }
    } else if (objectId == kRootObject) {
        page(static_cast<unsigned>(kRootEntryCount), [&](unsigned n) {
            const RootEntry& e = rootEntries[n];
            appendContainer(e.id, kRootObject, e.title, e.cls, e.count);
            return true;
        });
    } else if (objectId == kArtistsObject) {
        page(static_cast<unsigned>(artists.size()), [&](unsigned n) {
            const Artist& a = artists[n];
            appendContainer(artistContainerId(a.id), kArtistsObject, a.name, "object.container.person.musicArtist", a.albumIds.size());
            return true;
        });
    } else if (isPrefixed(objectId, kArtistPrefix)) {
        if (const Artist* a = findArtist(parseId(objectId, kArtistPrefix))) {
            page(static_cast<unsigned>(a->albumIds.size()), [&](unsigned n) {
                const Album* alb = findAlbum(a->albumIds[n]);
                if (!alb) return false;
                appendAlbumContainer(*alb, objectId, false);
                return true;
            });
        }
    } else if (objectId == kAlbumsObject) {
        page(static_cast<unsigned>(m_albumsByTitle.size()), [&](unsigned n) {
            const Album* alb = findAlbum(m_albumsByTitle[n]);
            if (!alb) return false;
            appendAlbumContainer(*alb, kAlbumsObject, true);   // creator shown: many albums share a title
            return true;
        });
    } else if (isPrefixed(objectId, kAlbumPrefix)) {
        if (const Album* a = findAlbum(parseId(objectId, kAlbumPrefix))) emitTrackList(a->itemIds, objectId);
    } else if (objectId == kGenresObject) {
        page(static_cast<unsigned>(genres.size()), [&](unsigned n) {
            const Genre& g = genres[n];
            appendContainer(genreContainerId(g.id), kGenresObject, g.name, "object.container.genre.musicGenre", g.itemIds.size());
            return true;
        });
    } else if (isPrefixed(objectId, kGenrePrefix)) {
        if (const Genre* g = findGenre(parseId(objectId, kGenrePrefix))) emitTrackList(g->itemIds, objectId);
    } else if (objectId == kFoldersObject) {
        if (rootFolder) emitFolderChildren(*rootFolder, objectId);
    } else if (isPrefixed(objectId, kFolderPrefix)) {
        if (const Folder* f = findFolder(parseId(objectId, kFolderPrefix))) emitFolderChildren(*f, objectId);
    } else if (objectId == kAllTracksObject) {
        emitTrackList(m_allTrackIds, objectId);
    } else if (objectId == kPlaylistsObject) {
        page(static_cast<unsigned>(playlists.size()), [&](unsigned n) {
            const Playlist& p = playlists[n];
            appendContainer(playlistContainerId(p.id), kPlaylistsObject, p.name, "object.container.playlistContainer", p.itemIds.size());
            return true;
        });
    } else if (isPrefixed(objectId, kPlaylistPrefix)) {
        if (const Playlist* p = findPlaylist(parseId(objectId, kPlaylistPrefix)))
            emitTrackList(p->itemIds, playlistContainerId(p->id));
    } else if (isPrefixed(objectId, kTrackPrefix)) {
        if (const Item* i = findItem(parseId(objectId, kTrackPrefix))) {
            appendTrack(*i, albumContainerId(i->albumId), objectId);
            numberReturned = totalMatches = 1;
        }
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
    uint32_t updateId = 1;
    { std::lock_guard<std::mutex> g(m_mutex); updateId = m_updateId; }   // get_status() would scan the cache folder just for this
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
        "<Result>" + xmlEscape(didl) + "</Result><NumberReturned>" + std::to_string(numberReturned) + "</NumberReturned>"
        "<TotalMatches>" + std::to_string(totalMatches) + "</TotalMatches><UpdateID>" + std::to_string(updateId) + "</UpdateID>"
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
        pfc::string8 sacdVersion, dspVersion;
        sacd_plugin_installed(&sacdVersion);
        dsd_processor_installed(&dspVersion);
        const auto dspMarker = std::string("\"dspPreset\":\"") + DsdProcessorBridge::preset_fingerprint() + "\"";
        return jsonNumberFieldEquals(line, "cacheVersion", static_cast<uint64_t>(kCacheFormatVersion)) &&
            jsonNumberFieldEquals(line, "subsong", static_cast<uint64_t>(item.subsong)) &&
            jsonNumberFieldEquals(line, "sourceSize", static_cast<uint64_t>(item.sourceSize)) &&
            jsonNumberFieldEquals(line, "sourceWriteTime", static_cast<int64_t>(item.sourceWriteTime)) &&
            line.find("\"decoderVersion\":\"" + std::string(sacdVersion.c_str()) + "\"") != std::string::npos &&
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
    if (!success) {
        std::string detail;
        {
            std::lock_guard<std::mutex> g(m_diagMutex);
            detail = m_lastError;
        }
        if (detail.empty()) detail = "unknown conversion failure";
        networkLog("DSP DSF cache FAILED: id=" + std::to_string(item.id) +
            " title=\"" + item.track.title + "\" source=" + item.sourcePath +
            " subsong=" + std::to_string(item.subsong) + " detail=" + detail);
        return false;
    }
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
        pfc::string8 sacdVersion;
        sacd_plugin_installed(&sacdVersion);
        return jsonNumberFieldEquals(line, "cacheVersion", static_cast<uint64_t>(kCacheFormatVersion)) &&
            jsonNumberFieldEquals(line, "subsong", static_cast<uint64_t>(item.subsong)) &&
            jsonNumberFieldEquals(line, "sourceSize", static_cast<uint64_t>(item.sourceSize)) &&
            jsonNumberFieldEquals(line, "sourceWriteTime", static_cast<int64_t>(item.sourceWriteTime)) &&
            line.find("\"decoderVersion\":\"" + std::string(sacdVersion.c_str()) + "\"") != std::string::npos && fileSize > 84;
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
    if (!success) {
        std::string detail;
        {
            std::lock_guard<std::mutex> g(m_diagMutex);
            detail = m_lastError;
        }
        if (detail.empty()) detail = "unknown conversion failure";
        networkLog("DVD-A FLAC cache FAILED: id=" + std::to_string(item.id) +
            " title=\"" + item.track.title + "\" source=" + item.sourcePath +
            " subsong=" + std::to_string(item.subsong) + " detail=" + detail);
        return false;
    }
    cachePath = path; return true;
}

bool SacdDlnaServer::ensureCachedFlac(const Item& item, std::wstring& cachePath, DsdTrack& track, abort_callback& abort, bool reportProgress) {
    if (!item.dvdAudio) return false;
    const auto key = std::string("dvda-flac-") + cacheKeyFor(item.sourcePath, item.subsong);
    const auto path = cacheFolder() + L"\\" + utf8ToWide((key + ".flac").c_str());
    const auto manifest = cacheMetaPath(path);
    // Do not gate media serving on componentversion enumeration. foo_input_dvda
    // is the actual decoder used by decode_to_flac(), and a valid cache must remain
    // servable even if componentversion probing temporarily returns false.
    pfc::string8 detectedDvdaVersion;
    const bool decoderDetected = dvda_plugin_installed(&detectedDvdaVersion);
    const std::string detectedVersion = decoderDetected ? std::string(detectedDvdaVersion.c_str()) : std::string();
    if (!decoderDetected) {
        networkLog("DVD-A decoder probe did not find foo_input_dvda; attempting cache/decode directly");
    }

    auto cacheValid = [&]() {
        if (!isRegularFile(path) || !isRegularFile(manifest)) return false;
        std::ifstream mf(manifest, std::ios::binary);
        std::string line((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
        const bool decoderVersionMatches = detectedVersion.empty() ||
            line.find("\"decoderVersion\":\"" + detectedVersion + "\"") != std::string::npos;
        return jsonNumberFieldEquals(line, "cacheVersion", static_cast<uint64_t>(kCacheFormatVersion)) &&
            jsonNumberFieldEquals(line, "subsong", static_cast<uint64_t>(item.subsong)) &&
            jsonNumberFieldEquals(line, "sourceSize", static_cast<uint64_t>(item.sourceSize)) &&
            jsonNumberFieldEquals(line, "sourceWriteTime", static_cast<int64_t>(item.sourceWriteTime)) &&
            decoderVersionMatches && validateFlacFile(path);
    };
    if (cacheValid()) {
        { std::lock_guard<std::mutex> g(m_rateMutex); ++m_cacheHits; }
        cachePath = path; track = item.track; track.path = path; track.fileSize = fileSizeSafe(path); track.bitsPerSample = 24;
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
        cachePath = path; track = item.track; track.path = path; track.fileSize = fileSizeSafe(path); track.bitsPerSample = 24;
        return true;
    }
    bool success = false;
    try {
        const auto tmp = path + L".partial";
        const auto tmpManifest = manifest + L".partial";
        DeleteFileW(tmp.c_str()); DeleteFileW(tmpManifest.c_str());
        if (reportProgress) setConversionStatus(true, 0);
        uint32_t rate = 0, channels = 0, bits = 0; uint64_t samples = 0;
        if (!dvd_audio_flac::decode_to_flac(item.sourcePath.c_str(), item.subsong, tmp, job->aborter ? *job->aborter : abort,
            rate, channels, bits, samples, [this, reportProgress](uint32_t pct) { if (reportProgress) setConversionStatus(true, pct); }))
            throw std::runtime_error("DVD-Audio decoder produced no PCM samples");
        abort.check();
        if (rate == 0 || channels == 0 || samples == 0 || fileSizeSafe(tmp) <= 64 ||
            !validateFlacFile(tmp, rate, channels, bits, samples))
            throw std::runtime_error("generated FLAC failed structural validation");
        if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("unable to finalize DVD-A FLAC cache");
        std::ofstream mf(tmpManifest, std::ios::binary | std::ios::trunc);
        if (!mf) throw std::runtime_error("unable to create DVD-A cache manifest");
        const char* manifestDecoderVersion = detectedVersion.empty() ? "unknown" : detectedVersion.c_str();
        mf << "{\"cacheVersion\":" << kCacheFormatVersion
           << ",\"subsong\":" << item.subsong
           << ",\"sourceSize\":" << item.sourceSize
           << ",\"sourceWriteTime\":" << item.sourceWriteTime
           << ",\"decoderVersion\":\"" << xmlAttr(manifestDecoderVersion) << "\""
           << ",\"sampleRate\":" << rate << ",\"channels\":" << channels << ",\"bitsPerSample\":" << bits << ",\"samples\":" << samples << "}\n";
        mf.close();
        if (!MoveFileExW(tmpManifest.c_str(), manifest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("unable to finalize DVD-A cache manifest");
        if (reportProgress) setConversionStatus(false, 100);
        success = true;
        track = item.track; track.path = path; track.fileSize = fileSizeSafe(path); track.dsdRate = 0; track.channels = channels; track.bitsPerSample = bits; track.duration = rate ? static_cast<double>(samples) / rate : track.duration;
    } catch (const exception_aborted&) {
        if (reportProgress) setConversionStatus(false, 0); DeleteFileW((path + L".partial").c_str()); DeleteFileW((manifest + L".partial").c_str());
        networkLog("DVD-A FLAC conversion aborted: " + item.sourcePath);
    } catch (std::exception const& e) {
        if (reportProgress) setConversionStatus(false, 0); DeleteFileW((path + L".partial").c_str()); DeleteFileW((manifest + L".partial").c_str());
        setLastError(std::string("DVD-A FLAC conversion failed: ") + e.what());
    }
    {
        std::lock_guard<std::mutex> g(m_cacheMutex); job->success = success; job->done = true; m_cacheJobs.erase(key);
    }
    job->cv.notify_all();
    if (!success) {
        std::string detail;
        {
            std::lock_guard<std::mutex> g(m_diagMutex);
            detail = m_lastError;
        }
        if (detail.empty()) detail = "unknown conversion failure";
        networkLog("SACD DSF cache FAILED: id=" + std::to_string(item.id) +
            " title=\"" + item.track.title + "\" source=" + item.sourcePath +
            " subsong=" + std::to_string(item.subsong) + " detail=" + detail);
        return false;
    }
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
        const auto cur = m_itemIndex.find(itemId);
        if (cur == m_itemIndex.end() || cur->second >= m_items.size()) return;
        current = m_items[cur->second];
        if (!current.id || !current.albumId) return;
        const auto alb = m_albumIndex.find(current.albumId);
        if (alb != m_albumIndex.end() && alb->second < m_albums.size()) {
            const Album& album = m_albums[alb->second];
            for (size_t i = 0; i + 1 < album.itemIds.size(); ++i) {
                if (album.itemIds[i] == itemId) { nextId = album.itemIds[i + 1]; break; }
            }
        }
    }
    if (!nextId) return;
    Item next;
    {
        std::lock_guard<std::mutex> g(m_mutex);
        const auto nx = m_itemIndex.find(nextId);
        if (nx != m_itemIndex.end() && nx->second < m_items.size()) next = m_items[nx->second];
    }
    if (!next.id) return;

    const bool needsCache = next.dvdAudio || static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled) || _stricmp(next.sourceExt.c_str(), ".iso") == 0;
    if (!needsCache) {
        std::lock_guard<std::mutex> g(m_prefetchMutex);
        if (m_prefetchActiveCount == 0) {
            m_prefetchTitle = next.track.title;
            m_prefetchState = "NATIVE DSD / NO CACHE NEEDED";
            m_prefetchActive = false;
        }
        return;
    }
    const std::string key = (static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled) ? "dsp-" : "native-") + cacheKeyFor(next.sourcePath, next.subsong);
    {
        std::lock_guard<std::mutex> g(m_prefetchMutex);
        if (!m_prefetchKeys.insert(key).second) return;
        m_prefetchTitle = next.track.title;
        m_prefetchState = "PREPARING NEXT TRACK";
        ++m_prefetchActiveCount;
        m_prefetchActive = true;
        // Reap prefetch threads that already finished (same reason as the client threads).
        for (const auto& id : m_finishedPrefetchThreads) {
            const auto it = std::find_if(m_prefetchThreads.begin(), m_prefetchThreads.end(), [&](const std::thread& t) { return t.get_id() == id; });
            if (it == m_prefetchThreads.end()) continue;
            if (it->joinable()) it->join();
            m_prefetchThreads.erase(it);
        }
        m_finishedPrefetchThreads.clear();
        auto aborter = std::make_shared<abort_callback_impl>();
        m_prefetchAborters.push_back(aborter);
        try {
            m_prefetchThreads.emplace_back([this, next, aborter, key] {
                bool ok = false;
                try {
                    std::wstring path; DsdTrack track = next.track;
                    if (next.dvdAudio) ok = ensureCachedFlac(next, path, track, *aborter, false);
                    else if (static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled)) ok = ensureProcessedDsf(next, path, track, *aborter, false);
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
                    if (m_prefetchActiveCount) --m_prefetchActiveCount;
                    m_prefetchActive = m_prefetchActiveCount != 0;
                    auto it = std::find(m_prefetchAborters.begin(), m_prefetchAborters.end(), aborter);
                    if (it != m_prefetchAborters.end()) m_prefetchAborters.erase(it);
                    m_prefetchState = m_prefetchActive ? (ok ? "READY / OTHER PREFETCH ACTIVE" : "NOT READY / OTHER PREFETCH ACTIVE")
                                                       : (ok ? "READY" : "NOT READY");
                    m_finishedPrefetchThreads.push_back(std::this_thread::get_id());   // last locked step: safe to join
                }
            });
        } catch (...) {
            m_prefetchKeys.erase(key);
            if (m_prefetchActiveCount) --m_prefetchActiveCount;
            m_prefetchActive = m_prefetchActiveCount != 0;
            auto it = std::find(m_prefetchAborters.begin(), m_prefetchAborters.end(), aborter);
            if (it != m_prefetchAborters.end()) m_prefetchAborters.erase(it);
            m_prefetchState = "PREFETCH WORKER UNAVAILABLE";
            throw;
        }
    }
}

bool SacdDlnaServer::serveMedia(SOCKET s, uint32_t itemId, const std::string& requestLine,
                                const std::string& requestHeaders, const std::string& peerIp,
                                abort_callback_impl& aborter) {
    // Without a send timeout, send() blocks *inside the OS kernel* for as long as Windows keeps
    // retrying a dead TCP connection - which can be minutes - whenever a renderer disappears
    // mid-response without a clean close (exactly what happens on every track skip: VLC/UPnP
    // renderers routinely abandon the HTTP connection for the track they just left). While a
    // worker thread is stuck in that send(), it is still holding its StreamSlot below, so with a
    // small "Max streams" limit (default 2) it only takes two abandoned connections in a row -
    // completely normal while skipping through a playlist - to occupy every slot and make the
    // server answer 503 to every other track, including unrelated ones, until those blocked
    // sends eventually time out on their own (which can take a very long time). Setting this
    // before any response - including the 503 itself - is sent makes a stuck send() fail
    // promptly instead, so the StreamSlot destructor (and the "Max streams" slot it releases)
    // always runs quickly.
    const DWORD sendTimeoutMs = 15000;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendTimeoutMs), sizeof(sendTimeoutMs));

    Item item;
    bool found = false;
    {
        std::lock_guard<std::mutex> g(m_mutex);
        const auto it = m_itemIndex.find(itemId);
        if (it != m_itemIndex.end() && it->second < m_items.size() && m_items[it->second].id == itemId) {
            item = m_items[it->second];
            found = true;
        } else {
            // Media URLs are stable for the lifetime of the server.  Do not make
            // HTTP delivery depend on a metadb_handle still being valid: DVD-A
            // cache generation uses sourcePath/subsong and can serve a perfectly
            // valid cached FLAC even after the library handle has disappeared.
            for (const auto& candidate : m_items) {
                if (candidate.id == itemId) {
                    item = candidate;
                    found = true;
                    break;
                }
            }
        }
    }
    if (!found) {
        networkLog("MEDIA 404 id=" + std::to_string(itemId) + " reason=ITEM_NOT_FOUND");
        return false;
    }

    const bool isHead = requestLine.rfind("HEAD ", 0) == 0;

    std::wstring path;
    std::string ext;
    DsdTrack track = item.track;
    const bool dspEnabledForItem = static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled);
    const bool isoItem = _stricmp(item.sourceExt.c_str(), ".iso") == 0;
    if (item.dvdAudio) {
        setPreparingTrack(item, "FLAC", "DVD-Audio -> foobar2000 decoder -> FLAC/PCM -> DLNA", "DVD-A DECODE / CACHE");
    } else if (dspEnabledForItem || isoItem) {
        const std::string prepPipeline = dspEnabledForItem
            ? "PCM/DSD source -> DSD Processor -> DSF/DSD -> DLNA"
            : "SACD ISO -> foo_input_sacd -> DSF/DSD -> DLNA";
        const std::string prepConversion = dspEnabledForItem ? "DSP CONVERTING" : "SACD DECODE / CACHE";
        setPreparingTrack(item, "DSF", prepPipeline, prepConversion);
    } else {
        setPreparingTrack(item, item.sourceExt.empty() ? "UNKNOWN" : item.sourceExt.substr(1),
                          "Native DSD -> DLNA (no conversion)", "NO CONVERSION");
    }
    bool prepared = true;
    std::string prepareError;
    if (item.dvdAudio) {
        prepared = ensureCachedFlac(item, path, track, aborter);
        ext = ".flac";
        if (!prepared) prepareError = "DVDA_CACHE_FAILED";
    } else if (static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled)) {
        prepared = ensureProcessedDsf(item, path, track, aborter);
        ext = ".dsf";
        if (!prepared) prepareError = "DSP_DSF_FAILED";
    } else if (_stricmp(item.sourceExt.c_str(), ".iso") == 0) {
        prepared = ensureCached(item, path, track, aborter);
        ext = ".dsf";
        if (!prepared) prepareError = "SACD_DSF_FAILED";
    } else {
        // item.sourcePath is a foobar2000 path ("file://..."): convert before touching the disk.
        path = nativePathFromFb2k(item.sourcePath.c_str());
        prepared = !path.empty();
        ext = item.sourceExt;
        if (!prepared) prepareError = "INVALID_NATIVE_PATH";
    }

    if (!prepared) {
        networkLog("MEDIA 503 id=" + std::to_string(itemId) +
            " reason=" + prepareError + " title=\"" + item.track.title +
            "\" source=" + item.sourcePath + " subsong=" + std::to_string(item.subsong));
        std::string detail;
        {
            std::lock_guard<std::mutex> g(m_diagMutex);
            detail = m_lastError;
        }
        if (detail.empty()) detail = prepareError;
        const std::string body = "Media temporarily unavailable: " + prepareError + " | " + detail;
        const std::string hdr = "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\nRetry-After: 1\r\nConnection: close\r\n\r\n";
        sendAll(s, hdr.data(), hdr.size(), &aborter);
        sendAll(s, body.data(), body.size(), &aborter);
        return true;
    }

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        networkLog("MEDIA 503 id=" + std::to_string(itemId) + " reason=OUTPUT_FILE_OPEN_FAILED");
        const std::string body = "Media temporarily unavailable: OUTPUT_FILE_OPEN_FAILED";
        const std::string hdr = "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\nRetry-After: 1\r\nConnection: close\r\n\r\n";
        sendAll(s, hdr.data(), hdr.size(), &aborter);
        sendAll(s, body.data(), body.size(), &aborter);
        return true;
    }
    const uint64_t size = static_cast<uint64_t>(f.tellg());
    if (size == 0) {
        networkLog("MEDIA 503 id=" + std::to_string(itemId) + " reason=OUTPUT_FILE_EMPTY");
        const std::string body = "Media temporarily unavailable: OUTPUT_FILE_EMPTY";
        const std::string hdr = "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\nRetry-After: 1\r\nConnection: close\r\n\r\n";
        sendAll(s, hdr.data(), hdr.size(), &aborter); sendAll(s, body.data(), body.size(), &aborter);
        return true;
    }
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

    // Stream slot: taken atomically at the moment audio is about to be sent, i.e. AFTER any SACD/DSP
    // conversion, and released on every return path. A request that is only waiting for a conversion (for
    // example one abandoned when the user skipped tracks) therefore never holds a slot, while a request over
    // the limit is still answered 503 BEFORE any 200/206 header. The converted file stays cached, so the
    // renderer's retry starts at once. The limit is the "Max streams" preference, re-read on every request:
    // changes apply immediately and streams already running are never cut.
    struct StreamSlot {
        SacdDlnaServer* server;
        std::string ip;
        bool held = false;
        ~StreamSlot() {
            if (!held) return;
            server->m_clientRegistry.streamEnded(ip, clientreg::Clock::now());
            server->m_streamLimiter.release();
        }
    } slot{ this, peerIp };
    if (!isHead) {
        const uint32_t limit = sacd_dlna_max_streams();
        // VLC and hardware renderers can open a second HTTP connection for a Range
        // seek/prefetch while the same renderer is already streaming.  That is one
        // logical client stream, not a second audio stream.  Requiring another
        // limiter slot caused perfectly valid DSF seeks and DVD-A retries to receive
        // 503 when Max streams was already reached.
        const bool peerAlreadyStreaming = m_clientRegistry.hasActiveStream(peerIp);
        if (!peerAlreadyStreaming && !m_streamLimiter.tryAcquire(limit)) {
            const std::string hdr503 = "HTTP/1.1 503 Service Unavailable\r\nRetry-After: 1\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            sendAll(s, hdr503.data(), hdr503.size(), &aborter);
            networkLog("stream rejected: limit of " + std::to_string(limit) + " concurrent logical stream(s) reached (" + peerIp + ")");
            return true;
        }
        slot.held = !peerAlreadyStreaming;
        if (!peerAlreadyStreaming && isClientPeer(peerIp)) m_clientRegistry.streamStarted(peerIp, clientreg::Clock::now());
    }

    const std::string mime = chooseRendererMime(mimeForExtension(ext));
    std::string hdr = partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
    hdr += "Content-Type: " + mime + "\r\nContent-Length: " + std::to_string(length) + "\r\nAccept-Ranges: bytes\r\n";
    if (!_stricmp(ext.c_str(), ".flac")) {
        hdr += "transferMode.dlna.org: Streaming\r\ncontentFeatures.dlna.org: DLNA.ORG_PN=FLAC;DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=03700000000000000000000000000000\r\n";
    } else {
        hdr += "transferMode.dlna.org: Streaming\r\ncontentFeatures.dlna.org: DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000\r\n";
    }
    hdr += "Cache-Control: no-cache\r\n";
    if (partial) hdr += "Content-Range: bytes " + std::to_string(begin) + "-" + std::to_string(end) + "/" + std::to_string(size) + "\r\n";
    hdr += "Connection: close\r\n\r\n";
    sendAll(s, hdr.data(), hdr.size(), &aborter);

    if (requestLine.rfind("HEAD ", 0) == 0) return true;

    // SO_SNDBUF is only the Windows TCP socket buffer.  For DVD-A/FLAC the
    // important reserve is an application-level read-ahead: the renderer must
    // receive a useful amount of contiguous FLAC before the first send and the
    // cache file must not be confused with a pre-buffer that does not actually
    // contain any data in RAM.
    const uint32_t prebufferSeconds = std::clamp<uint32_t>(sacd_dlna_cfg::prebuffer_seconds.get(), 5, 60);
    uint64_t prebufferTarget = 0;
    if (sacd_dlna_cfg::stability_mode) {
        if (track.dsdRate) {
            prebufferTarget = static_cast<uint64_t>(track.dsdRate) / 4ULL * prebufferSeconds;
        } else if (item.dvdAudio && track.duration > 0.0) {
            // FLAC is variable bitrate, so derive the reserve from the actual
            // cached file rather than assuming PCM bytes/sec.
            const uint64_t fileBytesPerSecond = static_cast<uint64_t>(
                std::max<double>(1.0, static_cast<double>(length) / track.duration));
            prebufferTarget = fileBytesPerSecond * prebufferSeconds;
        } else {
            prebufferTarget = 4ULL * 1024ULL * 1024ULL;
        }
        prebufferTarget = std::clamp<uint64_t>(prebufferTarget, 512ULL * 1024ULL, 32ULL * 1024ULL * 1024ULL);
        if (length < prebufferTarget) prebufferTarget = length;
    }

    const int sndbuf = static_cast<int>(std::clamp<uint64_t>(
        std::max<uint64_t>(4ULL * 1024ULL * 1024ULL, prebufferTarget),
        4ULL * 1024ULL * 1024ULL, 32ULL * 1024ULL * 1024ULL));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sndbuf), sizeof(sndbuf));

    std::string sourceFormat = item.sourceExt.empty() ? "UNKNOWN" : item.sourceExt.substr(1);
    std::transform(sourceFormat.begin(), sourceFormat.end(), sourceFormat.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    std::string outputFormat = ext.empty() ? "UNKNOWN" : ext.substr(1);
    std::transform(outputFormat.begin(), outputFormat.end(), outputFormat.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const bool dspEnabled = static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled);
    const std::string pipeline = item.dvdAudio
        ? "DVD-Audio -> foo_input_dvda -> PCM -> FLAC -> DLNA"
        : (dspEnabled
            ? "PCM/DSD source -> DSD Processor -> DSF/DSD -> DLNA"
            : (_stricmp(item.sourceExt.c_str(), ".iso") == 0
                ? "SACD ISO -> foo_input_sacd -> DSF/DSD -> DLNA"
                : "Native DSD -> DLNA (no conversion)"));
    const std::string conversion = item.dvdAudio
        ? "DVD-A DECODE / FLAC CACHE"
        : (dspEnabled
            ? "DSP OUTPUT / CACHED"
            : (_stricmp(item.sourceExt.c_str(), ".iso") == 0 ? "SACD DECODE / CACHE" : "NO CONVERSION"));
    const uint64_t outputSize = fileSizeSafe(path);
    updateStreamStart(peerIp, track, sourceFormat, outputFormat, pipeline, conversion, item.sourceSize, outputSize,
                      item.sourceSampleRate, item.sourceChannels, item.sourceBitsPerSample);
    bool ok = false;
    try {
        prefetchNextTrack(itemId);
        // Real application-level read-ahead.  The old code only populated the
        // status counters; it did not actually buffer any FLAC data.  For a
        // renderer such as the SDX this can make the first FLAC requests race
        // with socket/file delivery.  Read the reserve before sending audio.
        std::vector<char> prebuffer;
        if (prebufferTarget) {
            prebuffer.resize(static_cast<size_t>(prebufferTarget));
            f.read(prebuffer.data(), static_cast<std::streamsize>(prebuffer.size()));
            const auto n = f.gcount();
            if (n <= 0) throw std::runtime_error("unable to fill DVD-A FLAC read-ahead buffer");
            prebuffer.resize(static_cast<size_t>(n));
        }
        {
            std::lock_guard<std::mutex> g(m_rateMutex);
            m_prebufferTargetBytes = prebufferTarget;
            m_prebufferBytes = static_cast<uint64_t>(prebuffer.size());
        }

        uint64_t remaining = length;
        size_t bufferedOffset = 0;
        while (remaining) {
            aborter.check();
            if (bufferedOffset < prebuffer.size()) {
                const size_t available = prebuffer.size() - bufferedOffset;
                const size_t n = static_cast<size_t>(std::min<uint64_t>(available, remaining));
                sendAll(s, prebuffer.data() + bufferedOffset, n, &aborter);
                updateStreamBytes(static_cast<uint64_t>(n));
                bufferedOffset += n;
                remaining -= static_cast<uint64_t>(n);
                {
                    std::lock_guard<std::mutex> g(m_rateMutex);
                    m_prebufferBytes = static_cast<uint64_t>(prebuffer.size() - bufferedOffset);
                }
                continue;
            }

            char buf[128 * 1024];
            const uint64_t maxRead = static_cast<uint64_t>(sizeof(buf));
            const size_t want = static_cast<size_t>(remaining < maxRead ? remaining : maxRead);
            f.read(buf, static_cast<std::streamsize>(want));
            const auto n = f.gcount();
            if (n <= 0) break;
            sendAll(s, buf, static_cast<size_t>(n), &aborter);
            updateStreamBytes(static_cast<uint64_t>(n));
            remaining -= static_cast<uint64_t>(n);
        }
        ok = remaining == 0;
    } catch (const exception_aborted&) {
        networkLog("stream aborted for " + peerIp + " / " + track.title);
    } catch (std::exception const& e) {
        setLastError(std::string("stream failed: ") + e.what());
    } catch (...) {
        setLastError("stream failed with an unknown exception");
    }
    updateStreamEnd();
    // The response headers are already on the wire, so this request is finished whatever
    // happened: returning false would make handleClient append a "404 Not Found" to a
    // half-sent audio stream.
    if (!ok) networkLog("stream ended early for " + peerIp + " / " + track.title);
    return true;
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
        const auto alb = m_albumIndex.find(albumId);
        if (alb != m_albumIndex.end() && alb->second < m_albums.size()) {
            const auto rep = m_itemIndex.find(m_albums[alb->second].representativeItemId);
            if (rep != m_itemIndex.end() && rep->second < m_items.size()) handle = m_items[rep->second].handle;
        }
    }
    if (!handle.is_valid()) return false;

    // File size/timestamp come from the metadb cache: no disk access for a cache key.
    uint64_t sourceSize = 0; int64_t sourceTime = 0;
    {
        const foobar2000_io::t_filestats stats = handle->get_filestats();
        sourceSize = stats.m_size != foobar2000_io::filesize_invalid ? static_cast<uint64_t>(stats.m_size) : 0;
        sourceTime = static_cast<int64_t>(stats.m_timestamp);
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
    m_finishedClientThreads.push_back(std::this_thread::get_id());   // last thing clientThread() does: safe to join
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
        const std::string localIp = m_localIp;
        if (!peerIp.empty() && peerIp != localIp && peerIp != "127.0.0.1") {
            m_networkPresence = true;
            m_remoteHttpSeen = true;
            ++m_remoteHttpRequests;
            m_lastRemotePeer = peerIp;
            m_ssdpLastPeer = peerIp;
            m_networkVisibility = "CONFIRMED / REMOTE HTTP";
        }
    }
    if (isClientPeer(peerIp)) m_clientRegistry.touch(peerIp, headerValueCI(req, "USER-AGENT"), clientreg::Clock::now());
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
        body += "<p>Music Library: " + std::string(st.sharingLibrary ? "SHARING" : "NOT SHARING") + " (" + std::to_string(st.sharedCount) + " shared tracks)</p>";
        body += "<p>Clients: <b>" + std::to_string(st.clientsTotal) + " total</b> = " + std::to_string(st.clientsActive) + " active + " + std::to_string(st.clientsIdle) +
            " idle | seen since start: " + std::to_string(st.clientsSeenSinceStart) +
            " | streams: <b>" + std::to_string(st.streamSlotsUsed) + " of " + std::to_string(st.streamLimit) + "</b> allowed (rejected: " + std::to_string(st.streamsRejected) + ")</p>";
        {
            const auto clients = m_clientRegistry.snapshot(clientreg::Clock::now());
            if (!clients.empty()) {
                body += "<table border=\"1\" cellpadding=\"3\" cellspacing=\"0\"><tr><th>Client</th><th>State</th><th>Streams now</th><th>Streams served</th><th>Requests</th><th>Silent for</th><th>User-Agent</th></tr>";
                for (const auto& c : clients) {
                    body += "<tr><td>" + xmlEscape(c.ip) + "</td><td>" + std::string(c.active ? "ACTIVE" : "idle") + "</td><td>" + std::to_string(c.activeStreams) +
                        "</td><td>" + std::to_string(c.streamsServed) + "</td><td>" + std::to_string(c.requests) + "</td><td>" + std::to_string(static_cast<long long>(c.silentFor.count())) +
                        " s</td><td>" + xmlEscape(c.userAgent) + "</td></tr>";
                }
                body += "</table>";
            }
        }
        body += "<p>Cache: " + std::to_string(st.cacheHits) + " hits / " + std::to_string(st.cacheMisses) + " misses / " + std::to_string(st.cacheBytes / 1048576.0) + " MB</p>";
        body += "<p>HTTP ready: <b>" + std::string(st.httpReady ? "YES" : "NO") + "</b> | SSDP ready: <b>" + std::string(st.ssdpReady ? "YES" : "NO") + "</b></p>";
        body += "<p>Advertised LOCATION: <code>http://" + xmlEscape(st.localIp.c_str()) + ":" + std::to_string(m_port) + "/device.xml</code></p>";
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
        const std::string actionName = lowerCopy(soapActionName(action, soap));
        const auto soapReply = [&](const std::string& actionResponse) {
            return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                "<s:Body>" + actionResponse + "</s:Body></s:Envelope>";
        };
        if (actionName == "getsystemupdateid") {
            sendXml(systemUpdateIdResponse()); return;
        }
        // Mandatory ContentDirectory:1 actions. Before, every request that was not GetSystemUpdateID was
        // answered as a Browse, so control points asking for these got a BrowseResponse and many gave up.
        if (actionName == "getsearchcapabilities") {   // no Search action: empty capability list
            sendXml(soapReply("<u:GetSearchCapabilitiesResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><SearchCaps></SearchCaps></u:GetSearchCapabilitiesResponse>")); return;
        }
        if (actionName == "getsortcapabilities") {     // SortCriteria is ignored: empty capability list
            sendXml(soapReply("<u:GetSortCapabilitiesResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><SortCaps></SortCaps></u:GetSortCapabilitiesResponse>")); return;
        }
        if (!actionName.empty() && actionName != "browse") {   // anything else (Search, CreateObject...): UPnP error 401
            const std::string fault = soapReply("<s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail>"
                "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\"><errorCode>401</errorCode><errorDescription>Invalid Action</errorDescription></UPnPError>"
                "</detail></s:Fault>");
            const std::string faultHeader = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: " + std::to_string(fault.size()) + "\r\nConnection: close\r\n\r\n";
            sendAll(s, faultHeader.data(), faultHeader.size(), &aborter); sendAll(s, fault.data(), fault.size(), &aborter);
            networkLog("ContentDirectory: unsupported action " + actionName);
            return;
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
        // Keep the advertised Source list synchronized with what the server can
        // actually publish.  FLAC was missing here even though DIDL-Lite and
        // the HTTP endpoint advertised audio/flac.  Strict renderers can reject
        // a resource when ConnectionManager::GetProtocolInfo does not list its
        // MIME type.
        const std::string body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><u:GetProtocolInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\"><Source>http-get:*:audio/flac:DLNA.ORG_PN=FLAC;DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=03700000000000000000000000000000,http-get:*:audio/x-flac:DLNA.ORG_PN=FLAC;DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=03700000000000000000000000000000,http-get:*:audio/dsf:DLNA.ORG_OP=01;DLNA.ORG_CI=0,http-get:*:audio/x-dsf:DLNA.ORG_OP=01;DLNA.ORG_CI=0,http-get:*:audio/dff:DLNA.ORG_OP=01;DLNA.ORG_CI=0,http-get:*:audio/x-dff:DLNA.ORG_OP=01;DLNA.ORG_CI=0</Source><Sink></Sink></u:GetProtocolInfoResponse></s:Body></s:Envelope>";
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
    if (m_httpListen == INVALID_SOCKET) {
        const int err = WSAGetLastError();
        m_running = false;
        { std::lock_guard<std::mutex> g(m_diagMutex); m_httpReady = false; m_networkDiagnostic = "HTTP socket creation failed: " + std::to_string(err); }
        return;
    }
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
        bool launched = false;
        {
            std::lock_guard<std::mutex> g(m_clientMutex);
            // Reap client threads that already finished. Every HTTP request runs on its own
            // thread; without this the std::thread objects (and their OS handles) piled up
            // until the server was stopped.
            for (const auto& id : m_finishedClientThreads) {
                const auto it = std::find_if(m_clientThreads.begin(), m_clientThreads.end(), [&](const std::thread& t) { return t.get_id() == id; });
                if (it == m_clientThreads.end()) continue;
                if (it->joinable()) it->join();
                m_clientThreads.erase(it);
            }
            m_finishedClientThreads.clear();
            m_clients.push_back({ clientSocket, aborter });
            try {
                m_clientThreads.emplace_back([this, clientSocket, aborter] { clientThread(clientSocket, aborter); });
                launched = true;
            } catch (...) {
                m_clients.pop_back();
            }
        }
        if (!launched) {
            aborter->set();
            shutdown(clientSocket, SD_BOTH);
            closesocket(clientSocket);
            networkLog("unable to launch HTTP client worker thread");
        }
    }
    { std::lock_guard<std::mutex> g(m_diagMutex); m_httpReady = false; }
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
        const std::string contentUsn = std::string(kUuid) + "::urn:schemas-upnp-org:service:ContentDirectory:1";
        const std::string connectionUsn = std::string(kUuid) + "::urn:schemas-upnp-org:service:ConnectionManager:1";
        const char* nts[] = { "upnp:rootdevice", kUuid, "urn:schemas-upnp-org:device:MediaServer:1", "urn:schemas-upnp-org:service:ContentDirectory:1", "urn:schemas-upnp-org:service:ConnectionManager:1" };
        const char* usns[] = { uuidUsn.c_str(), kUuid, deviceUsn.c_str(), contentUsn.c_str(), connectionUsn.c_str() };
        for (int i = 0; i < 5; ++i) {
            const std::string msg = "NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nCACHE-CONTROL: max-age=1800\r\nLOCATION: " + location +
                "\r\nNT: " + nts[i] + "\r\nNTS: ssdp:alive\r\nSERVER: Windows/10 UPnP/1.1 foo_sacd_dlna/" + std::string(kVersion) + "\r\nUSN: " + usns[i] + "\r\n\r\n";
            if (sendMulticast(msg)) { std::lock_guard<std::mutex> g(m_diagMutex); ++m_ssdpAliveSent; }
        }
        networkLog("SSDP NOTIFY ssdp:alive sent to 239.255.255.250:1900");
    };
    auto notifyByeBye = [&]() {
        const std::string contentUsn = std::string(kUuid) + "::urn:schemas-upnp-org:service:ContentDirectory:1";
        const std::string connectionUsn = std::string(kUuid) + "::urn:schemas-upnp-org:service:ConnectionManager:1";
        const char* nts[] = { "upnp:rootdevice", kUuid, "urn:schemas-upnp-org:device:MediaServer:1", "urn:schemas-upnp-org:service:ContentDirectory:1", "urn:schemas-upnp-org:service:ConnectionManager:1" };
        const char* usns[] = { uuidUsn.c_str(), kUuid, deviceUsn.c_str(), contentUsn.c_str(), connectionUsn.c_str() };
        for (int i = 0; i < 5; ++i) {
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
        if (st != "ssdp:all" && st != "upnp:rootdevice" && st != kUuid &&
            st != "urn:schemas-upnp-org:device:MediaServer:1" &&
            st != "urn:schemas-upnp-org:service:ContentDirectory:1" &&
            st != "urn:schemas-upnp-org:service:ConnectionManager:1") continue;
        if (externalPeer) {
            std::lock_guard<std::mutex> g(m_diagMutex);
            m_networkPresence = true;
            m_remoteSsdpSeen = true;
            ++m_remoteSsdpSearches;
            m_lastRemotePeer = peerIp;
            m_ssdpLastPeer = peerIp;
            m_networkVisibility = "CONFIRMED / REMOTE SSDP M-SEARCH";
        }
        const std::string contentUsn = std::string(kUuid) + "::urn:schemas-upnp-org:service:ContentDirectory:1";
        const std::string connectionUsn = std::string(kUuid) + "::urn:schemas-upnp-org:service:ConnectionManager:1";
        const std::string usn = (st == "upnp:rootdevice") ? uuidUsn :
            (st == kUuid ? std::string(kUuid) :
            (st == "urn:schemas-upnp-org:service:ContentDirectory:1" ? contentUsn :
            (st == "urn:schemas-upnp-org:service:ConnectionManager:1" ? connectionUsn : deviceUsn)));
        const std::string response = "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=1800\r\nEXT:\r\nLOCATION: " + location +
            "\r\nSERVER: Windows/10 UPnP/1.1 foo_sacd_dlna/" + std::string(kVersion) + "\r\nST: " + st + "\r\nUSN: " + usn + "\r\n\r\n";
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
    {
        std::lock_guard<std::mutex> g(m_artMutex);
        m_artCache.clear();
    }
    {
        std::lock_guard<std::mutex> g(m_cacheStatsMutex);
        m_cachedCacheBytes = 0;
        m_cacheStatsTick = std::chrono::steady_clock::now();
    }
}

void SacdDlnaServer::clear_shared_library() {
    std::lock_guard<std::mutex> g(m_mutex);
    m_items.clear(); m_artists.clear(); m_albums.clear(); m_genres.clear(); m_folders.clear(); m_playlists.clear();
    m_albumsByTitle.clear(); m_allTrackIds.clear(); m_folderRootId = 0;
    m_itemIndex.clear(); m_artistIndex.clear(); m_albumIndex.clear(); m_genreIndex.clear(); m_folderIndex.clear();
    m_sharedCount = 0; m_sharingLibrary = false; ++m_updateId;
    { std::lock_guard<std::mutex> a(m_artMutex); m_artCache.clear(); }
}

void SacdDlnaServer::publish(const metadb_handle_list& items) {
    const auto t0 = std::chrono::steady_clock::now();
    const bool dspEnabled = static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled);
    const bool dspInstalled = !dspEnabled || DsdProcessorBridge::installed();   // evaluated once, not per track
    const std::unordered_set<std::string> sharedFormats = parseSharedFormats(std::string(sacd_dlna_cfg::shared_formats.get()));   // parsed once, not per track

    // Media IDs must remain stable not only across publish()/refresh(), but also
    // across a foobar2000/component restart. VLC may keep DIDL resources briefly
    // and reuse /media/<id> after SSDP rediscovery. A per-process m_nextId scheme
    // therefore cannot be used here: the same ID can refer to a different track
    // after restart, or disappear entirely.
    //
    // The playable identity is the normalized foobar path + subsong index. We
    // derive a deterministic non-zero 32-bit ID from that key and resolve the
    // extremely unlikely hash collision deterministically within this publish.
    std::unordered_set<uint32_t> assignedMediaIds;
    assignedMediaIds.reserve(items.get_count() * 2 + 1);
    auto stableMediaId = [&](const std::string& key) -> uint32_t {
        uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : key) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        uint32_t id = static_cast<uint32_t>((h ^ (h >> 32)) & 0x7fffffffU);
        if (id == 0) id = 1;
        for (uint32_t salt = 0; assignedMediaIds.find(id) != assignedMediaIds.end(); ++salt) {
            uint64_t x = h + 0x9e3779b97f4a7c15ULL * (static_cast<uint64_t>(salt) + 1ULL);
            x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
            x ^= x >> 27; x *= 0x94d049bb133111ebULL;
            x ^= x >> 31;
            id = static_cast<uint32_t>(x & 0x7fffffffU);
            if (id == 0) id = 1;
        }
        assignedMediaIds.insert(id);
        return id;
    };

    std::vector<Item> newItems; newItems.reserve(items.get_count());
    std::vector<libindex::Track> indexTracks; indexTracks.reserve(items.get_count());   // parallel to newItems
    for (size_t i = 0; i < items.get_count(); ++i) {
        const auto& handle = items[i]; const char* path = handle->get_path(); if (!path || !*path) continue;
        const char* ext = strrchr(path, '.'); if (!ext) continue;
        const std::string normalizedExt = lowerCopy(ext);
        const bool isDvdCandidate = normalizedExt == ".iso" || normalizedExt == ".aob" || normalizedExt == ".ifo" || normalizedExt == ".mlp" || normalizedExt == ".thd" || normalizedExt == ".truehd";
        const bool isDvdAudio = isDvdCandidate && isDvdAudioInput(path);
        const bool isDsd = normalizedExt == ".iso" || normalizedExt == ".dsf" || normalizedExt == ".dff";
        const bool isSharedFormat = sharedFormats.count(normalizedExt) != 0;
        if (!isSharedFormat) continue;
        if (isDsd && !isDvdAudio) {
            // SACD ISO is indexed as DSD content and is advertised as the generated
            // DSF resource; the ISO container itself is never sent to the renderer.
        } else if (dspEnabled) {
            // Non-DSD formats may be sent natively or passed through DSD Processor,
            // depending on the configured DSP mode. They are still selectable by filter.
        }
        if (!dspInstalled && !isDsd && !isDvdAudio) continue;
        Item x; x.sourcePath = path; x.sourceExt = lowerCopy(ext); x.subsong = handle->get_subsong_index(); x.dvdAudio = isDvdAudio; x.handle = handle;
        {
            std::string key = lowerCopy(x.sourcePath);
            key.push_back('\x1f');
            key += std::to_string(x.subsong);
            x.id = stableMediaId(key);
        }

        // File size and timestamp come from foobar2000's metadb cache, so indexing a
        // library does not stat thousands of files (this runs on the main thread).
        const foobar2000_io::t_filestats stats = handle->get_filestats();
        x.sourceSize = stats.m_size != foobar2000_io::filesize_invalid ? static_cast<uint64_t>(stats.m_size) : 0;
        x.sourceWriteTime = static_cast<int64_t>(stats.m_timestamp);
        pfc::string8 nativeUtf8;
        const bool isLocalFile = foobar2000_io::extract_native_path(path, nativeUtf8);
        const std::wstring nativePath = isLocalFile ? utf8ToWide(nativeUtf8.c_str()) : std::wstring();
        if ((x.sourceSize == 0 || x.sourceWriteTime == 0) && !nativePath.empty()) {   // stats not known yet: one direct stat
            std::error_code ec; const fs::path filePath(nativePath);
            const auto sz = fs::file_size(filePath, ec); if (!ec) x.sourceSize = static_cast<uint64_t>(sz);
            std::error_code ec2; const auto wt = fs::last_write_time(filePath, ec2);
            if (!ec2) x.sourceWriteTime = static_cast<int64_t>(wt.time_since_epoch().count());
        }

        try {
            // Tags and technical info also come from the metadb cache. Only when a track
            // is not in the metadb yet is the file opened (previously EVERY track was
            // opened, which froze the UI for large or network libraries).
            file_info_impl fallbackInfo;
            const file_info* infoPtr = nullptr;
            metadb_info_container::ptr infoRef;
            if (handle->get_info_ref(infoRef) && infoRef.is_valid()) {
                infoPtr = &infoRef->info();
            } else {
                abort_callback_dummy abort; service_ptr_t<input_info_reader> infoReader;
                input_entry::g_open_for_info_read(infoReader, nullptr, path, abort);
                infoReader->get_info(x.subsong, fallbackInfo, abort);
                infoPtr = &fallbackInfo;
            }
            const file_info& info = *infoPtr;
            auto meta = [&](const char* key) { const char* v = info.meta_get(key, 0); return v ? std::string(v) : std::string{}; };
            auto metaFirst = [&](std::initializer_list<const char*> names) { for (auto n : names) { auto v = meta(n); if (!v.empty()) return v; } return std::string{}; };
            x.track.title = meta("title"); x.track.artist = meta("artist"); x.track.album = meta("album"); x.track.albumArtist = metaFirst({"album artist", "albumartist"});
            x.track.genre = meta("genre"); x.track.date = metaFirst({"date", "year"}); x.track.composer = meta("composer"); x.track.publisher = metaFirst({"publisher", "label"}); x.track.comment = metaFirst({"comment", "comments"}); x.track.trackNumber = metaFirst({"tracknumber", "track number", "track"}); x.track.discNumber = metaFirst({"discnumber", "disc number", "disc"}); x.track.totalTracks = metaFirst({"totaltracks", "total tracks", "tracktotal"}); x.track.totalDiscs = metaFirst({"totaldiscs", "total discs"});
            if (x.track.title.empty()) x.track.title = "Track " + std::to_string(x.subsong + 1);
            if (x.track.artist.empty()) x.track.artist = "Unknown Artist";
            if (x.track.album.empty()) x.track.album = "Unknown Album";
            x.track.duration = info.get_length();
            x.sourceSampleRate = static_cast<uint32_t>(std::max<t_int64>(0, info.info_get_int("samplerate")));
            x.sourceChannels = static_cast<uint32_t>(std::max<t_int64>(1, info.info_get_int("channels")));
            x.sourceBitsPerSample = static_cast<uint32_t>(std::max<t_int64>(0, info.info_get_int("bitspersample")));
            x.track.dsdRate = x.sourceSampleRate;
            x.track.channels = x.sourceChannels;
            x.track.bitsPerSample = x.sourceBitsPerSample ? x.sourceBitsPerSample : 1;
            if (x.track.dsdRate != 2822400 && x.track.dsdRate != 5644800 && x.track.dsdRate != 11289600) x.track.dsdRate = 0;
            if (x.sourceExt == ".dsf" || x.sourceExt == ".dff") x.track.fileSize = x.sourceSize;
            x.track.path = x.sourceExt == ".iso" ? std::wstring{} : nativePath;
            libindex::Track it;
            it.id = x.id; it.title = x.track.title; it.artist = x.track.artist; it.albumArtist = x.track.albumArtist;
            it.album = x.track.album; it.genre = x.track.genre; it.trackNumber = x.track.trackNumber; it.discNumber = x.track.discNumber;
            if (isLocalFile) it.nativePath = nativeUtf8.c_str();
            newItems.push_back(std::move(x));
            indexTracks.push_back(std::move(it));
        } catch (std::exception const& e) { setLastError(std::string("unable to index ") + path + ": " + e.what()); }
    }

    // Artists, Albums, Genres, Folders and All Tracks (grouping, sorting and ids live in library_index.h,
    // which has its own unit test under tests/library_index_selftest).
    libindex::NextIds nextIds;
    nextIds.artist = m_nextArtistId; nextIds.album = m_nextAlbumId; nextIds.genre = m_nextGenreId; nextIds.folder = m_nextFolderId;
    libindex::Index index = libindex::build(indexTracks, nextIds);
    m_nextArtistId = nextIds.artist; m_nextAlbumId = nextIds.album; m_nextGenreId = nextIds.genre; m_nextFolderId = nextIds.folder;
    for (size_t i = 0; i < newItems.size(); ++i) { newItems[i].artistId = index.trackArtistId[i]; newItems[i].albumId = index.trackAlbumId[i]; }

    // Snapshot foobar2000 playlists and map their entries to the currently shared
    // library items. Playlists are exposed as read-only UPnP containers; the renderer
    // receives the same DSD/native resources as the library tree.
    std::vector<Playlist> newPlaylists;
    const auto playlistCount = playlist_manager::get()->get_playlist_count();
    newPlaylists.reserve(playlistCount);
    // Path (ASCII-lower-cased) -> positions in newItems, for the playlist matching below.
    std::unordered_map<std::string, std::vector<size_t>> itemsByPath;
    itemsByPath.reserve(newItems.size());
    for (size_t k = 0; k < newItems.size(); ++k) itemsByPath[lowerCopy(newItems[k].sourcePath)].push_back(k);
    for (t_size pi = 0; pi < playlistCount; ++pi) {
        metadb_handle_list playlistItems;
        playlist_manager::get()->playlist_get_all_items(pi, playlistItems);

        // Keep the real foobar2000 playlist name.  In SDK 2025-03-07 the API is
        // playlist_get_name(), not get_playlist_name().
        pfc::string8 playlistName;
        playlist_manager::get()->playlist_get_name(pi, playlistName);

        Playlist pl;
        pl.id = m_nextPlaylistId++;
        pl.name = playlistName.is_empty() ? ("Playlist " + std::to_string(static_cast<unsigned>(pi + 1))) : playlistName.c_str();

        // Match playlist entries to the shared library.  A playlist entry is a
        // metadb_handle, and for multitrack containers (notably SACD ISO) the
        // subsong is part of the identity.  Some fb2k/library paths can differ in
        // representation, however, so use several safe fallbacks instead of
        // silently producing an empty playlist.
        for (size_t li = 0; li < playlistItems.get_count(); ++li) {
            const auto& ph = playlistItems[li];
            if (!ph.is_valid()) continue;
            const char* ppath = ph->get_path();
            if (!ppath || !*ppath) continue;
            const t_uint32 psub = ph->get_subsong_index();

            // Only items sharing this path can match, so look them up in the path index instead of
            // scanning the whole library (the previous code did up to three full scans, each with a
            // Unicode-aware path_compare, for EVERY playlist entry, on the main thread).
            // path_compare() below still decides every match; the index only narrows the search.
            const auto candidatesIt = itemsByPath.find(lowerCopy(ppath));
            if (candidatesIt == itemsByPath.end()) continue;   // not a shared file (e.g. its format is filtered out)
            const std::vector<size_t>& candidates = candidatesIt->second;
            const Item* match = nullptr;

            // 1) Exact playable location: path + subsong.
            for (const size_t ci : candidates) {
                const Item& item = newItems[ci];
                if (item.subsong == psub && metadb::path_compare(item.sourcePath.c_str(), ppath) == 0) { match = &item; break; }
            }

            // 2) Same path and same title/track number. This handles cases where a playlist
            // provider recreates a handle with a different subsong index while retaining the
            // actual SACD track metadata.
            if (!match) {
                pfc::string8 pTitle, pTrackNo;
                metadb_info_container::ptr pref;
                if (ph->get_info_ref(pref) && pref.is_valid()) {
                    const file_info& pinfo = pref->info();
                    const char* t = pinfo.meta_get("title", 0);
                    const char* n = pinfo.meta_get("tracknumber", 0);
                    if (t) pTitle = t;
                    if (n) pTrackNo = n;
                }
                if (!pTitle.is_empty() || !pTrackNo.is_empty()) {
                    for (const size_t ci : candidates) {
                        const Item& item = newItems[ci];
                        if (metadb::path_compare(item.sourcePath.c_str(), ppath) != 0) continue;
                        const bool titleOK = pTitle.is_empty() ||
                            stricmp_utf8(item.track.title.c_str(), pTitle.c_str()) == 0;
                        const bool trackOK = pTrackNo.is_empty() ||
                            stricmp_utf8(item.track.trackNumber.c_str(), pTrackNo.c_str()) == 0;
                        if (titleOK && trackOK) { match = &item; break; }
                    }
                }
            }

            // 3) Last safe fallback: if the path exists only once in the shared library, use that
            // item even if the playlist subsong metadata is stale.
            if (!match && candidates.size() == 1) {
                const Item& only = newItems[candidates[0]];
                if (metadb::path_compare(only.sourcePath.c_str(), ppath) == 0) match = &only;
            }

            if (match) pl.itemIds.push_back(match->id);
        }

        // Expose every playlist, even when none of its entries pass the current
        // Shared Formats filter. This makes the playlist visible in UPnP; entries
        // that are not shared are simply omitted from that playlist.
        newPlaylists.push_back(std::move(pl));
    }

    {
        std::lock_guard<std::mutex> g(m_mutex);
        m_items = std::move(newItems);
        m_artists = std::move(index.artists); m_albums = std::move(index.albums);
        m_genres = std::move(index.genres); m_folders = std::move(index.folders); m_playlists = std::move(newPlaylists);
        m_albumsByTitle = std::move(index.albumsByTitle); m_allTrackIds = std::move(index.allTracks);
        m_folderRootId = index.folderRoot;
        m_artistIndex = std::move(index.artistIndex); m_albumIndex = std::move(index.albumIndex);
        m_genreIndex = std::move(index.genreIndex); m_folderIndex = std::move(index.folderIndex);
        m_itemIndex.clear(); m_itemIndex.reserve(m_items.size());
        for (size_t i = 0; i < m_items.size(); ++i) m_itemIndex.emplace(m_items[i].id, i);
        m_sharedCount = m_items.size(); ++m_updateId;
    }
    { std::lock_guard<std::mutex> g(m_artMutex); m_artCache.clear(); }
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    FB2K_console_formatter() << "SACD DLNA: indexed " << static_cast<unsigned>(m_sharedCount.load()) << " DSD tracks in " << static_cast<unsigned>(elapsedMs) << " ms, UpdateID " << m_updateId;
}

void SacdDlnaServer::share_music_library() {
    m_libraryRefreshPending = false;
    // DVD-Audio is decoded by foo_input_dvda; SACD still uses foo_input_sacd.
    // Sharing is allowed when either decoder is installed.
    if (!sacd_plugin_installed() && !dvda_plugin_installed()) {
        popup_message::g_show("Install foo_input_sacd or DVD-Audio Decoder (foo_input_dvda) before sharing the Music Library.", "SACD DLNA");
        return;
    }
    if (!library_manager::get()->is_library_enabled()) {
        library_manager::get()->show_preferences();
        console::print("SACD DLNA: Media Library is not enabled; opened foobar2000 Music Library preferences");
        return;
    }
    pfc::list_t<metadb_handle_ptr> all; library_manager::get()->get_all_items(all);
    metadb_handle_list dsd;
    const std::unordered_set<std::string> sharedFormats = parseSharedFormats(std::string(sacd_dlna_cfg::shared_formats.get()));   // parsed once, not per track
    for (size_t i = 0; i < all.get_count(); ++i) {
        const char* path = all[i]->get_path(); if (!path) continue; const char* ext = strrchr(path, '.'); if (!ext) continue;
        const std::string normalizedExt = lowerCopy(ext);
        if (sharedFormats.count(normalizedExt) != 0) dsd += all[i];
    }
    publish(dsd); m_sharingLibrary = true;
    console::printf("SACD DLNA: Music Library SHARING / %u track(s) (per Shared formats filter: %s)",
        static_cast<unsigned>(m_sharedCount.load()), sacd_dlna_cfg::shared_formats.get().c_str());
}

void SacdDlnaServer::request_library_refresh() {
    if (!m_sharingLibrary.load() || !m_running.load()) return;
    bool expected = false;
    if (m_libraryRefreshPending.compare_exchange_strong(expected, true)) main_thread_callback_spawn<library_refresh_callback>();
}
