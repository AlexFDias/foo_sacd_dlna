#pragma once
#include "stdafx.h"
#include "dsf_writer.h"
#include <functional>

class SacdDecoder {
public:
    static bool unpackDop(const audio_chunk& chunk, std::vector<std::vector<uint8_t>>& out, uint32_t& dsdRate);
    static input_entry::ptr findFooSacd(const char* path);

    static DsdTrack decodeToDsf(const char* path, t_uint32 subsong,
                                const std::wstring& outputPath,
                                abort_callback& abort,
                                const std::function<void(uint32_t)>& progress = {});

};
