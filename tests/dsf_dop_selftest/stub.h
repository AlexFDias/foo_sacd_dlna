#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <vector>
#include <string>
#include <array>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <filesystem>
typedef double audio_sample;
namespace audio_math {
// Réplica fiel de pfc/audio_math.cpp: scale *= 0x800000; clip; rint; int24 little-endian
inline void convert_to_int24(const double* in, size_t count, void* out, double scale) {
    scale *= 0x800000; uint8_t* p = (uint8_t*)out;
    for (size_t i = 0; i < count; ++i) {
        double v = in[i] * scale; if (v < -8388608.0) v = -8388608.0; if (v > 8388607.0) v = 8388607.0;
        int32_t iv = (int32_t)std::nearbyint(v);
        p[0] = iv & 0xFF; p[1] = (iv >> 8) & 0xFF; p[2] = (iv >> 16) & 0xFF; p += 3;
    }
}
}
struct audio_chunk {
    unsigned ch = 2, sr = 176400; std::vector<double> data;
    unsigned get_channels() const { return ch; } unsigned get_srate() const { return sr; }
    const audio_sample* get_data() const { return data.data(); }
    size_t get_sample_count() const { return data.size() / ch; }
};
struct SacdDecoder { static bool unpackDop(const audio_chunk&, std::vector<std::vector<uint8_t>>&, uint32_t&); };
