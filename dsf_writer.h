#pragma once
#include "stdafx.h"

struct DsdTrack {
    std::string title;
    std::string artist;
    std::string album;
    std::string albumArtist;
    std::string genre;
    std::string date;
    std::string composer;
    std::string publisher;
    std::string comment;
    std::string trackNumber;
    std::string discNumber;
    std::string totalTracks;
    std::string totalDiscs;
    uint32_t dsdRate = 0;
    uint32_t channels = 2;
    uint32_t bitsPerSample = 1;
    uint64_t dsdSamplesPerChannel = 0;
    uint64_t fileSize = 0;
    double duration = 0.0;
    std::wstring path;
};

class DsfWriter {
public:
    explicit DsfWriter(const std::wstring& path);
    ~DsfWriter();

    void begin(uint32_t dsdRate, uint32_t channels = 2);
    void append(const std::vector<std::vector<uint8_t>>& channelBytes);
    void finish(uint64_t samplesPerChannel);
    uint64_t currentFileSize() const noexcept { return m_fileSize; }

private:
    std::fstream m_file;
    uint32_t m_rate = 0;
    uint32_t m_channels = 0;
    uint64_t m_bytesPerChannel = 0;
    uint64_t m_dataOffset = 0;
    uint64_t m_fileSize = 0;
    std::vector<uint8_t> m_pending[2];

    void writeU32(uint32_t v);
    void writeU64(uint64_t v);
    void writeBytes(const void* p, size_t n);
};
