// The macro laws, in one place: the engine applies them, the factory presets
// invert them (so a preset can be written as "what you hear" plus macro
// positions, and the per-module base values are solved from that).
#pragma once

#include "Params.h"
#include "Util.h"

namespace swoon::macro
{

/** "Amount" law for mixes: 0 -> nothing, 0.5 -> the module's own value, 1 -> pushed 70 % of the way to full. */
inline float amount (float m, float base) noexcept
{
    m = clamp01 (m);
    if (m <= 0.5f)
    {
        const float t = m * 2.0f;
        return base * t * (2.0f - t);
    }
    return base + (1.0f - base) * (m * 2.0f - 1.0f) * 0.7f;
}

/** Base value that makes amount (m, base) == target (clamped to 0..1 when that is not reachable). */
inline float amountInverse (float m, float target) noexcept
{
    m = clamp01 (m);
    if (m <= 0.5f)
    {
        const float t = m * 2.0f, k = t * (2.0f - t);
        return k <= 1.0e-4f ? 0.0f : clamp01 (target / k);
    }
    const float k = (m * 2.0f - 1.0f) * 0.7f;
    return clamp01 ((target - k) / (1.0f - k));
}

// ---- Wall: fuzz amount and density -------------------------------------------------------
inline float sustainOffset (float wall) noexcept { return (wall - 0.5f) * 0.5f; }
inline float stackOffset (float wall) noexcept   { return (wall - 0.5f) * 0.6f; }
inline float ampDriveOffset (float wall) noexcept { return std::max (0.0f, wall - 0.5f) * 0.3f; }

// ---- Wash: reverb size and mix -----------------------------------------------------------
inline float decayMul (float wash) noexcept    { return std::pow (2.0f, (wash - 0.5f) * 3.0f); }
inline float sizeOffset (float wash) noexcept  { return (wash - 0.5f) * 0.4f; }

// ---- Glide: pitch drift ------------------------------------------------------------------
inline float glideDepthMul (float glide) noexcept { return 2.0f * glide; }
inline float glideMixMul (float glide) noexcept   { return clamp01 (glide * 8.0f); }

// ---- Blur: soft focus and modulation -----------------------------------------------------
inline float modDepthMul (float blur) noexcept { return 0.7f + 0.6f * blur; }
inline float detuneMul (float blur) noexcept   { return 0.6f + 0.8f * blur; }
inline float hazeOffset (float blur) noexcept  { return (blur - 0.5f) * 0.4f; }

// ---- Warmth: darker, softer, more saturated above 0.5; brighter and cleaner below ---------
inline float fuzzToneOffset (float warmth) noexcept   { return -(warmth - 0.5f) * 0.35f; }
inline float ampTrebleOffset (float warmth) noexcept  { return -(warmth - 0.5f) * 0.25f; }
inline float toneOffset (float warmth) noexcept       { return -(warmth - 0.5f) * 0.3f; }     // focus, reverse
inline float washDampMul (float warmth) noexcept      { return std::pow (2.0f, -(warmth - 0.5f) * 2.4f); }
inline float tapeDriveOffset (float warmth) noexcept  { return (warmth - 0.5f) * 0.4f; }
inline float tapeAgeOffset (float warmth) noexcept    { return (warmth - 0.5f) * 0.5f; }
inline float tiltOffset (float warmth) noexcept       { return -(warmth - 0.5f) * 0.9f; }
inline float warmShelfDb (float warmth) noexcept      { return (warmth - 0.5f) * 5.0f; }

// ---- tempo sync (Focus and Reverse) --------------------------------------------------------
/** Sync choice -> time in ms at the given tempo. Choice 0 ("Free") returns freeMs. */
inline float syncedMs (int choice, float bpm, float freeMs) noexcept
{
    static constexpr float beats[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    if (choice <= 0 || choice > 6)
        return freeMs;
    return beats[choice] * 60000.0f / std::clamp (bpm, 20.0f, 400.0f);
}

} // namespace swoon::macro
