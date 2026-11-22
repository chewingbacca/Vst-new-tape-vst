// Minimal WAV writer/reader for offline tools and tests (not used on the audio thread).
#pragma once
#include <algorithm>
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

// Reads 16/24-bit PCM or 32-bit float WAV. Returns false on failure.
inline bool readWav (const std::string& path, std::vector<std::vector<float>>& channels, int& sampleRate)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) return false;
    std::fseek (f, 0, SEEK_END); const long size = std::ftell (f); std::fseek (f, 0, SEEK_SET);
    std::vector<uint8_t> d ((size_t) std::max (0L, size));
    const size_t got = std::fread (d.data(), 1, d.size(), f);
    std::fclose (f);
    if (got < 44 || std::memcmp (d.data(), "RIFF", 4) != 0 || std::memcmp (d.data() + 8, "WAVE", 4) != 0) return false;
    size_t pos = 12; uint16_t tag = 0, nch = 0, bits = 0; uint32_t sr = 0; const uint8_t* raw = nullptr; uint32_t rawSize = 0;
    while (pos + 8 <= got)
    {
        uint32_t sz; std::memcpy (&sz, d.data() + pos + 4, 4);
        if (std::memcmp (d.data() + pos, "fmt ", 4) == 0 && pos + 24 <= got)
        { std::memcpy (&tag, d.data() + pos + 8, 2); std::memcpy (&nch, d.data() + pos + 10, 2); std::memcpy (&sr, d.data() + pos + 12, 4); std::memcpy (&bits, d.data() + pos + 22, 2); }
        else if (std::memcmp (d.data() + pos, "data", 4) == 0) { raw = d.data() + pos + 8; rawSize = (uint32_t) std::min<size_t> (sz, got - pos - 8); }
        pos += 8 + sz + (sz & 1);
    }
    if (raw == nullptr || nch == 0 || bits == 0) return false;
    const uint32_t bytes = bits / 8, frames = rawSize / (bytes * nch);
    channels.assign (nch, std::vector<float> (frames));
    for (uint32_t i = 0; i < frames; ++i)
        for (uint16_t c = 0; c < nch; ++c)
        {
            const uint8_t* p = raw + (size_t) (i * nch + c) * bytes;
            float v = 0;
            if (bits == 32 && tag == 3) std::memcpy (&v, p, 4);
            else if (bits == 16) { int16_t x; std::memcpy (&x, p, 2); v = (float) x / 32768.f; }
            else if (bits == 24) { int32_t x = p[0] | (p[1] << 8) | (p[2] << 16); if (x & 0x800000) x -= 0x1000000; v = (float) x / 8388608.f; }
            else if (bits == 32) { int32_t x; std::memcpy (&x, p, 4); v = (float) x / 2147483648.f; }
            channels[c][i] = v;
        }
    sampleRate = (int) sr;
    return true;
}

} // namespace atm
