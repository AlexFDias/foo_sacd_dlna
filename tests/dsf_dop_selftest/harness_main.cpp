// Feeds a synthetic DoP stream through the plugin's REAL SacdDecoder::unpackDop()
// and DsfWriter and writes a DSF file. See README.md.
#include "stub.h"
#include "dsf_writer.h"
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: harness <dop_input.f64> <out.dsf>\n"); return 1; }
    std::ifstream in(argv[1], std::ios::binary | std::ios::ate);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    const size_t n = static_cast<size_t>(in.tellg()) / sizeof(double); in.seekg(0);
    std::vector<double> all(n); in.read(reinterpret_cast<char*>(all.data()), static_cast<std::streamsize>(n * sizeof(double)));
    DsfWriter w(std::wstring(argv[2], argv[2] + std::strlen(argv[2])));
    bool started = false; uint32_t rate = 0; uint64_t spc = 0;
    const size_t chunkFrames = 2048;
    for (size_t f = 0; f < n / 2; f += chunkFrames) {
        audio_chunk c; const size_t nf = std::min(chunkFrames, n / 2 - f);
        c.data.assign(all.begin() + static_cast<std::ptrdiff_t>(f * 2), all.begin() + static_cast<std::ptrdiff_t>((f + nf) * 2));
        std::vector<std::vector<uint8_t>> dsd; uint32_t r = 0;
        if (!SacdDecoder::unpackDop(c, dsd, r)) { std::printf("unpackDop rejected chunk at frame %zu\n", f); return 2; }
        if (!started) { rate = r; w.begin(rate, 2); started = true; }
        spc += static_cast<uint64_t>(dsd[0].size()) * 8; w.append(dsd);
    }
    w.finish(spc);
    std::printf("DSF written: rate=%u samples/channel=%llu\n", rate, static_cast<unsigned long long>(spc));
    return 0;
}
