#include "stdafx.h"
#include "dsf_writer.h"

namespace {
static void put4(std::fstream& f, const char s[4]) { f.write(s, 4); }
}

DsfWriter::DsfWriter(const std::wstring& path) {
    m_file.open(path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!m_file) throw std::runtime_error("Unable to create DSF cache file");
}

DsfWriter::~DsfWriter() {
    if (m_file.is_open()) m_file.close();
}

void DsfWriter::writeU32(uint32_t v) { m_file.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
void DsfWriter::writeU64(uint64_t v) { m_file.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
void DsfWriter::writeBytes(const void* p, size_t n) { m_file.write(reinterpret_cast<const char*>(p), static_cast<std::streamsize>(n)); }

void DsfWriter::begin(uint32_t dsdRate, uint32_t channels) {
    if (channels != 2) throw std::runtime_error("Only stereo DSF is supported in this version");
    if (dsdRate != 2822400 && dsdRate != 5644800 && dsdRate != 11289600)
        throw std::runtime_error("Only DSD64/DSD128/DSD256 (44.1-family) are enabled for the T+A streaming path");

    m_rate = dsdRate;
    m_channels = channels;

    // DSD chunk, 28 bytes.
    put4(m_file, "DSD ");
    writeU64(28);
    writeU64(0); // file size, patched at finish
    writeU64(0); // metadata offset, no ID3 metadata in this build

    // fmt chunk, 52 bytes.
    put4(m_file, "fmt ");
    writeU64(52);
    writeU32(1); // format version
    writeU32(0); // format ID
    writeU32(2); // stereo channel type
    writeU32(channels);
    writeU32(dsdRate);
    writeU32(1); // 1 bit per sample
    writeU64(0); // sample count, patched at finish
    writeU32(4096); // block size per channel
    writeU32(0); // reserved

    // data chunk begins at byte 80 in a normal DSF header.
    m_dataOffset = static_cast<uint64_t>(m_file.tellp());
    put4(m_file, "data");
    writeU64(12); // patched at finish
    m_fileSize = static_cast<uint64_t>(m_file.tellp());
}

void DsfWriter::append(const std::vector<std::vector<uint8_t>>& channelBytes) {
    if (channelBytes.size() != m_channels || channelBytes.empty())
        throw std::runtime_error("Invalid DSF channel count");
    const size_t n = channelBytes[0].size();
    if (channelBytes[1].size() != n || (n & 1))
        throw std::runtime_error("Mismatched DSD channel data");

    // DSF stores 4096-byte blocks per channel, interleaved by channel.
    // Keep partial input chunks buffered so a decoder chunk boundary can never
    // become an invalid DSF block boundary.
    for (uint32_t ch = 0; ch < m_channels; ++ch) {
        m_pending[ch].insert(m_pending[ch].end(), channelBytes[ch].begin(), channelBytes[ch].end());
    }

    while (m_pending[0].size() >= 4096 && m_pending[1].size() >= 4096) {
        writeBytes(m_pending[0].data(), 4096);
        writeBytes(m_pending[1].data(), 4096);
        m_pending[0].erase(m_pending[0].begin(), m_pending[0].begin() + 4096);
        m_pending[1].erase(m_pending[1].begin(), m_pending[1].begin() + 4096);
        m_bytesPerChannel += 4096;
    }
    m_fileSize = static_cast<uint64_t>(m_file.tellp());
}

void DsfWriter::finish(uint64_t samplesPerChannel) {
    if (!m_file) throw std::runtime_error("DSF write failed before finish");

    if (m_pending[0].size() != m_pending[1].size())
        throw std::runtime_error("DSF channel block sizes became unbalanced");
    if (!m_pending[0].empty()) {
        writeBytes(m_pending[0].data(), m_pending[0].size());
        writeBytes(m_pending[1].data(), m_pending[1].size());
        m_bytesPerChannel += m_pending[0].size();
        m_pending[0].clear();
        m_pending[1].clear();
    }
    m_fileSize = static_cast<uint64_t>(m_file.tellp());

    const uint64_t dataChunkSize = 12 + m_bytesPerChannel * m_channels;

    // File size field in DSD chunk: offset 12.
    m_file.seekp(12, std::ios::beg);
    writeU64(m_fileSize);

    // fmt sample-count field: fmt starts at 28 and the field begins 36 bytes into it => 64.
    m_file.seekp(64, std::ios::beg);
    writeU64(samplesPerChannel);

    // data chunk size: data header starts at m_dataOffset, size at +4.
    m_file.seekp(static_cast<std::streamoff>(m_dataOffset + 4), std::ios::beg);
    writeU64(dataChunkSize);

    m_file.flush();
}
