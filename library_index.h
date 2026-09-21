#pragma once
// Library index behind the UPnP ContentDirectory tree: Artists, Albums, Genres,
// Folders and All Tracks. Standard C++ only (no foobar2000 SDK, no Windows), so it
// can be unit-tested anywhere: see tests/library_index_selftest.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace libindex {

// One shared track, as far as the index is concerned.
struct Track {
    uint32_t id = 0;
    std::string title, artist, albumArtist, album, genre, trackNumber, discNumber;
    // UTF-8 native path ("D:\Music\Album\01.dsf", "\\nas\share\a.dsf"). Empty when the
    // track is not a plain local file; such tracks are left out of the Folders view.
    std::string nativePath;
};

struct Artist {
    uint32_t id = 0;
    std::string name;
    std::string key;
    std::vector<uint32_t> albumIds;
};

struct Album {
    uint32_t id = 0;
    uint32_t artistId = 0;
    std::string title;
    std::string key;
    uint32_t representativeItemId = 0;
    std::vector<uint32_t> itemIds;
};

struct Genre {
    uint32_t id = 0;
    std::string name;
    std::string key;
    std::vector<uint32_t> itemIds;
};

struct Folder {
    uint32_t id = 0;
    uint32_t parentId = 0;
    std::string name;
    std::vector<uint32_t> folderIds;   // sub-folders, sorted by name
    std::vector<uint32_t> itemIds;     // tracks directly inside, sorted by disc/track/title
};

// Object ids are never reused across rebuilds: a stale id held by a renderer
// resolves to nothing instead of silently pointing at different content.
struct NextIds {
    uint32_t artist = 1, album = 1, genre = 1, folder = 1;
};

struct Index {
    std::vector<Artist> artists;                // sorted by name
    std::vector<Album> albums;                  // sorted by artist, then title
    std::vector<Genre> genres;                  // sorted by name
    std::vector<Folder> folders;                // creation order; folders[0] is the virtual root
    std::vector<uint32_t> albumsByTitle;        // album ids sorted by title, then artist
    std::vector<uint32_t> allTracks;            // track ids sorted by title, then artist, album
    uint32_t folderRoot = 0;                    // id of the folder shown as "Folders" (single-child chains collapsed)
    std::vector<uint32_t> trackArtistId;        // parallel to the input tracks
    std::vector<uint32_t> trackAlbumId;         // parallel to the input tracks
    std::unordered_map<uint32_t, size_t> artistIndex, albumIndex, genreIndex, folderIndex;   // id -> position
};

inline std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Case-insensitive ordering on bytes; a shorter string that is a prefix sorts first.
inline int ciCompare(const std::string& a, const std::string& b) {
    const size_t n = a.size() < b.size() ? a.size() : b.size();   // no std::min: windows.h min/max macros
    for (size_t i = 0; i < n; ++i) {
        const int ca = std::tolower(static_cast<unsigned char>(a[i]));
        const int cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

// "3", "3/12" -> 3. Anything not starting with a digit sorts last.
inline unsigned numberPrefix(const std::string& s) {
    const char* p = s.c_str();
    while (*p == ' ' || *p == '\t') ++p;
    if (*p < '0' || *p > '9') return UINT32_MAX;
    return static_cast<unsigned>(std::strtoul(p, nullptr, 10));
}

inline Index build(const std::vector<Track>& tracks, NextIds& next) {
    Index idx;
    const size_t n = tracks.size();
    idx.trackArtistId.assign(n, 0);
    idx.trackAlbumId.assign(n, 0);

    // ---- group into artists, albums, genres (hash lookups, not linear scans) ----
    std::unordered_map<std::string, size_t> artistByKey, albumByKey, genreByKey;
    for (size_t i = 0; i < n; ++i) {
        const Track& t = tracks[i];

        // Albums live under the ALBUM artist when tagged, so a compilation does not
        // split into one pseudo-album per track artist.
        const std::string& groupArtist = !t.albumArtist.empty() ? t.albumArtist : t.artist;
        const std::string artistKey = lower(groupArtist);
        auto ait = artistByKey.find(artistKey);
        if (ait == artistByKey.end()) {
            Artist a; a.id = next.artist++; a.name = groupArtist; a.key = artistKey;
            idx.artists.push_back(std::move(a));
            ait = artistByKey.emplace(artistKey, idx.artists.size() - 1).first;
        }
        Artist& artist = idx.artists[ait->second];
        idx.trackArtistId[i] = artist.id;

        const std::string albumTitleKey = lower(t.album);
        const std::string albumMapKey = std::to_string(artist.id) + '\x1f' + albumTitleKey;
        auto bit = albumByKey.find(albumMapKey);
        if (bit == albumByKey.end()) {
            Album a; a.id = next.album++; a.artistId = artist.id; a.title = t.album; a.key = albumTitleKey; a.representativeItemId = t.id;
            idx.albums.push_back(std::move(a));
            bit = albumByKey.emplace(albumMapKey, idx.albums.size() - 1).first;
            artist.albumIds.push_back(idx.albums.back().id);
        }
        Album& album = idx.albums[bit->second];
        idx.trackAlbumId[i] = album.id;
        album.itemIds.push_back(t.id);

        // Only the first genre value reaches the index; an empty tag becomes "Unknown Genre".
        const std::string genreName = t.genre.empty() ? std::string("Unknown Genre") : t.genre;
        const std::string genreKey = lower(genreName);
        auto git = genreByKey.find(genreKey);
        if (git == genreByKey.end()) {
            Genre g; g.id = next.genre++; g.name = genreName; g.key = genreKey;
            idx.genres.push_back(std::move(g));
            git = genreByKey.emplace(genreKey, idx.genres.size() - 1).first;
        }
        idx.genres[git->second].itemIds.push_back(t.id);
    }

    // ---- sort keys ----
    struct Key { unsigned disc, track; const Track* t; };
    std::unordered_map<uint32_t, Key> keyOf;
    keyOf.reserve(n);
    for (const Track& t : tracks) keyOf.emplace(t.id, Key{ numberPrefix(t.discNumber), numberPrefix(t.trackNumber), &t });

    // disc, track number, title, id: the order an album is played in.
    const auto albumOrder = [&](uint32_t a, uint32_t b) {
        const Key& ka = keyOf.at(a); const Key& kb = keyOf.at(b);
        if (ka.disc != kb.disc) return ka.disc < kb.disc;
        if (ka.track != kb.track) return ka.track < kb.track;
        const int c = ciCompare(ka.t->title, kb.t->title);
        return c != 0 ? c < 0 : a < b;
    };

    // ---- artists, albums ----
    std::sort(idx.artists.begin(), idx.artists.end(), [](const Artist& a, const Artist& b) {
        const int c = ciCompare(a.name, b.name);
        return c != 0 ? c < 0 : a.id < b.id;
    });
    for (size_t i = 0; i < idx.artists.size(); ++i) idx.artistIndex.emplace(idx.artists[i].id, i);
    const auto artistName = [&](uint32_t id) -> const std::string& {
        static const std::string empty;
        const auto it = idx.artistIndex.find(id);
        return it == idx.artistIndex.end() ? empty : idx.artists[it->second].name;
    };

    std::sort(idx.albums.begin(), idx.albums.end(), [&](const Album& a, const Album& b) {
        int c = ciCompare(artistName(a.artistId), artistName(b.artistId));
        if (c == 0) c = ciCompare(a.title, b.title);
        return c != 0 ? c < 0 : a.id < b.id;
    });
    for (size_t i = 0; i < idx.albums.size(); ++i) idx.albumIndex.emplace(idx.albums[i].id, i);
    const auto albumOf = [&](uint32_t id) -> const Album& { return idx.albums[idx.albumIndex.at(id)]; };

    for (Album& album : idx.albums) std::sort(album.itemIds.begin(), album.itemIds.end(), albumOrder);
    for (Artist& artist : idx.artists) std::sort(artist.albumIds.begin(), artist.albumIds.end(), [&](uint32_t a, uint32_t b) {
        const int c = ciCompare(albumOf(a).title, albumOf(b).title);
        return c != 0 ? c < 0 : a < b;
    });

    // All albums by title (then artist): the flat "Albums" view.
    idx.albumsByTitle.reserve(idx.albums.size());
    for (const Album& a : idx.albums) idx.albumsByTitle.push_back(a.id);
    std::sort(idx.albumsByTitle.begin(), idx.albumsByTitle.end(), [&](uint32_t a, uint32_t b) {
        const Album& x = albumOf(a); const Album& y = albumOf(b);
        int c = ciCompare(x.title, y.title);
        if (c == 0) c = ciCompare(artistName(x.artistId), artistName(y.artistId));
        return c != 0 ? c < 0 : a < b;
    });

    // ---- genres: by name; tracks by artist, album, disc, track, title ----
    std::sort(idx.genres.begin(), idx.genres.end(), [](const Genre& a, const Genre& b) {
        const int c = ciCompare(a.name, b.name);
        return c != 0 ? c < 0 : a.id < b.id;
    });
    for (size_t i = 0; i < idx.genres.size(); ++i) idx.genreIndex.emplace(idx.genres[i].id, i);
    const auto genreOrder = [&](uint32_t a, uint32_t b) {
        const Track& x = *keyOf.at(a).t; const Track& y = *keyOf.at(b).t;
        int c = ciCompare(x.artist, y.artist);
        if (c == 0) c = ciCompare(x.album, y.album);
        if (c != 0) return c < 0;
        return albumOrder(a, b);
    };
    for (Genre& g : idx.genres) std::sort(g.itemIds.begin(), g.itemIds.end(), genreOrder);

    // ---- folders: a tree mirroring the file system ----
    {
        Folder root; root.id = next.folder++;
        idx.folders.push_back(std::move(root));
        std::unordered_map<std::string, size_t> childByKey;   // "<parentId>\x1f<lower(name)>" -> position

        for (const Track& t : tracks) {
            if (t.nativePath.empty()) continue;
            std::vector<std::string> parts;
            std::string cur;
            for (const char c : t.nativePath) {
                if (c == '\\' || c == '/') { if (!cur.empty()) parts.push_back(cur); cur.clear(); }
                else cur.push_back(c);
            }
            if (!cur.empty()) parts.push_back(cur);
            if (parts.empty()) continue;

            size_t node = 0;
            for (size_t k = 0; k + 1 < parts.size(); ++k) {   // every part except the file name
                const std::string key = std::to_string(idx.folders[node].id) + '\x1f' + lower(parts[k]);
                auto it = childByKey.find(key);
                if (it == childByKey.end()) {
                    Folder f; f.id = next.folder++; f.parentId = idx.folders[node].id; f.name = parts[k];
                    idx.folders.push_back(std::move(f));
                    const size_t pos = idx.folders.size() - 1;
                    idx.folders[node].folderIds.push_back(idx.folders[pos].id);
                    it = childByKey.emplace(key, pos).first;
                }
                node = it->second;
            }
            idx.folders[node].itemIds.push_back(t.id);
        }

        for (size_t i = 0; i < idx.folders.size(); ++i) idx.folderIndex.emplace(idx.folders[i].id, i);
        for (Folder& f : idx.folders) {
            std::sort(f.folderIds.begin(), f.folderIds.end(), [&](uint32_t a, uint32_t b) {
                const int c = ciCompare(idx.folders[idx.folderIndex.at(a)].name, idx.folders[idx.folderIndex.at(b)].name);
                return c != 0 ? c < 0 : a < b;
            });
            std::sort(f.itemIds.begin(), f.itemIds.end(), albumOrder);
        }

        // "Folders" starts at the first folder that actually branches or holds tracks,
        // so a library under D:\Music\DSD does not open with three single-entry levels.
        size_t r = 0;
        while (idx.folders[r].itemIds.empty() && idx.folders[r].folderIds.size() == 1)
            r = idx.folderIndex.at(idx.folders[r].folderIds[0]);
        idx.folderRoot = idx.folders[r].id;
    }

    // ---- all tracks: by title, then artist, album ----
    idx.allTracks.reserve(n);
    for (const Track& t : tracks) idx.allTracks.push_back(t.id);
    std::sort(idx.allTracks.begin(), idx.allTracks.end(), [&](uint32_t a, uint32_t b) {
        const Track& x = *keyOf.at(a).t; const Track& y = *keyOf.at(b).t;
        int c = ciCompare(x.title, y.title);
        if (c == 0) c = ciCompare(x.artist, y.artist);
        if (c == 0) c = ciCompare(x.album, y.album);
        return c != 0 ? c < 0 : a < b;
    });

    return idx;
}

}  // namespace libindex
