#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <strings.h>
#define _stricmp strcasecmp
#include "library_index.h"
#include "dsdtrack.inc"

struct metadb_handle_ptr {};
namespace sacd_dlna_cfg { struct Flag { bool v = false; operator bool() const { return v; } }; inline Flag dsd_processor_enabled; }
typedef uint32_t t_uint32;
namespace { 
#include "consts.inc"
std::string mimeForExtension(const std::string&) { return "audio/x-dsf"; }
std::string formatDuration(double s) { char b[32]; std::snprintf(b, sizeof b, "0:%02d:%02d.000", (int)s / 60, (int)s % 60); return b; }
}

class SacdDlnaServer {
public:
    struct Item {
        uint32_t id = 0; std::string sourcePath, sourceExt; uint64_t sourceSize = 0; uint32_t sourceSampleRate = 0, sourceChannels = 0, sourceBitsPerSample = 0;
        int64_t sourceWriteTime = 0; t_uint32 subsong = 0; metadb_handle_ptr handle; DsdTrack track; std::wstring cachePath; uint32_t artistId = 0, albumId = 0;
    };
    using Artist = libindex::Artist; using Album = libindex::Album; using Genre = libindex::Genre; using Folder = libindex::Folder;
    struct Playlist { uint32_t id = 0; std::string name; std::vector<uint32_t> itemIds; };

    std::string browseDidl(const std::string& objectId, bool metadataOnly, unsigned startingIndex, unsigned requestedCount, unsigned& numberReturned, unsigned& totalMatches) const;

    // Replica exacta do bloco de "commit" do publish()
    void load(std::vector<Item> newItems, const std::vector<libindex::Track>& tracks) {
        libindex::NextIds nextIds; libindex::Index index = libindex::build(tracks, nextIds);
        for (size_t i = 0; i < newItems.size(); ++i) { newItems[i].artistId = index.trackArtistId[i]; newItems[i].albumId = index.trackAlbumId[i]; }
        std::lock_guard<std::mutex> g(m_mutex);
        m_items = std::move(newItems);
        m_artists = std::move(index.artists); m_albums = std::move(index.albums); m_genres = std::move(index.genres); m_folders = std::move(index.folders);
        m_albumsByTitle = std::move(index.albumsByTitle); m_allTrackIds = std::move(index.allTracks); m_folderRootId = index.folderRoot;
        m_artistIndex = std::move(index.artistIndex); m_albumIndex = std::move(index.albumIndex); m_genreIndex = std::move(index.genreIndex); m_folderIndex = std::move(index.folderIndex);
        m_itemIndex.clear(); for (size_t i = 0; i < m_items.size(); ++i) m_itemIndex.emplace(m_items[i].id, i);
    }
    void setPlaylists(std::vector<Playlist> p) { std::lock_guard<std::mutex> g(m_mutex); m_playlists = std::move(p); }
private:
    std::string localAddress() const { return "10.0.0.5"; }
    std::string chooseRendererMime(const std::string& m) const { return m; }
    std::string chooseRendererProtocolInfo(const std::string& m) const { return "http-get:*:" + m + ":*"; }
    static std::string xmlEscape(const std::string& s) { std::string o; for (char c : s) { switch (c) { case '&': o += "&amp;"; break; case '<': o += "&lt;"; break; case '>': o += "&gt;"; break; case '"': o += "&quot;"; break; default: o += c; } } return o; }
    mutable std::mutex m_mutex; uint16_t m_port = 8192; uint32_t m_updateId = 1;
    std::vector<Item> m_items; std::vector<Artist> m_artists; std::vector<Album> m_albums; std::vector<Genre> m_genres; std::vector<Folder> m_folders; std::vector<Playlist> m_playlists;
    std::vector<uint32_t> m_albumsByTitle, m_allTrackIds; uint32_t m_folderRootId = 0;
    std::unordered_map<uint32_t, size_t> m_itemIndex, m_artistIndex, m_albumIndex, m_genreIndex, m_folderIndex;
};

#include "browse_real.inc"

int main(int argc, char** argv) {
    // biblioteca sintetica: 40 artistas x 5 albuns x 12 faixas, generos e pastas variados (+ 1 compilacao, 1 ISO)
    std::vector<SacdDlnaServer::Item> items; std::vector<libindex::Track> tracks; uint32_t id = 1;
    const char* genresList[] = { "Jazz", "Rock", "Classical", "Blues", "" };
    auto add = [&](const std::string& title, const std::string& artist, const std::string& album, const std::string& genre, int tn, int dn, const std::string& path, const std::string& albumArtist = "") {
        SacdDlnaServer::Item x; x.id = id; x.sourceExt = ".dsf"; x.track.title = title; x.track.artist = artist; x.track.album = album; x.track.genre = genre;
        x.track.dsdRate = 2822400; x.track.duration = 200; x.track.fileSize = 1000; x.track.trackNumber = std::to_string(tn); items.push_back(x);
        libindex::Track t; t.id = id; t.title = title; t.artist = artist; t.album = album; t.albumArtist = albumArtist; t.genre = genre;
        t.trackNumber = std::to_string(tn); t.discNumber = std::to_string(dn); t.nativePath = path; tracks.push_back(t); ++id; };
    for (int a = 0; a < 40; ++a) for (int al = 0; al < 5; ++al) for (int t = 1; t <= 12; ++t) {
        char artist[32], album[32], title[40], path[128];
        std::snprintf(artist, sizeof artist, "Artist %02d", a); std::snprintf(album, sizeof album, "Album %d", al);
        std::snprintf(title, sizeof title, "Track %02d of A%02d/%d", t, a, al);
        std::snprintf(path, sizeof path, "D:\\Music\\DSD\\%s\\%s\\%02d.dsf", artist, album, t);
        add(title, artist, album, genresList[(a + al) % 5], t, 1, path);
    }
    for (int t = 1; t <= 3; ++t) add("Comp " + std::to_string(t), "Guest " + std::to_string(t), "Best of & <More>", "Rock", t, 1, "E:\\Comp\\c" + std::to_string(t) + ".dsf", "Various Artists");
    for (int t = 1; t <= 3; ++t) add("Iso " + std::to_string(t), "Orchestra", "Symphony", "", t, 1, "D:\\Music\\ISO\\sym.iso");
    SacdDlnaServer s; s.load(items, tracks);
    { SacdDlnaServer::Playlist a; a.id = 1; a.name = "Favourites & <Best>"; for (uint32_t k = 1; k <= 30; ++k) a.itemIds.push_back(k * 7);
      SacdDlnaServer::Playlist b; b.id = 2; b.name = "Empty"; s.setPlaylists({a, b}); }

    if (argc < 5) { std::printf("total items=%u\n", id - 1); return 0; }
    unsigned nr = 0, tm = 0;
    const std::string didl = s.browseDidl(argv[1], std::string(argv[2]) == "meta", (unsigned)std::strtoul(argv[3], nullptr, 10), (unsigned)std::strtoul(argv[4], nullptr, 10), nr, tm);
    std::printf("%u %u\n%s\n", nr, tm, didl.c_str());
    return 0;
}
