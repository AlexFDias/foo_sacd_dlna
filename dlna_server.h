#pragma once

#include "stdafx.h"
#include "dsf_writer.h"
#include "status.h"
#include "library_index.h"
#include "client_registry.h"
#include "dvd_audio_flac.h"

class SacdDlnaServer {
public:
    static SacdDlnaServer& instance();

    void start();
    void stop();
    void set_enabled(bool enabled);
    bool is_running() const noexcept { return m_running.load(); }

    void publish(const metadb_handle_list& items);
    void share_music_library();
    void clear_shared_library();
    void request_library_refresh();
    void clear_persistent_cache();
    bool run_network_diagnostics();
    uint64_t persistent_cache_bytes() const;
    uint64_t cached_persistent_cache_bytes() const;
    size_t shared_count() const;
    SacdDlnaStatus get_status() const;
    void record_error(const std::string& message);

private:
    SacdDlnaServer() = default;
    ~SacdDlnaServer() = default;
    SacdDlnaServer(const SacdDlnaServer&) = delete;
    SacdDlnaServer& operator=(const SacdDlnaServer&) = delete;

    struct Playlist {
        uint32_t id = 0;
        std::string name;
        std::vector<uint32_t> itemIds;
    };

    struct Item {
        uint32_t id = 0;
        std::string sourcePath;
        std::string sourceExt;
        uint64_t sourceSize = 0;
        uint32_t sourceSampleRate = 0;
        uint32_t sourceChannels = 0;
        uint32_t sourceBitsPerSample = 0;
        int64_t sourceWriteTime = 0;
        t_uint32 subsong = 0;
        bool dvdAudio = false;
        metadb_handle_ptr handle;
        DsdTrack track;
        std::wstring cachePath;
        uint32_t artistId = 0;
        uint32_t albumId = 0;
    };

    // The browse tree (Artists, Albums, Genres, Folders, All Tracks) is built by library_index.h.
    using Artist = libindex::Artist;
    using Album = libindex::Album;
    using Genre = libindex::Genre;
    using Folder = libindex::Folder;

    struct CacheJob {
        std::condition_variable cv;
        bool done = false;
        bool success = false;
        std::shared_ptr<abort_callback_impl> aborter = std::make_shared<abort_callback_impl>();
    };

    struct ClientState {
        SOCKET socket = INVALID_SOCKET;
        std::shared_ptr<abort_callback_impl> aborter;
    };

    struct ArtCache {
        std::string mime;
        std::vector<uint8_t> bytes;
        std::wstring path;
    };

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_wsaStarted{false};
    mutable std::mutex m_lifecycleMutex;
    std::atomic<size_t> m_sharedCount{0};
    std::atomic<bool> m_sharingLibrary{false};
    std::atomic<bool> m_libraryRefreshPending{false};
    std::thread m_httpThread;
    std::thread m_ssdpThread;
    SOCKET m_httpListen = INVALID_SOCKET;
    mutable std::mutex m_mutex;
    std::vector<Item> m_items;
    std::vector<Artist> m_artists;
    std::vector<Album> m_albums;
    std::vector<Genre> m_genres;
    std::vector<Folder> m_folders;
    std::vector<Playlist> m_playlists;
    std::vector<uint32_t> m_albumsByTitle;   // album ids, sorted by title
    std::vector<uint32_t> m_allTrackIds;     // track ids, sorted by title
    uint32_t m_folderRootId = 0;
    uint32_t m_nextPlaylistId = 1;             // folder shown as "Folders"
    // id -> position lookups, rebuilt by publish() (a linear scan per child made large Browse pages O(n^2))
    std::unordered_map<uint32_t, size_t> m_itemIndex, m_artistIndex, m_albumIndex, m_genreIndex, m_folderIndex;
    uint32_t m_nextArtistId = 1;
    uint32_t m_nextAlbumId = 1;
    uint32_t m_nextGenreId = 1;
    uint32_t m_nextFolderId = 1;
    uint16_t m_port = 8192;
    uint32_t m_updateId = 1;
    std::string m_lastHttpRequest;

    clientreg::Registry m_clientRegistry;        // clients seen, streaming and idle (own lock)
    clientreg::StreamLimiter m_streamLimiter;    // caps simultaneous audio streams (lock-free)

    std::mutex m_clientMutex;
    std::vector<ClientState> m_clients;
    std::vector<std::thread> m_clientThreads;
    std::vector<std::thread::id> m_finishedClientThreads;   // guarded by m_clientMutex; reaped by httpLoop

    mutable std::mutex m_prefetchMutex;
    std::vector<std::shared_ptr<abort_callback_impl>> m_prefetchAborters;
    std::vector<std::thread> m_prefetchThreads;
    std::vector<std::thread::id> m_finishedPrefetchThreads; // guarded by m_prefetchMutex; reaped by prefetchNextTrack
    std::set<std::string> m_prefetchKeys;

    std::mutex m_cacheMutex;
    std::map<std::string, std::shared_ptr<CacheJob>> m_cacheJobs;

    std::mutex m_artMutex;
    std::map<uint32_t, ArtCache> m_artCache;

    std::shared_ptr<class library_tracker> m_libraryTracker;

    std::string m_sdxProtocolInfo;
    std::string m_prefetchTitle;
    std::string m_prefetchState;
    bool m_prefetchActive = false;
    size_t m_prefetchActiveCount = 0;
    std::string m_sdxConnectionManagerControl;
    std::string m_sdxModelNumber;

    mutable std::mutex m_diagMutex;
    bool m_httpReady = false;
    bool m_ssdpReady = false;
    bool m_httpSelfTestOk = false;
    bool m_ssdpProbeOk = false;
    bool m_ssdpNotifyLoopbackOk = false;
    bool m_networkPresence = false;
    uint32_t m_ssdpAliveSent = 0;
    uint32_t m_ssdpDiscoverSent = 0;
    uint32_t m_ssdpMSearchReceived = 0;
    uint32_t m_ssdpResponsesSent = 0;
    uint32_t m_ssdpDiscoverResponses = 0;
    uint32_t m_httpRequests = 0;
    uint32_t m_remoteHttpRequests = 0;
    uint32_t m_remoteSsdpSearches = 0;
    std::string m_localIp;
    std::string m_lastRemotePeer;
    bool m_remoteHttpSeen = false;
    bool m_remoteSsdpSeen = false;
    std::string m_networkVisibility;
    std::string m_ssdpLastPeer;
    std::string m_networkDiagnostic;
    std::string m_lastError;

    void stopUnlocked();
    void httpLoop();
    void ssdpLoop();
    void clientThread(SOCKET s, std::shared_ptr<abort_callback_impl> aborter);
    void handleClient(SOCKET s, abort_callback_impl& aborter);
    void closeClientState(SOCKET s);
    // True for a real remote client: not empty, not loopback and not this machine's own address (self-test).
    bool isClientPeer(const std::string& ip) const;

    std::string makeDeviceXml() const;
    std::string makeContentDirectoryScpd() const;
    std::string makeConnectionManagerScpd() const;
    std::string browseDidl(const std::string& objectId,
                           bool metadataOnly,
                           unsigned startingIndex,
                           unsigned requestedCount,
                           unsigned& numberReturned,
                           unsigned& totalMatches) const;
    std::string browseResponse(const std::string& objectId,
                               const std::string& browseFlag,
                               unsigned startingIndex,
                               unsigned requestedCount,
                               const std::string& filter,
                               const std::string& sortCriteria) const;
    std::string systemUpdateIdResponse() const;

    bool serveMedia(SOCKET s, uint32_t itemId, const std::string& requestLine,
                    const std::string& requestHeaders, const std::string& peerIp,
                    abort_callback_impl& aborter);
    bool serveAlbumArt(SOCKET s, uint32_t albumId, const std::string& requestLine,
                       abort_callback_impl& aborter);

    std::string localAddress() const;
    std::wstring cacheFolder() const;
    bool ensureCached(const Item& item, std::wstring& cachePath, DsdTrack& track,
                      abort_callback& abort, bool reportProgress = true);
    bool ensureCachedFlac(const Item& item, std::wstring& cachePath, DsdTrack& track,
                          abort_callback& abort, bool reportProgress = true);
    static bool isDvdAudioInput(const char* path);
    bool ensureProcessedDsf(const Item& item, std::wstring& cachePath, DsdTrack& track,
                            abort_callback& abort, bool reportProgress = true);
    void prefetchNextTrack(uint32_t itemId);
    void clearPrefetchThreads();
    void setConversionStatus(bool active, uint32_t percent);
    void setPreparingTrack(const Item& item, const std::string& outputFormat, const std::string& pipelineState, const std::string& conversionState);

    void refreshLibraryNow();

    static std::string xmlEscape(const std::string& s);
    static std::string urlPathDecode(const std::string& s);
    static std::string normalizeKey(const std::string& s);
    static std::string mimeForExtension(const std::string& ext);
    static bool formatAllowed(const std::string& ext);
    static std::string detectImageMime(const void* data, size_t size);
    static std::string didlProtocolInfo(const std::string& mime);
    static std::string firstXmlTagText(const std::string& xml, const std::string& localName);
    static std::string extractSoapArg(const std::string& soap, const char* localName, const std::string& fallback = {});
    static unsigned extractSoapUint(const std::string& soap, const char* localName, unsigned fallback);
    static std::string xmlLocalTagValueCI(const std::string& xml, const char* localName);
    static std::string resolveUrl(const std::string& baseUrl, const std::string& relative);
    static std::string extractConnectionManagerControl(const std::string& deviceXml, const std::string& baseUrl);
    static std::string extractProtocolList(const std::string& soap, const char* tag);
    static bool protocolListSupports(const std::string& protocols, const std::string& mime);
    std::string chooseRendererMime(const std::string& defaultMime) const;
    std::string chooseRendererProtocolInfo(const std::string& defaultMime) const;
    static bool readDsfHeader(const std::wstring& path, uint32_t& rate,
                              uint32_t& channels, uint64_t& samplesPerChannel,
                              uint64_t& fileSize);

    void updateStreamStart(const std::string& peerIp, const DsdTrack& track, const std::string& sourceFormat,
                           const std::string& outputFormat, const std::string& pipelineState,
                           const std::string& conversionState, uint64_t sourceSize, uint64_t outputSize,
                           uint32_t sourceSampleRate, uint32_t sourceChannels, uint32_t sourceBitsPerSample);
    void updateStreamBytes(uint64_t bytes);
    void updateStreamEnd();
    void updateLastHttpRequest(const std::string& request);
    void discoverSdxResponse(const std::string& response, const std::string& peerIp);
    void probeSdxDescription(const std::string& location, const std::string& peerIp);
    void probeSdxConnectionManager(const std::string& controlUrl, const std::string& peerIp);

    mutable std::mutex m_rateMutex;
    mutable std::mutex m_cacheStatsMutex;
    mutable uint64_t m_cachedCacheBytes = 0;
    mutable std::chrono::steady_clock::time_point m_cacheStatsTick{};
    uint64_t m_rateBytes = 0;
    mutable uint64_t m_lastRateBytes = 0;
    mutable std::chrono::steady_clock::time_point m_lastRateTick{};
    mutable uint64_t m_currentBps = 0;
    uint64_t m_totalBytes = 0;
    uint64_t m_streamBytes = 0;
    uint64_t m_streamElapsedMs = 0;
    std::chrono::steady_clock::time_point m_streamStartTick{};
    uint64_t m_cacheHits = 0;
    uint64_t m_cacheMisses = 0;
    uint32_t m_activeStreams = 0;
    std::string m_clientIp;
    std::string m_clientName;
    std::string m_clientModel;
    std::string m_streamTitle;
    std::string m_streamArtist;
    std::string m_streamAlbum;
    std::string m_sourceFormat;
    std::string m_outputFormat;
    std::string m_pipelineState;
    std::string m_conversionState;
    uint64_t m_sourceFileSize = 0;
    uint64_t m_outputFileSize = 0;
    uint32_t m_sourceSampleRate = 0;
    uint32_t m_sourceChannels = 0;
    uint32_t m_sourceBitsPerSample = 0;
    uint32_t m_streamChannels = 0;
    uint32_t m_streamBitsPerSample = 0;
    double m_streamDuration = 0.0;
    uint32_t m_streamDsdRate = 0;
    std::string m_sdxIp;
    std::string m_sdxName;
    std::string m_sdxModel;
    std::string m_sdxManufacturer;
    bool m_conversionActive = false;
    uint32_t m_conversionPercent = 0;
    uint64_t m_prebufferBytes = 0;
    uint64_t m_prebufferTargetBytes = 0;
};
