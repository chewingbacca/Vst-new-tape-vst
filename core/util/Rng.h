// Deterministic hashing and random numbers.
//
// The sequencer never keeps a running generator: every random decision is
// hash(seed, track, step, salt...) so it can be recomputed from song position.
// DSP noise sources use the small xorshift generator below, re-seeded on note-on.
#pragma once
#include <cstdint>

namespace atm {

inline uint32_t hash32 (uint32_t x)
{
    // "lowbias32" by Chris Wellons (public domain)
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

inline uint32_t hashCombine (uint32_t h, uint32_t v) { return hash32 (h ^ (v + 0x9e3779b9U + (h << 6) + (h >> 2))); }

inline uint32_t hashN (uint32_t a, uint32_t b) { return hashCombine (hash32 (a), b); }
inline uint32_t hashN (uint32_t a, uint32_t b, uint32_t c) { return hashCombine (hashN (a, b), c); }
inline uint32_t hashN (uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return hashCombine (hashN (a, b, c), d); }
inline uint32_t hashN (uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { return hashCombine (hashN (a, b, c, d), e); }

// uniform in [0,1)
inline float hashToFloat (uint32_t h) { return (float) (h >> 8) * (1.0f / 16777216.0f); }
// uniform in [-1,1)
inline float hashToBipolar (uint32_t h) { return hashToFloat (h) * 2.f - 1.f; }

// Sequential generator for generators/pattern building (message thread or bounded audio-thread work).
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed = 1) : s (hash32 (seed) | 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return hash32 (s); }
    float uni() { return hashToFloat (next()); }                       // [0,1)
    float bip() { return uni() * 2.f - 1.f; }                          // [-1,1)
    float range (float lo, float hi) { return lo + (hi - lo) * uni(); }
    int   irange (int lo, int hi) { return lo + (int) (uni() * (float) (hi - lo + 1)); } // inclusive
    bool  chance (float p) { return uni() < p; }
    template <class T, int N> const T& pick (const T (&arr)[N]) { return arr[irange (0, N - 1)]; }
    // weighted pick: returns index
    int weighted (const float* w, int n)
    {
        float sum = 0; for (int i = 0; i < n; ++i) sum += w[i];
        float r = uni() * sum;
        for (int i = 0; i < n; ++i) { r -= w[i]; if (r < 0) return i; }
        return n - 1;
    }
};

// Fast audio-rate white noise (xorshift32), output in [-1,1).
struct Noise
{
    uint32_t s = 0x12345678u;
    void seed (uint32_t v) { s = hash32 (v) | 1u; }
    inline float next()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (int32_t) s * (1.0f / 2147483648.0f);
    }
    inline float uni() { return next() * 0.5f + 0.5f; }
};

} // namespace atm
