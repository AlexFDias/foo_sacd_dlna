#pragma once

#include "stdafx.h"

namespace dvd_audio_flac {

// Decode one foobar2000 input track through the installed input component and
// write a standards-compliant FLAC file containing 24-bit PCM. The decoder's
// native sample rate and channel count are preserved. The writer intentionally
// uses FLAC verbatim subframes: it is lossless and requires no bundled external
// encoder library, which makes it suitable for a DLNA cache inside this component.
bool decode_to_flac(const char* path, t_uint32 subsong,
                    const std::wstring& outputPath,
                    abort_callback& abort,
                    uint32_t& sampleRate,
                    uint32_t& channels,
                    uint32_t& bitsPerSample,
                    uint64_t& totalSamples,
                    const std::function<void(uint32_t)>& progress = {});

}
