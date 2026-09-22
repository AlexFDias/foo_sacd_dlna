// Unit test for the plugin's real parseSharedFormats() (extracted from dlna_server.cpp by run.sh).
#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>
#include "extracted.inc"   // lowerCopy() and parseSharedFormats()

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL  line %d: %s\n", __LINE__, #cond); } } while (0)
static std::vector<std::string> sorted(const std::unordered_set<std::string>& s) { std::vector<std::string> v(s.begin(), s.end()); std::sort(v.begin(), v.end()); return v; }
using V = std::vector<std::string>;

int main() {
    CHECK(sorted(parseSharedFormats("dsf,dff,iso")) == V({".dff", ".dsf", ".iso"}));          // the default
    CHECK(sorted(parseSharedFormats("")).empty());                                             // nothing shared
    CHECK(sorted(parseSharedFormats("   ")).empty());
    CHECK(sorted(parseSharedFormats("DSF, .Flac ; wav")) == V({".dsf", ".flac", ".wav"}));    // case, leading dot, separators
    CHECK(sorted(parseSharedFormats("dsf,,,dff")) == V({".dff", ".dsf"}));                    // empty tokens ignored
    CHECK(sorted(parseSharedFormats("dsf dff\tiso\nflac\r\nmp3")) == V({".dff", ".dsf", ".flac", ".iso", ".mp3"}));
    CHECK(sorted(parseSharedFormats("flac,flac,FLAC")) == V({".flac"}));                       // duplicates collapse
    CHECK(sorted(parseSharedFormats(".")).empty());                                            // a lone dot is not a format
    CHECK(sorted(parseSharedFormats("iso, ")) == V({".iso"}));                                 // trailing separator must not share extension-less files
    CHECK(parseSharedFormats("iso,").count("") == 0 && parseSharedFormats("iso,").count(".") == 0);
    CHECK(sorted(parseSharedFormats("tar.gz")) == V({".tar.gz"}));
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
