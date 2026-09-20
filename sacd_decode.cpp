#include "stdafx.h"
#include "sacd_decode.h"

namespace {

static bool isFooSacdName(const char* name) {
    return name && _stricmp(name, "Super Audio CD Decoder") == 0;
}

static int32_t toInt24(audio_sample sample) {
    // foo_input_sacd with input_flag_dop exposes DoP as 24-bit PCM carried
    // in foobar2000's normalized audio_sample representation. Use the SDK's
    // exact conversion helper so x64 (double audio_sample) retains all bits.
    uint8_t bytes[3]{};
    audio_math::convert_to_int24(&sample, 1, bytes, 1.0);
    return (static_cast<int32_t>(bytes[0]) |
            (static_cast<int32_t>(bytes[1]) << 8) |
            (static_cast<int32_t>(bytes[2]) << 16));
}



static std::string metaOrEmpty(const file_info& info, const char* name) {
    const char* v = info.meta_get(name, 0);
    return v ? v : "";
}

static std::string metaFirst(const file_info& info, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        auto v = metaOrEmpty(info, name);
        if (!v.empty()) return v;
    }
    return {};
}

}

bool SacdDecoder::unpackDop(const audio_chunk& chunk,
                      std::vector<std::vector<uint8_t>>& out,
                      uint32_t& dsdRate) {
    if (chunk.get_channels() != 2) return false;

    const unsigned dopRate = chunk.get_srate();
    switch (dopRate) {
        case 176400: dsdRate = 2822400; break;   // DSD64
        case 352800: dsdRate = 5644800; break;   // DSD128
        case 705600: dsdRate = 11289600; break;  // DSD256
        default: return false;
    }

    const audio_sample* samples = chunk.get_data();
    const size_t count = chunk.get_sample_count();
    out.assign(2, {});
    out[0].reserve(count * 2);
    out[1].reserve(count * 2);

    for (size_t i = 0; i < count; ++i) {
        const int32_t l = toInt24(samples[i * 2 + 0]);
        const int32_t r = toInt24(samples[i * 2 + 1]);

        const uint8_t lm = static_cast<uint8_t>((static_cast<uint32_t>(l) >> 16) & 0xFF);
        const uint8_t rm = static_cast<uint8_t>((static_cast<uint32_t>(r) >> 16) & 0xFF);
        if ((lm != 0x05 && lm != 0xFA) || (rm != 0x05 && rm != 0xFA)) return false;

        out[0].push_back(static_cast<uint8_t>(l & 0xFF));
        out[0].push_back(static_cast<uint8_t>((static_cast<uint32_t>(l) >> 8) & 0xFF));
        out[1].push_back(static_cast<uint8_t>(r & 0xFF));
        out[1].push_back(static_cast<uint8_t>((static_cast<uint32_t>(r) >> 8) & 0xFF));
    }
    return true;
}

input_entry::ptr SacdDecoder::findFooSacd(const char* path) {
    pfc::list_t<input_entry::ptr> inputs;
    if (!input_entry::g_find_inputs_by_path(inputs, path, false)) return nullptr;

    for (auto const& entry : inputs) {
        input_entry_v2::ptr v2;
        if (v2 &= entry) {
            if (isFooSacdName(v2->get_name())) return entry;
        }
    }
    return nullptr;
}

DsdTrack SacdDecoder::decodeToDsf(const char* path, t_uint32 subsong,
                                  const std::wstring& outputPath,
                                  abort_callback& abort,
                                  const std::function<void(uint32_t)>& progress) {
    auto sacdInput = findFooSacd(path);
    if (!sacdInput.is_valid())
        throw std::runtime_error("Super Audio CD Decoder (foo_input_sacd) is not installed or does not handle this file");

    service_ptr_t<input_info_reader> infoReader;
    sacdInput->open_for_info_read(infoReader, nullptr, path, abort);

    file_info_impl info;
    infoReader->get_info(subsong, info, abort);

    DsdTrack result;
    result.title = metaOrEmpty(info, "title");
    result.artist = metaOrEmpty(info, "artist");
    result.album = metaOrEmpty(info, "album");
    result.albumArtist = metaFirst(info, {"album artist", "albumartist"});
    result.genre = metaOrEmpty(info, "genre");
    result.date = metaFirst(info, {"date", "year"});
    result.composer = metaOrEmpty(info, "composer");
    result.publisher = metaFirst(info, {"publisher", "label"});
    result.comment = metaFirst(info, {"comment", "comments"});
    result.trackNumber = metaFirst(info, {"tracknumber", "track number", "track"});
    result.discNumber = metaFirst(info, {"discnumber", "disc number", "disc"});
    result.totalTracks = metaFirst(info, {"totaltracks", "total tracks", "tracktotal"});
    result.totalDiscs = metaFirst(info, {"totaldiscs", "total discs"});
    result.path = outputPath;
    result.channels = static_cast<uint32_t>(std::max<t_int64>(1, info.info_get_int("channels")));
    result.bitsPerSample = static_cast<uint32_t>(std::max<t_int64>(1, info.info_get_int("bitspersample")));
    const double duration = info.get_length();

    service_ptr_t<input_decoder> decoder;
    sacdInput->open_for_decoding(decoder, nullptr, path, abort);

    decoder->initialize(
        subsong,
        input_flag_no_seeking | input_flag_no_looping | input_flag_dop,
        abort
    );

    DsfWriter writer(outputPath);
    bool started = false;
    uint32_t dsdRate = 0;
    uint64_t samplesPerChannel = 0;

    for (;;) {
        abort.check();
        audio_chunk_impl_temporary chunk;
        if (!decoder->run(chunk, abort)) break;

        std::vector<std::vector<uint8_t>> dsd;
        uint32_t thisRate = 0;
        if (!unpackDop(chunk, dsd, thisRate))
            throw std::runtime_error("foo_input_sacd did not return a supported native DSD/DoP stream");

        if (!started) {
            dsdRate = thisRate;
            writer.begin(dsdRate, 2);
            started = true;
        } else if (thisRate != dsdRate) {
            throw std::runtime_error("DSD rate changed while decoding a single track");
        }

        samplesPerChannel += static_cast<uint64_t>(dsd[0].size()) * 8ULL;
        writer.append(dsd);
        if (progress && duration > 0.0) {
            const double decodedSeconds = static_cast<double>(samplesPerChannel) / static_cast<double>(dsdRate);
            const uint32_t pct = static_cast<uint32_t>(std::clamp(decodedSeconds / duration * 100.0, 0.0, 100.0));
            progress(pct);
        }
    }

    if (!started) throw std::runtime_error("foo_input_sacd produced no DSD data");

    writer.finish(samplesPerChannel);
    if (progress) progress(100);
    result.dsdRate = dsdRate;
    result.channels = 2;
    result.bitsPerSample = 1;
    result.dsdSamplesPerChannel = samplesPerChannel;
    result.duration = dsdRate ? static_cast<double>(samplesPerChannel) / static_cast<double>(dsdRate) : duration;

    std::ifstream check(outputPath, std::ios::binary | std::ios::ate);
    if (!check) throw std::runtime_error("Unable to verify generated DSF file");
    result.fileSize = static_cast<uint64_t>(check.tellg());
    return result;
}
