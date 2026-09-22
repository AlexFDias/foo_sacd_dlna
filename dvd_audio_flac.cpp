#include "stdafx.h"
#include "dvd_audio_flac.h"

namespace dvd_audio_flac {
namespace {

uint8_t crc8(const uint8_t* data, size_t size) {
    uint8_t crc = 0;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned b = 0; b < 8; ++b) crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07) : static_cast<uint8_t>(crc << 1);
    }
    return crc;
}

uint16_t crc16(const uint8_t* data, size_t size) {
    uint16_t crc = 0;
    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (unsigned b = 0; b < 8; ++b) crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x8005) : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

void appendUtf8Uint(std::vector<uint8_t>& out, uint64_t v) {
    if (v < 0x80) { out.push_back(static_cast<uint8_t>(v)); return; }
    if (v < 0x800) { out.push_back(static_cast<uint8_t>(0xC0 | (v >> 6))); out.push_back(static_cast<uint8_t>(0x80 | (v & 0x3F))); return; }
    if (v < 0x10000) { out.push_back(static_cast<uint8_t>(0xE0 | (v >> 12))); out.push_back(static_cast<uint8_t>(0x80 | ((v >> 6) & 0x3F))); out.push_back(static_cast<uint8_t>(0x80 | (v & 0x3F))); return; }
    if (v < 0x200000) { out.push_back(static_cast<uint8_t>(0xF0 | (v >> 18))); out.push_back(static_cast<uint8_t>(0x80 | ((v >> 12) & 0x3F))); out.push_back(static_cast<uint8_t>(0x80 | ((v >> 6) & 0x3F))); out.push_back(static_cast<uint8_t>(0x80 | (v & 0x3F))); return; }
    if (v < 0x4000000) { out.push_back(static_cast<uint8_t>(0xF8 | (v >> 24))); out.push_back(static_cast<uint8_t>(0x80 | ((v >> 18) & 0x3F))); out.push_back(static_cast<uint8_t>(0x80 | ((v >> 12) & 0x3F))); out.push_back(static_cast<uint8_t>(0x80 | ((v >> 6) & 0x3F))); out.push_back(static_cast<uint8_t>(0x80 | (v & 0x3F))); return; }
    out.push_back(static_cast<uint8_t>(0xFC | (v >> 30))); out.push_back(static_cast<uint8_t>(0x80 | ((v >> 24) & 0x3F))); out.push_back(static_cast<uint8_t>(0x80 | ((v >> 18) & 0x3F))); out.push_back(static_cast<uint8_t>(0x80 | ((v >> 12) & 0x3F))); out.push_back(static_cast<uint8_t>(0x80 | ((v >> 6) & 0x3F))); out.push_back(static_cast<uint8_t>(0x80 | (v & 0x3F)));
}

uint8_t sampleRateCode(uint32_t rate, std::vector<uint8_t>& extra) {
    extra.clear();
    switch (rate) {
        case 88200: return 1;
        case 176400: return 2;
        case 8000: return 3;
        case 16000: return 4;
        case 22050: return 5;
        case 24000: return 6;
        case 32000: return 7;
        case 44100: return 8;
        case 48000: return 9;
        case 96000: return 10;
        case 192000: return 11;
        default:
            if (rate % 10 == 0 && rate / 10 <= 65535) {
                extra.push_back(static_cast<uint8_t>((rate / 10) >> 8));
                extra.push_back(static_cast<uint8_t>((rate / 10) & 0xFF));
                return 14;
            }
            if (rate / 1000 <= 255 && rate % 1000 == 0) {
                extra.push_back(static_cast<uint8_t>(rate / 1000));
                return 12;
            }
            if (rate <= 65535) {
                extra.push_back(static_cast<uint8_t>(rate >> 8));
                extra.push_back(static_cast<uint8_t>(rate & 0xFF));
                return 13;
            }
            throw std::runtime_error("unsupported DVD-A sample rate for FLAC frame header");
    }
}

void putBE16(std::ostream& os, uint16_t v) { os.put(static_cast<char>(v >> 8)); os.put(static_cast<char>(v)); }
void putBE24(std::ostream& os, uint32_t v) { os.put(static_cast<char>(v >> 16)); os.put(static_cast<char>(v >> 8)); os.put(static_cast<char>(v)); }
void putBE32(std::ostream& os, uint32_t v) { os.put(static_cast<char>(v >> 24)); os.put(static_cast<char>(v >> 16)); os.put(static_cast<char>(v >> 8)); os.put(static_cast<char>(v)); }
void putBE64(std::ostream& os, uint64_t v) {
    for (int i = 7; i >= 0; --i) os.put(static_cast<char>(v >> (i * 8)));
}

// FLAC frame with independent 24-bit verbatim subframes. This is deliberately
// simple rather than compression-oriented: the output is lossless and valid on
// every FLAC decoder, including renderers that are strict about channel layout.
void writeFrame(std::ofstream& out, const std::vector<int32_t>& pcm, size_t frames,
                uint32_t channels, uint32_t rate, uint64_t frameNumber) {
    std::vector<uint8_t> header;
    header.reserve(32);
    header.push_back(0xFF); header.push_back(0xF8); // sync + fixed-blocking strategy
    std::vector<uint8_t> rateExtra;
    const uint8_t rateCode = sampleRateCode(rate, rateExtra);
    header.push_back(static_cast<uint8_t>((7u << 4) | rateCode)); // 7 = 16-bit block-size field
    header.push_back(static_cast<uint8_t>(((channels - 1u) << 4) | 6u)); // independent channels, 24-bit
    appendUtf8Uint(header, frameNumber);
    header.push_back(static_cast<uint8_t>((frames - 1) >> 8));
    header.push_back(static_cast<uint8_t>((frames - 1) & 0xFF));
    header.insert(header.end(), rateExtra.begin(), rateExtra.end());
    header.push_back(crc8(header.data(), header.size()));
    out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));

    std::vector<uint8_t> frameBody;
    frameBody.reserve(frames * channels * 3 + channels);
    for (uint32_t ch = 0; ch < channels; ++ch) {
        frameBody.push_back(0x02); // verbatim subframe header
        for (size_t i = 0; i < frames; ++i) {
            const int32_t sample = pcm[i * channels + ch];
            frameBody.push_back(static_cast<uint8_t>((sample >> 16) & 0xFF));
            frameBody.push_back(static_cast<uint8_t>((sample >> 8) & 0xFF));
            frameBody.push_back(static_cast<uint8_t>(sample & 0xFF));
        }
    }
    out.write(reinterpret_cast<const char*>(frameBody.data()), static_cast<std::streamsize>(frameBody.size()));
    std::vector<uint8_t> crcInput;
    crcInput.reserve(header.size() - 1 + frameBody.size());
    crcInput.insert(crcInput.end(), header.begin(), header.end() - 1);
    crcInput.insert(crcInput.end(), frameBody.begin(), frameBody.end());
    putBE16(out, crc16(crcInput.data(), crcInput.size()));
}

}

bool decode_to_flac(const char* path, t_uint32 subsong, const std::wstring& outputPath,
                    abort_callback& abort, uint32_t& sampleRate, uint32_t& channels,
                    uint32_t& bitsPerSample, uint64_t& totalSamples,
                    const std::function<void(uint32_t)>& progress) {
    service_ptr_t<input_info_reader> infoReader;
    input_entry::g_open_for_info_read(infoReader, nullptr, path, abort);
    file_info_impl info;
    infoReader->get_info(subsong, info, abort);
    sampleRate = static_cast<uint32_t>(std::max<t_int64>(1, info.info_get_int("samplerate")));
    channels = static_cast<uint32_t>(std::max<t_int64>(1, info.info_get_int("channels")));
    bitsPerSample = 24;
    if (channels > 8) throw std::runtime_error("DVD-Audio has more than 8 channels; FLAC channel mapping is unsupported");
    const double duration = info.get_length();

    service_ptr_t<input_decoder> decoder;
    input_entry::g_open_for_decoding(decoder, nullptr, path, abort);
    decoder->initialize(subsong, input_flag_no_seeking | input_flag_no_looping | input_flag_playback, abort);

    std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("unable to create DVD-A FLAC cache");

    // STREAMINFO block: placeholder total sample count, patched after decoding.
    out.write("fLaC", 4);
    out.put(static_cast<char>(0x80)); out.put(static_cast<char>(0x00)); out.put(static_cast<char>(0x22)); // last metadata block, STREAMINFO length 34
    putBE16(out, 4096); putBE16(out, 4096);
    putBE24(out, 0); putBE24(out, 0);
    const std::streamoff streamInfoPayload = out.tellp();
    uint64_t packed = (static_cast<uint64_t>(sampleRate) << 44) |
        (static_cast<uint64_t>(channels - 1) << 41) |
        (static_cast<uint64_t>(bitsPerSample - 1) << 36);
    putBE64(out, packed); // top 8 bytes contain rate/channels/bps/totalSamples; low 4 are patched below
    putBE32(out, 0);      // md5 placeholder (first 4 bytes of 16-byte MD5 field)
    uint8_t md5zero[12]{}; out.write(reinterpret_cast<const char*>(md5zero), 12);
    const std::streamoff dataStart = out.tellp();
    (void)dataStart;

    std::vector<int32_t> pcm;
    pcm.reserve(4096 * channels);
    uint64_t decoded = 0;
    uint64_t frameNumber = 0;
    for (;;) {
        abort.check();
        audio_chunk_impl_temporary chunk;
        if (!decoder->run(chunk, abort)) break;
        const size_t count = chunk.get_sample_count();
        const unsigned ch = chunk.get_channels();
        if (!count || ch != channels) throw std::runtime_error("DVD-A decoder changed channel count during decoding");
        std::vector<uint8_t> raw(count * channels * 3 + 4);
        audio_math::convert_to_int24(chunk.get_data(), count * channels, raw.data(), 1.0);
        for (size_t i = 0; i < count * channels; ++i) {
            const uint8_t* p = raw.data() + i * 3;
            int32_t v = static_cast<int32_t>(p[0]) | (static_cast<int32_t>(p[1]) << 8) | (static_cast<int32_t>(p[2]) << 16);
            if (v & 0x800000) v |= ~0xFFFFFF;
            pcm.push_back(v);
        }
        size_t framesAvailable = pcm.size() / channels;
        while (framesAvailable >= 4096) {
            std::vector<int32_t> block(pcm.begin(), pcm.begin() + 4096 * channels);
            writeFrame(out, block, 4096, channels, sampleRate, frameNumber++);
            pcm.erase(pcm.begin(), pcm.begin() + 4096 * channels);
            decoded += 4096;
            framesAvailable -= 4096;
            if (progress && duration > 0) progress(static_cast<uint32_t>(std::clamp(decoded / duration / sampleRate * 100.0, 0.0, 100.0)));
        }
    }
    if (!pcm.empty()) {
        const size_t frames = pcm.size() / channels;
        writeFrame(out, pcm, frames, channels, sampleRate, frameNumber++);
        decoded += frames;
    }
    out.flush();
    if (!out) throw std::runtime_error("unable to finalize DVD-A FLAC cache");
    out.close();

    // Patch totalSamples into STREAMINFO. The packed 64-bit value occupies the first
    // eight bytes of STREAMINFO and is followed by the 16-byte MD5 placeholder.
    std::fstream patch(outputPath, std::ios::binary | std::ios::in | std::ios::out);
    if (!patch) throw std::runtime_error("unable to reopen DVD-A FLAC cache");
    patch.seekp(streamInfoPayload);
    packed = (static_cast<uint64_t>(sampleRate) << 44) |
        (static_cast<uint64_t>(channels - 1) << 41) |
        (static_cast<uint64_t>(bitsPerSample - 1) << 36) |
        (decoded & 0xFFFFFFFFFULL);
    putBE64(patch, packed);
    patch.close();
    totalSamples = decoded;
    if (progress) progress(100);
    return decoded != 0;
}

}
