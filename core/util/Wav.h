// Minimal WAV writer/reader for offline tools and tests (not used on the audio thread).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace atm {

inline bool writeWav (const std::string& path, const std::vector<std::vector<float>>& channels, int sampleRate, int bits = 24)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return false;
    const uint16_t nch = (uint16_t) channels.size();
    const uint32_t frames = nch ? (uint32_t) channels[0].size() : 0;
    const uint16_t bytes = (uint16_t) (bits / 8);
    const uint32_t dataSize = frames * nch * bytes;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataSize); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (bits == 32 ? 3 : 1); w16 (nch); w32 ((uint32_t) sampleRate);
    w32 ((uint32_t) sampleRate * nch * bytes); w16 ((uint16_t) (nch * bytes)); w16 ((uint16_t) bits);
    std::fwrite ("data", 1, 4, f); w32 (dataSize);
    std::vector<uint8_t> buf; buf.reserve (dataSize);
    for (uint32_t i = 0; i < frames; ++i)
        for (uint16_t c = 0; c < nch; ++c)
        {
            float x = channels[c][i];
            if (bits == 32) { uint8_t b[4]; std::memcpy (b, &x, 4); buf.insert (buf.end(), b, b + 4); continue; }
            if (x > 1.f) x = 1.f; if (x < -1.f) x = -1.f;
            if (bits == 16) { const int16_t v = (int16_t) (x * 32767.f); buf.push_back ((uint8_t) (v & 0xff)); buf.push_back ((uint8_t) ((v >> 8) & 0xff)); }
            else { const int32_t v = (int32_t) (x * 8388607.f); buf.push_back ((uint8_t) (v & 0xff)); buf.push_back ((uint8_t) ((v >> 8) & 0xff)); buf.push_back ((uint8_t) ((v >> 16) & 0xff)); }
        }
    std::fwrite (buf.data(), 1, buf.size(), f);
    std::fclose (f);
    return true;
}

} // namespace atm
