#include "stdafx.h"
#include "dvd_audio_flac.h"

namespace dvd_audio_flac {
namespace {

// We use the official libFLAC encoder (FLAC 1.5.x) at runtime instead of
// maintaining a hand-written FLAC frame encoder. The project ships the
// Win64 libFLAC.dll in third_party/libFLAC/Win64 and loads it dynamically,
// so no import library or FLAC SDK headers are required to build foo_sacd_dlna.
//
// This keeps the component's build independent of a system-wide FLAC install,
// while using Xiph's encoder for frame headers, subframes, CRCs and STREAMINFO.

using FLAC_bool = int;
using FLAC_int32 = int32_t;
using FLAC_uint64 = uint64_t;

struct FLAC_StreamEncoder;

using FLAC_StreamEncoderInitStatus = int;

constexpr FLAC_StreamEncoderInitStatus FLAC_INIT_OK = 0;

struct FlacApi {
    HMODULE module = nullptr;

    FlacApi() = default;
    FlacApi(const FlacApi&) = delete;
    FlacApi& operator=(const FlacApi&) = delete;

    FLAC_StreamEncoder* (*encoder_new)() = nullptr;
    void (*encoder_delete)(FLAC_StreamEncoder*) = nullptr;
    FLAC_bool (*set_verify)(FLAC_StreamEncoder*, FLAC_bool) = nullptr;
    FLAC_bool (*set_streamable_subset)(FLAC_StreamEncoder*, FLAC_bool) = nullptr;
    FLAC_bool (*set_channels)(FLAC_StreamEncoder*, uint32_t) = nullptr;
    FLAC_bool (*set_bits_per_sample)(FLAC_StreamEncoder*, uint32_t) = nullptr;
    FLAC_bool (*set_sample_rate)(FLAC_StreamEncoder*, uint32_t) = nullptr;
    FLAC_bool (*set_compression_level)(FLAC_StreamEncoder*, uint32_t) = nullptr;
    FLAC_bool (*set_blocksize)(FLAC_StreamEncoder*, uint32_t) = nullptr;
    FLAC_bool (*set_do_mid_side_stereo)(FLAC_StreamEncoder*, FLAC_bool) = nullptr;
    FLAC_bool (*set_total_samples_estimate)(FLAC_StreamEncoder*, FLAC_uint64) = nullptr;
    FLAC_StreamEncoderInitStatus (*init_file)(FLAC_StreamEncoder*, const char*, void (*)(const FLAC_StreamEncoder*, FLAC_uint64, FLAC_uint64, uint32_t, uint32_t, void*), void*) = nullptr;
    FLAC_bool (*finish)(FLAC_StreamEncoder*) = nullptr;
    FLAC_bool (*process_interleaved)(FLAC_StreamEncoder*, const FLAC_int32*, uint32_t) = nullptr;

    ~FlacApi() {
        if (module) FreeLibrary(module);
    }

    template<typename T>
    bool load(T& target, const char* name) {
        target = reinterpret_cast<T>(GetProcAddress(module, name));
        return target != nullptr;
    }

    static std::wstring moduleDirectory() {
        HMODULE self = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&moduleDirectory), &self)) {
            return {};
        }
        wchar_t path[MAX_PATH]{};
        const DWORD n = GetModuleFileNameW(self, path, static_cast<DWORD>(std::size(path)));
        if (!n || n >= std::size(path)) return {};
        std::wstring result(path, path + n);
        const auto slash = result.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return {};
        result.resize(slash);
        return result;
    }

    bool load() {
        const std::wstring dir = moduleDirectory();
        if (!dir.empty()) {
            const std::wstring candidate = dir + L"\\libFLAC.dll";
            module = LoadLibraryW(candidate.c_str());
        }
        if (!module) module = LoadLibraryW(L"libFLAC.dll");
        if (!module) return false;

        const bool ok =
            load(encoder_new, "FLAC__stream_encoder_new") &&
            load(encoder_delete, "FLAC__stream_encoder_delete") &&
            load(set_verify, "FLAC__stream_encoder_set_verify") &&
            load(set_streamable_subset, "FLAC__stream_encoder_set_streamable_subset") &&
            load(set_channels, "FLAC__stream_encoder_set_channels") &&
            load(set_bits_per_sample, "FLAC__stream_encoder_set_bits_per_sample") &&
            load(set_sample_rate, "FLAC__stream_encoder_set_sample_rate") &&
            load(set_compression_level, "FLAC__stream_encoder_set_compression_level") &&
            load(set_blocksize, "FLAC__stream_encoder_set_blocksize") &&
            load(set_do_mid_side_stereo, "FLAC__stream_encoder_set_do_mid_side_stereo") &&
            load(set_total_samples_estimate, "FLAC__stream_encoder_set_total_samples_estimate") &&
            load(init_file, "FLAC__stream_encoder_init_file") &&
            load(finish, "FLAC__stream_encoder_finish") &&
            load(process_interleaved, "FLAC__stream_encoder_process_interleaved");
        if (!ok) {
            FreeLibrary(module);
            module = nullptr;
        }
        return ok;
    }
};

FlacApi& flacApi() {
    static FlacApi api;
    static const bool loaded = api.load();
    (void)loaded;
    return api;
}

std::string utf8FromWide(const std::wstring& value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) throw std::runtime_error("unable to convert FLAC output path to UTF-8");
    std::string result(static_cast<size_t>(needed), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.c_str(), static_cast<int>(value.size()), result.data(), needed, nullptr, nullptr)) {
        throw std::runtime_error("unable to convert FLAC output path to UTF-8");
    }
    return result;
}

int32_t floatToInt24(audio_sample sample) {
    if (sample <= -1.0f) return -8388608;
    if (sample >= 1.0f) return 8388607;
    const double scaled = static_cast<double>(sample) * 8388607.0;
    return static_cast<int32_t>(std::llround(scaled));
}

void flacProgress(const FLAC_StreamEncoder*, FLAC_uint64, FLAC_uint64 samplesWritten,
                  uint32_t, uint32_t, void* clientData) {
    auto* ctx = static_cast<std::pair<double, const std::function<void(uint32_t)>*>*>(clientData);
    if (!ctx || !ctx->second || !*ctx->second || ctx->first <= 0.0) return;
    const double percent = std::clamp(
        static_cast<double>(samplesWritten) / ctx->first * 100.0, 0.0, 100.0);
    (*ctx->second)(static_cast<uint32_t>(percent));
}

}

bool decode_to_flac(const char* path, t_uint32 subsong, const std::wstring& outputPath,
                    abort_callback& abort, uint32_t& sampleRate, uint32_t& channels,
                    uint32_t& bitsPerSample, uint64_t& totalSamples,
                    const std::function<void(uint32_t)>& progress) {
    // Open the real DVD-A decoder first.  Some DVD-A program/track variants expose
    // channel information through the decoder that can differ from the static
    // file_info metadata (notably downmix / C-LFE variants).  The PCM chunk is the
    // authoritative format that must be handed to libFLAC.
    service_ptr_t<input_info_reader> infoReader;
    input_entry::g_open_for_info_read(infoReader, nullptr, path, abort);
    file_info_impl info;
    infoReader->get_info(subsong, info, abort);
    const double duration = info.get_length();

    service_ptr_t<input_decoder> decoder;
    input_entry::g_open_for_decoding(decoder, nullptr, path, abort);
    decoder->initialize(subsong, input_flag_no_looping | input_flag_playback, abort);

    // Some DVD-A program variants emit one or more setup/priming decoder runs
    // with a zero-sample chunk before the first real PCM block.  Those chunks
    // are harmless and the stream format must be taken from the first non-empty
    // PCM block.  Rejecting the first run here made multichannel/C-LFE tracks
    // fail before libFLAC was even initialized.
    audio_chunk_impl_temporary firstChunk;
    size_t emptyChunks = 0;
    for (;;) {
        abort.check();
        if (!decoder->run(firstChunk, abort))
            throw std::runtime_error("DVD-Audio decoder returned no PCM data after " + std::to_string(emptyChunks) + " empty chunk(s)");

        const size_t firstCount = firstChunk.get_sample_count();
        const unsigned actualChannels = firstChunk.get_channels();
        const unsigned actualSampleRate = firstChunk.get_srate();
        if (firstCount && actualChannels && actualSampleRate) break;

        ++emptyChunks;
        if (emptyChunks >= 64)
            throw std::runtime_error("DVD-Audio decoder returned only empty/invalid PCM chunks (" + std::to_string(emptyChunks) + ")");
    }

    const size_t firstCount = firstChunk.get_sample_count();
    const unsigned actualChannels = firstChunk.get_channels();
    const unsigned actualSampleRate = firstChunk.get_srate();
    if (actualChannels > 8)
        throw std::runtime_error("DVD-Audio decoder returned more than 8 channels; FLAC supports at most 8 channels");

    sampleRate = actualSampleRate;
    channels = actualChannels;
    bitsPerSample = 24;
    totalSamples = 0;

    FlacApi& api = flacApi();
    if (!api.module)
        throw std::runtime_error("libFLAC.dll 1.5.x was not found next to foo_sacd_dlna");

    const std::string utf8Path = utf8FromWide(outputPath);
    FLAC_StreamEncoder* encoder = api.encoder_new();
    if (!encoder) throw std::runtime_error("FLAC encoder allocation failed");

    bool initialized = false;
    auto cleanup = [&] {
        if (initialized && encoder) {
            api.finish(encoder);
            initialized = false;
        }
        if (encoder) {
            api.encoder_delete(encoder);
            encoder = nullptr;
        }
        DeleteFileW(outputPath.c_str());
    };

    if (!api.set_verify(encoder, 1) ||
        !api.set_streamable_subset(encoder, 1) ||
        !api.set_channels(encoder, channels) ||
        !api.set_bits_per_sample(encoder, bitsPerSample) ||
        !api.set_sample_rate(encoder, sampleRate) ||
        !api.set_compression_level(encoder, 5) ||
        !api.set_blocksize(encoder, 4096) ||
        !api.set_do_mid_side_stereo(encoder, channels == 2 ? 1 : 0)) {
        cleanup();
        throw std::runtime_error("unable to configure libFLAC encoder");
    }

    if (duration > 0.0) {
        const uint64_t estimate = static_cast<uint64_t>(std::llround(duration * sampleRate));
        if (estimate) api.set_total_samples_estimate(encoder, estimate);
    }

    std::pair<double, const std::function<void(uint32_t)>*> progressCtx{
        duration > 0.0 ? duration * sampleRate : 0.0, &progress};

    const auto initStatus = api.init_file(encoder, utf8Path.c_str(), flacProgress, &progressCtx);
    if (initStatus != FLAC_INIT_OK) {
        cleanup();
        throw std::runtime_error("libFLAC initialization failed");
    }
    initialized = true;

    auto processChunk = [&](const audio_chunk& chunk) {
        const size_t count = chunk.get_sample_count();
        const unsigned ch = chunk.get_channels();
        const unsigned sr = chunk.get_srate();
        if (!count) return;
        if (ch != channels || sr != sampleRate) {
            throw std::runtime_error(
                "DVD-A decoder PCM format changed during stream (expected " +
                std::to_string(channels) + "ch/" + std::to_string(sampleRate) +
                "Hz, got " + std::to_string(ch) + "ch/" + std::to_string(sr) + "Hz)");
        }

        const audio_sample* samples = chunk.get_data();
        if (!samples) throw std::runtime_error("DVD-A decoder returned PCM data pointer = null");
        std::vector<FLAC_int32> pcm(count * channels);
        for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = floatToInt24(samples[i]);
        if (!api.process_interleaved(encoder, pcm.data(), static_cast<uint32_t>(count)))
            throw std::runtime_error("libFLAC failed while encoding DVD-A PCM");
        totalSamples += count;
        if (progress && duration > 0.0) {
            progress(static_cast<uint32_t>(std::clamp(
                static_cast<double>(totalSamples) / (duration * sampleRate) * 100.0,
                0.0, 100.0)));
        }
    };

    try {
        processChunk(firstChunk);
        for (;;) {
            abort.check();
            audio_chunk_impl_temporary chunk;
            if (!decoder->run(chunk, abort)) break;
            processChunk(chunk);
        }

        if (!totalSamples)
            throw std::runtime_error("DVD-Audio decoder produced no PCM samples");

        const bool finishOk = api.finish(encoder);
        initialized = false;
        if (!finishOk) {
            encoder = nullptr;
            throw std::runtime_error("libFLAC finalization failed");
        }
        api.encoder_delete(encoder);
        encoder = nullptr;
        if (progress) progress(100);
        return true;
    } catch (...) {
        cleanup();
        throw;
    }
}
}
