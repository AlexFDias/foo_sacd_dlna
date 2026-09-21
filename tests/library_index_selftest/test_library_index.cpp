// Unit test for library_index.h (standard C++ only). Build: see run.sh
#include "../../library_index.h"
#include <cstdio>
#include <set>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL  line %d: %s\n", __LINE__, #cond); } } while (0)

using libindex::Track;

static Track T(uint32_t id, const char* title, const char* artist, const char* album, const char* genre,
               const char* tn, const char* dn, const char* path, const char* albumArtist = "") {
    Track t; t.id = id; t.title = title; t.artist = artist; t.album = album; t.genre = genre;
    t.trackNumber = tn; t.discNumber = dn; t.nativePath = path; t.albumArtist = albumArtist; return t;
}

int main() {
    std::vector<Track> v;
    // Miles Davis - two-disc album stored out of order, with "3/12" style numbers
    v.push_back(T(10, "So What",        "Miles Davis", "Kind of Blue", "Jazz", "1",    "1/1", "D:\\Music\\DSD\\Miles\\Kind of Blue\\01.dsf"));
    v.push_back(T(11, "Blue in Green",  "Miles Davis", "Kind of Blue", "jazz", "3/5",  "1/1", "D:\\Music\\DSD\\Miles\\Kind of Blue\\03.dsf"));
    v.push_back(T(12, "Freddie Freeloader","Miles Davis","Kind of Blue","Jazz","2",    "1/1", "D:\\Music\\DSD\\Miles\\Kind of Blue\\02.dsf"));
    v.push_back(T(13, "Live Track",     "Miles Davis", "Live Album",   "Jazz", "1",    "2",   "D:\\Music\\DSD\\Miles\\Live\\d2t1.dsf"));
    v.push_back(T(14, "Live Track 0",   "Miles Davis", "Live Album",   "Jazz", "9",    "1",   "D:\\Music\\DSD\\Miles\\Live\\d1t9.dsf"));
    // A compilation: different track artists, same album artist
    v.push_back(T(20, "Song A", "Artist One", "Great Hits", "Rock", "1", "", "D:\\Music\\DSD\\Comp\\a.dsf", "Various Artists"));
    v.push_back(T(21, "Song B", "Artist Two", "Great Hits", "Rock", "2", "", "D:\\Music\\DSD\\Comp\\b.dsf", "Various Artists"));
    // SACD ISO with three subsongs (same file, three items), no genre tag
    v.push_back(T(30, "Iso 1", "Orchestra", "Symphony", "", "1", "", "D:\\Music\\ISO\\sym.iso"));
    v.push_back(T(31, "Iso 2", "Orchestra", "Symphony", "", "2", "", "D:\\Music\\ISO\\sym.iso"));
    v.push_back(T(32, "Iso 3", "Orchestra", "Symphony", "", "3", "", "D:\\Music\\ISO\\sym.iso"));
    // Other drive, UNC share, and a track that is not a plain local file
    v.push_back(T(40, "Drive E", "Someone", "E Album", "Classical", "1", "", "E:\\Stuff\\e.dsf"));
    v.push_back(T(41, "On NAS",  "Someone", "NAS Album", "Classical", "1", "", "\\\\nas\\share\\Music\\n.dsf"));
    v.push_back(T(42, "Remote",  "Someone", "Remote Album", "Classical", "1", "", ""));
    // Same title/artist differing only in case must merge
    v.push_back(T(50, "same",   "MILES davis", "kind of blue", "JAZZ", "4", "1/1", "D:\\Music\\DSD\\Miles\\Kind of Blue\\04.dsf"));

    libindex::NextIds ids;
    const libindex::Index ix = libindex::build(v, ids);

    // ---- artists: case-insensitive merge, album artist wins for compilations
    CHECK(ix.artists.size() == 4);   // Miles Davis (case-insensitive merge), Various Artists, Orchestra, Someone
    std::vector<std::string> names; for (auto& a : ix.artists) names.push_back(a.name);
    CHECK(std::find(names.begin(), names.end(), "Various Artists") != names.end());
    CHECK(std::find(names.begin(), names.end(), "Artist One") == names.end());   // did NOT split the compilation
    CHECK(std::is_sorted(ix.artists.begin(), ix.artists.end(), [](auto& a, auto& b){ return libindex::ciCompare(a.name, b.name) < 0; }));

    // ---- albums
    const libindex::Artist* miles = nullptr;
    for (auto& a : ix.artists) if (libindex::lower(a.name) == "miles davis") miles = &a;
    CHECK(miles && miles->albumIds.size() == 2);                         // "Kind of Blue" (merged with 'kind of blue') + Live
    const libindex::Album* kob = nullptr; const libindex::Album* live = nullptr;
    for (auto& al : ix.albums) { if (al.key == "kind of blue") kob = &al; if (al.key == "live album") live = &al; }
    CHECK(kob && kob->itemIds.size() == 4);
    CHECK(kob && kob->itemIds == std::vector<uint32_t>({10, 12, 11, 50}));       // track order 1,2,3,4
    CHECK(live && live->itemIds == std::vector<uint32_t>({14, 13}));             // disc 1 track 9 before disc 2 track 1
    CHECK(miles && ix.albums[ix.albumIndex.at(miles->albumIds[0])].title == "Kind of Blue");   // albums by title within artist
    const libindex::Album* great = nullptr;
    for (auto& al : ix.albums) if (al.title == "Great Hits") great = &al;
    CHECK(great && great->itemIds.size() == 2 && ix.artists[ix.artistIndex.at(great->artistId)].name == "Various Artists");

    // ---- flat Albums list
    CHECK(ix.albumsByTitle.size() == ix.albums.size());
    std::vector<std::string> titles; for (auto id : ix.albumsByTitle) titles.push_back(ix.albums[ix.albumIndex.at(id)].title);
    CHECK(std::is_sorted(titles.begin(), titles.end(), [](auto& a, auto& b){ return libindex::ciCompare(a, b) < 0; }));

    // ---- genres: case-insensitive merge, empty -> Unknown Genre, sorted
    std::vector<std::string> gn; for (auto& g : ix.genres) gn.push_back(g.name);
    CHECK(ix.genres.size() == 4);   // Classical, Jazz, Rock, Unknown Genre
    CHECK(std::find(gn.begin(), gn.end(), "Unknown Genre") != gn.end());
    size_t jazzCount = 0; for (auto& g : ix.genres) if (g.key == "jazz") jazzCount = g.itemIds.size();
    CHECK(jazzCount == 6);          // 10,11,12,13,14 and 50 ("JAZZ" merges with "Jazz"/"jazz")
    size_t total = 0; for (auto& g : ix.genres) total += g.itemIds.size();
    CHECK(total == v.size());       // every track is in exactly one genre

    // ---- all tracks
    CHECK(ix.allTracks.size() == v.size());
    CHECK(std::set<uint32_t>(ix.allTracks.begin(), ix.allTracks.end()).size() == v.size());
    std::vector<std::string> tt; for (auto id : ix.allTracks) for (auto& t : v) if (t.id == id) tt.push_back(t.title);
    CHECK(std::is_sorted(tt.begin(), tt.end(), [](auto& a, auto& b){ return libindex::ciCompare(a, b) <= 0; }));

    // ---- folders
    // Tree: [root] -> D: -> Music -> {DSD -> {Miles -> {Kind of Blue, Live}, Comp}, ISO}, E: -> Stuff, \\nas.. (nas -> share -> Music)
    // Root has two/three branches (D:, E:, nas), so the collapsed root is the virtual root.
    CHECK(ix.folders.size() > 5);
    const libindex::Folder& froot = ix.folders[ix.folderIndex.at(ix.folderRoot)];
    CHECK(froot.folderIds.size() == 3);                // D:, E:, nas (UNC)
    CHECK(froot.itemIds.empty());
    CHECK(ix.folders[ix.folderIndex.at(froot.folderIds[0])].name == "D:");   // sorted by name
    // The remote track (no native path) is in no folder
    size_t inFolders = 0; for (auto& f : ix.folders) inFolders += f.itemIds.size();
    CHECK(inFolders == v.size() - 1);
    // ISO: three subsong items in the same folder, in track order
    for (auto& f : ix.folders) if (f.name == "ISO") CHECK(f.itemIds == std::vector<uint32_t>({30, 31, 32}));
    // parent links
    for (auto& f : ix.folders) for (auto cid : f.folderIds) CHECK(ix.folders[ix.folderIndex.at(cid)].parentId == f.id);

    // ---- collapsing: a library under one drive and one path collapses to the first branching folder
    std::vector<Track> w;
    w.push_back(T(1, "a", "X", "Alb", "G", "1", "", "D:\\Music\\DSD\\Rock\\a.dsf"));
    w.push_back(T(2, "b", "X", "Alb", "G", "2", "", "D:\\Music\\DSD\\Jazz\\b.dsf"));
    libindex::NextIds ids2; const libindex::Index iw = libindex::build(w, ids2);
    const libindex::Folder& r2 = iw.folders[iw.folderIndex.at(iw.folderRoot)];
    CHECK(r2.name == "DSD");                           // D: and Music (single-child chains) are skipped
    CHECK(r2.folderIds.size() == 2);
    CHECK(iw.folders[iw.folderIndex.at(r2.folderIds[0])].name == "Jazz");

    // ---- ids are never reused across rebuilds
    libindex::NextIds keep = ids;
    const libindex::Index again = libindex::build(v, keep);
    for (auto& a : again.artists) CHECK(a.id >= ids.artist);       // nothing from the first build is reused
    for (auto& a : again.albums)  CHECK(a.id >= ids.album);
    for (auto& g : again.genres)  CHECK(g.id >= ids.genre);
    for (auto& f : again.folders) CHECK(f.id >= ids.folder);
    CHECK(keep.artist > ids.artist && keep.folder > ids.folder);

    // ---- empty and degenerate libraries
    std::vector<Track> none; libindex::NextIds ids3; const libindex::Index e = libindex::build(none, ids3);
    CHECK(e.artists.empty() && e.albums.empty() && e.genres.empty() && e.allTracks.empty());
    CHECK(e.folders.size() == 1 && e.folderRoot == e.folders[0].id);

    std::vector<Track> flat; flat.push_back(T(1, "only", "A", "B", "G", "1", "", "file.dsf"));   // no directory at all
    libindex::NextIds ids4; const libindex::Index f = libindex::build(flat, ids4);
    CHECK(f.folders[f.folderIndex.at(f.folderRoot)].itemIds.size() == 1);

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
