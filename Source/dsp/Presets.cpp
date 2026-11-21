#include "Presets.h"

#include "Macros.h"

#include <cmath>
#include <initializer_list>
#include <string_view>

namespace swoon
{

bool isPresetExempt (P p)
{
    return p == P::bypass || p == P::inGain;
}

ParamValues presetValues (const PresetDef& preset)
{
    ParamValues pv;
    for (const auto& [p, value] : preset.values)
        if (! isPresetExempt (p))
            pv[p] = value;
    return pv;
}

namespace
{
// choice values, named for readability
enum : int { Sustain = 0, Octave = 1, Velcro = 2 };
enum : int { Flat = 0, Full = 1, Scoop = 2 };
enum : int { Swell = 0, TapeRev = 1, Both = 2 };
enum : int { Chorus = 0, Ensemble = 1, Vibrato = 2, Flanger = 3, Phaser = 4 };
enum : int { OctUp = 0, FifthUp = 1, OctDown = 2, UpDown = 3 };
enum : int { Direct = 0, Open212 = 1, Closed412 = 2, Chime112 = 3 };

using Settings = std::initializer_list<std::pair<P, float>>;

float clampTo (P p, float v)
{
    const auto& d = def (p);
    return std::clamp (v, d.min, d.max);
}

/** Presets are written as the sound you hear ("effective" values) plus macro positions. This solves the
    per-module base values that produce that sound at those macro positions, so the macro knobs can sit
    anywhere and still mean something. With a macro at zero its effect is silent, so the written values are
    taken as the module's own settings - what you get as the macro is brought up to noon. */
PresetDef build (const char* name, const char* category, const char* note, Settings heard,
                 std::initializer_list<Slot> order = {})
{
    ParamValues v;                       // starts from defaults
    for (const auto& [p, value] : heard)
        v[p] = value;

    const float wall = v[P::wall], wash = v[P::wash], glide = v[P::glide], blur = v[P::blur];

    if (wall > 0.02f)
    {
        v[P::fuzzMix] = macro::amountInverse (wall, v[P::fuzzMix]);
        v[P::fuzzSustain] -= macro::sustainOffset (wall);
        v[P::fuzzStack] -= macro::stackOffset (wall);
        v[P::ampDrive] -= macro::ampDriveOffset (wall);
    }
    if (wash > 0.02f)
    {
        v[P::washMix] = macro::amountInverse (wash, v[P::washMix]);
        v[P::washDecay] /= macro::decayMul (wash);
        v[P::washSize] -= macro::sizeOffset (wash);
    }
    if (glide > 0.02f)
    {
        const float k = macro::glideDepthMul (glide);
        v[P::glideWobble] /= k;
        v[P::glideDrift] /= k;
        v[P::glideDip] /= k;
        v[P::glideMix] /= macro::glideMixMul (glide);
    }
    if (blur > 0.02f)
    {
        v[P::focMix] = macro::amountInverse (blur, v[P::focMix]);
        v[P::modMix] = macro::amountInverse (blur, v[P::modMix]);
        v[P::modDepth] /= macro::modDepthMul (blur);
        v[P::focDetune] /= macro::detuneMul (blur);
        v[P::focHaze] -= macro::hazeOffset (blur);
    }

    if (order.size() == static_cast<size_t> (kNumSlots))
    {
        int position = 1;
        for (Slot s : order)
            v[kOrderParams[static_cast<size_t> (s)]] = static_cast<float> (position++);
    }

    PresetDef preset { name, category, note, {} };
    const ParamValues defaults;
    for (const auto& d : paramDefs())
    {
        if (isPresetExempt (d.index))
            continue;
        float value = clampTo (d.index, v[d.index]);
        if (d.kind != ParamKind::Float)
            value = std::round (value);
        if (std::abs (value - defaults[d.index]) > 1.0e-6f)
            preset.values.emplace_back (d.index, value);
    }
    return preset;
}

std::vector<PresetDef> makeFactoryPresets()
{
    using S = Slot;
    std::vector<PresetDef> p;

    // =====================================================================================
    // Signature
    // =====================================================================================
    p.push_back (build ("Swoon", "Wall",
        "The house sound: sustaining fuzz dissolving into a slow hall, pitch gently adrift.", {}));

    // =====================================================================================
    // Dream Pop - clean to lightly driven, soft-focus shimmer
    // =====================================================================================
    p.push_back (build ("Soft Focus Morning", "Dream Pop",
        "Twin delays, detuned voices and a plate of haze over a clean amp. Raise Wall for grit.", {
            { P::wall, 0.0f }, { P::wash, 0.5f }, { P::glide, 0.3f }, { P::blur, 0.65f },
            { P::glideWobble, 4.0f }, { P::glideDrift, 6.0f }, { P::glideDip, 0.0f }, { P::glideSpread, 0.25f },
            { P::ampDrive, 0.2f }, { P::ampTreble, 0.55f }, { P::ampCab, Open212 },
            { P::modMode, Ensemble }, { P::modRate, 0.35f }, { P::modDepth, 0.45f }, { P::modMix, 0.3f }, { P::modWidth, 1.0f },
            { P::focTime, 250.0f }, { P::focFeedback, 0.55f }, { P::focDetune, 9.0f }, { P::focHaze, 0.6f }, { P::focTone, 0.6f }, { P::focMix, 0.5f },
            { P::washDecay, 4.5f }, { P::washSize, 0.65f }, { P::washDamp, 7000.0f }, { P::washMix, 0.32f }, { P::washMod, 0.5f },
            { P::tapeDrive, 0.25f }, { P::tapeWow, 0.15f }, { P::tapeFlutter, 0.1f }, { P::tapeAge, 0.25f },
        }));

    p.push_back (build ("Glass Lanterns", "Dream Pop",
        "Chiming and wide: chorus placed after the delays so every repeat wobbles a little differently.", {
            { P::wall, 0.0f }, { P::wash, 0.45f }, { P::glide, 0.25f }, { P::blur, 0.6f }, { P::warmth, 0.42f },
            { P::glideWobble, 3.0f }, { P::glideDrift, 5.0f }, { P::glideDip, 0.0f },
            { P::ampDrive, 0.25f }, { P::ampTreble, 0.6f }, { P::ampCab, Chime112 },
            { P::modMode, Chorus }, { P::modRate, 0.7f }, { P::modDepth, 0.6f }, { P::modColor, 0.35f }, { P::modMix, 0.55f },
            { P::focTime, 375.0f }, { P::focFeedback, 0.45f }, { P::focDetune, 12.0f }, { P::focHaze, 0.35f }, { P::focTone, 0.7f }, { P::focMix, 0.4f },
            { P::washDecay, 3.2f }, { P::washSize, 0.5f }, { P::washDamp, 9000.0f }, { P::washMix, 0.25f },
            { P::tapeDrive, 0.2f }, { P::tapeWow, 0.1f }, { P::outTilt, 0.12f },
        }, { S::Glide, S::Fuzz, S::Amp, S::Focus, S::Mod, S::Reverse, S::Wash }));

    p.push_back (build ("Souvenir Tape", "Dream Pop",
        "A cassette left in the sun: saturated, wobbling, a little dim around the edges.", {
            { P::wall, 0.0f }, { P::wash, 0.4f }, { P::glide, 0.45f }, { P::blur, 0.45f }, { P::warmth, 0.7f },
            { P::glideWobble, 6.0f }, { P::glideDrift, 14.0f }, { P::glideDip, 10.0f },
            { P::ampDrive, 0.45f }, { P::ampCab, Open212 },
            { P::modMode, Vibrato }, { P::modRate, 4.2f }, { P::modDepth, 0.25f }, { P::modMix, 0.3f },
            { P::focTime, 300.0f }, { P::focFeedback, 0.35f }, { P::focMix, 0.25f },
            { P::washDecay, 2.8f }, { P::washDamp, 4500.0f }, { P::washMix, 0.25f },
            { P::tapeDrive, 0.55f }, { P::tapeWow, 0.55f }, { P::tapeFlutter, 0.4f }, { P::tapeAge, 0.6f },
        }));

    p.push_back (build ("Pale Blue Hours", "Dream Pop",
        "A thread of fuzz under a clean guitar; the hall ducks while you play and opens between phrases.", {
            { P::wall, 0.3f }, { P::wash, 0.6f }, { P::glide, 0.4f }, { P::blur, 0.5f },
            { P::fuzzVoice, Flat }, { P::fuzzSustain, 0.4f }, { P::fuzzTone, 0.45f }, { P::fuzzStack, 0.2f }, { P::fuzzMix, 0.32f }, { P::fuzzLevel, -4.0f },
            { P::glideWobble, 6.0f }, { P::glideDrift, 8.0f }, { P::glideDip, 15.0f },
            { P::ampDrive, 0.3f }, { P::ampCab, Open212 },
            { P::modMode, Chorus }, { P::modMix, 0.35f },
            { P::focMix, 0.3f },
            { P::washDecay, 8.0f }, { P::washSize, 0.8f }, { P::washPre, 40.0f }, { P::washDuck, 0.45f }, { P::washMix, 0.42f },
        }));

    p.push_back (build ("Lullaby Engine", "Dream Pop",
        "Swell softens every attack; an ensemble and long delays turn chords into a slow-breathing pad.", {
            { P::wall, 0.0f }, { P::wash, 0.7f }, { P::glide, 0.3f }, { P::blur, 0.75f },
            { P::inSwell, 0.6f },
            { P::glideWobble, 4.0f }, { P::glideDrift, 8.0f }, { P::glideDip, 0.0f },
            { P::ampDrive, 0.2f }, { P::ampCab, Open212 },
            { P::modMode, Ensemble }, { P::modRate, 0.28f }, { P::modDepth, 0.6f }, { P::modMix, 0.45f },
            { P::focTime, 420.0f }, { P::focFeedback, 0.6f }, { P::focHaze, 0.7f }, { P::focMix, 0.5f },
            { P::washDecay, 9.0f }, { P::washSize, 0.85f }, { P::washShimmer, 0.1f }, { P::washMix, 0.5f },
            { P::tapeAge, 0.4f },
        }));

    p.push_back (build ("Sea Glass", "Dream Pop",
        "Bright, cool and slowly phasing, with the low end kept out of the reverb.", {
            { P::wall, 0.0f }, { P::wash, 0.55f }, { P::glide, 0.2f }, { P::blur, 0.5f }, { P::warmth, 0.38f },
            { P::glideWobble, 3.0f }, { P::glideDrift, 4.0f }, { P::glideDip, 0.0f },
            { P::ampDrive, 0.22f }, { P::ampTreble, 0.6f }, { P::ampCab, Chime112 },
            { P::modMode, Phaser }, { P::modRate, 0.12f }, { P::modDepth, 0.5f }, { P::modColor, 0.3f }, { P::modMix, 0.35f },
            { P::focTime, 380.0f }, { P::focFeedback, 0.5f }, { P::focMix, 0.35f }, { P::focTone, 0.75f },
            { P::washDecay, 6.0f }, { P::washDamp, 11000.0f }, { P::washLow, 220.0f }, { P::washMod, 0.6f }, { P::washMix, 0.36f },
            { P::outTilt, 0.15f },
        }));

    p.push_back (build ("Honeyed Static", "Dream Pop",
        "Amp and tape pushed until they purr, a little fuzz folded in underneath.", {
            { P::wall, 0.3f }, { P::wash, 0.4f }, { P::glide, 0.35f }, { P::blur, 0.4f }, { P::warmth, 0.65f },
            { P::fuzzVoice, Flat }, { P::fuzzSustain, 0.3f }, { P::fuzzMix, 0.25f }, { P::fuzzLevel, -5.0f }, { P::fuzzStack, 0.15f },
            { P::glideWobble, 5.0f }, { P::glideDrift, 8.0f }, { P::glideDip, 12.0f },
            { P::ampDrive, 0.75f }, { P::ampCab, Open212 }, { P::ampBass, 0.55f },
            { P::modMix, 0.25f }, { P::focMix, 0.2f },
            { P::washDecay, 3.0f }, { P::washMix, 0.22f },
            { P::tapeDrive, 0.6f }, { P::tapeAge, 0.45f },
        }));

    // =====================================================================================
    // Glide - the held tremolo arm
    // =====================================================================================
    p.push_back (build ("Held Arm", "Glide",
        "Strummed fuzz through an all-wet reverse swell, the whole chord sagging and swimming with each stroke.", {
            { P::wall, 0.65f }, { P::wash, 0.25f }, { P::glide, 0.7f }, { P::blur, 0.0f },
            { P::fuzzVoice, Full }, { P::fuzzSustain, 0.55f }, { P::fuzzTone, 0.45f }, { P::fuzzStack, 0.35f }, { P::fuzzGate, 0.1f },
            { P::glideWobble, 14.0f }, { P::glideRate, 0.55f }, { P::glideDrift, 16.0f }, { P::glideDip, 45.0f }, { P::glideReturn, 520.0f }, { P::glideSpread, 0.45f },
            { P::ampDrive, 0.4f }, { P::ampCab, Closed412 },
            { P::revOn, 1.0f }, { P::revMode, Swell }, { P::revTime, 300.0f }, { P::revFeedback, 0.0f }, { P::revSmear, 0.55f }, { P::revTone, 0.45f }, { P::revMix, 0.85f },
            { P::washDecay, 2.2f }, { P::washSize, 0.45f }, { P::washMix, 0.15f },
            { P::tapeDrive, 0.4f }, { P::tapeWow, 0.25f },
        }, { S::Glide, S::Fuzz, S::Amp, S::Reverse, S::Mod, S::Focus, S::Wash }));

    p.push_back (build ("Tilted Room", "Glide",
        "Slow, deep drift on an edge-of-breakup amp. Nothing stays quite in tune, or wants to.", {
            { P::wall, 0.0f }, { P::wash, 0.5f }, { P::glide, 0.8f }, { P::blur, 0.45f },
            { P::glideWobble, 22.0f }, { P::glideRate, 0.28f }, { P::glideDrift, 30.0f }, { P::glideDip, 25.0f }, { P::glideReturn, 800.0f }, { P::glideSpread, 0.5f },
            { P::ampDrive, 0.6f }, { P::ampCab, Open212 },
            { P::focMix, 0.3f }, { P::focHaze, 0.5f }, { P::modMix, 0.2f },
            { P::washDecay, 5.0f }, { P::washMix, 0.35f },
            { P::tapeDrive, 0.45f }, { P::tapeWow, 0.5f }, { P::tapeFlutter, 0.2f },
        }));

    p.push_back (build ("Strum Dip", "Glide",
        "No wobble, just the gesture: each stroke drops nearly a semitone and climbs back.", {
            { P::wall, 0.55f }, { P::wash, 0.45f }, { P::glide, 0.6f }, { P::blur, 0.35f },
            { P::fuzzVoice, Flat }, { P::fuzzSustain, 0.5f }, { P::fuzzTone, 0.55f },
            { P::glideWobble, 0.0f }, { P::glideDrift, 5.0f }, { P::glideDip, 95.0f }, { P::glideReturn, 260.0f }, { P::glideSpread, 0.2f },
            { P::ampCab, Closed412 },
            { P::focMix, 0.2f }, { P::modMix, 0.2f },
            { P::washDecay, 3.5f }, { P::washMix, 0.28f },
        }));

    p.push_back (build ("Warped Acetate", "Glide",
        "A record cut slightly off-centre and played on a tired motor.", {
            { P::wall, 0.0f }, { P::wash, 0.4f }, { P::glide, 0.75f }, { P::blur, 0.5f }, { P::warmth, 0.65f },
            { P::glideWobble, 25.0f }, { P::glideRate, 1.1f }, { P::glideDrift, 28.0f }, { P::glideDip, 0.0f }, { P::glideSpread, 0.25f },
            { P::ampDrive, 0.4f },
            { P::modMode, Vibrato }, { P::modRate, 5.5f }, { P::modDepth, 0.3f }, { P::modMix, 0.5f },
            { P::focMix, 0.15f },
            { P::washDecay, 2.5f }, { P::washMix, 0.25f },
            { P::tapeDrive, 0.5f }, { P::tapeWow, 0.8f }, { P::tapeFlutter, 0.6f }, { P::tapeAge, 0.7f },
        }));

    p.push_back (build ("Feather Bender", "Glide",
        "The drifting copy sits beside the dry guitar like a second player who almost agrees.", {
            { P::wall, 0.0f }, { P::wash, 0.5f }, { P::glide, 0.5f }, { P::blur, 0.4f },
            { P::glideWobble, 8.0f }, { P::glideRate, 0.4f }, { P::glideDrift, 14.0f }, { P::glideDip, 22.0f }, { P::glideSpread, 0.8f }, { P::glideMix, 0.5f },
            { P::ampDrive, 0.25f }, { P::ampCab, Chime112 },
            { P::modMix, 0.15f }, { P::focMix, 0.3f },
            { P::washDecay, 5.0f }, { P::washMix, 0.35f },
        }));

    p.push_back (build ("Drifting Apart", "Glide",
        "Two fuzzed guitars, left and right, each wandering off on its own.", {
            { P::wall, 0.6f }, { P::wash, 0.5f }, { P::glide, 0.65f }, { P::blur, 0.3f },
            { P::fuzzVoice, Full }, { P::fuzzSustain, 0.65f }, { P::fuzzStack, 0.8f },
            { P::glideWobble, 8.0f }, { P::glideDrift, 26.0f }, { P::glideDip, 30.0f }, { P::glideSpread, 1.0f },
            { P::ampCab, Closed412 },
            { P::focMix, 0.2f }, { P::modMix, 0.15f },
            { P::washDecay, 6.0f }, { P::washMix, 0.4f },
        }));

    // =====================================================================================
    // Wall - sustaining fuzz and a lot of air
    // =====================================================================================
    p.push_back (build ("Wall of Rose", "Wall",
        "Thick, stacked and very wide, with a long hall behind it. The full bloom.", {
            { P::wall, 0.8f }, { P::wash, 0.55f }, { P::glide, 0.4f }, { P::blur, 0.35f },
            { P::fuzzVoice, Full }, { P::fuzzSustain, 0.85f }, { P::fuzzTone, 0.5f }, { P::fuzzStack, 0.65f }, { P::fuzzGate, 0.2f },
            { P::ampDrive, 0.45f }, { P::ampBass, 0.55f }, { P::ampCab, Closed412 },
            { P::focMix, 0.25f }, { P::modMix, 0.15f },
            { P::washDecay, 7.0f }, { P::washSize, 0.8f }, { P::washDamp, 5500.0f }, { P::washMix, 0.45f },
        }));

    p.push_back (build ("Smeared Wall", "Wall",
        "Reverb first, fuzz second: every tail is dragged back up into the distortion. Chords melt together.", {
            { P::wall, 0.7f }, { P::wash, 0.6f }, { P::glide, 0.4f }, { P::blur, 0.3f },
            { P::fuzzVoice, Full }, { P::fuzzSustain, 0.6f }, { P::fuzzTone, 0.45f }, { P::fuzzStack, 0.4f }, { P::fuzzGate, 0.0f },
            { P::ampCab, Closed412 },
            { P::focMix, 0.15f }, { P::modMix, 0.1f },
            { P::washDecay, 3.5f }, { P::washSize, 0.6f }, { P::washDamp, 5000.0f }, { P::washLow, 180.0f }, { P::washMix, 0.6f },
        }, { S::Glide, S::Wash, S::Fuzz, S::Amp, S::Mod, S::Focus, S::Reverse }));

    p.push_back (build ("Scooped Avalanche", "Wall",
        "Mid-scooped and enormous, with a slow flanger turning underneath.", {
            { P::wall, 0.85f }, { P::wash, 0.5f }, { P::glide, 0.3f }, { P::blur, 0.3f },
            { P::fuzzVoice, Scoop }, { P::fuzzSustain, 0.9f }, { P::fuzzTone, 0.6f }, { P::fuzzStack, 0.5f }, { P::fuzzGate, 0.25f },
            { P::ampBass, 0.6f }, { P::ampTreble, 0.55f }, { P::ampCab, Closed412 },
            { P::modMode, Flanger }, { P::modRate, 0.08f }, { P::modDepth, 0.5f }, { P::modColor, 0.4f }, { P::modMix, 0.2f },
            { P::focMix, 0.1f },
            { P::washDecay, 5.0f }, { P::washMix, 0.3f },
        }));

    p.push_back (build ("Cathedral of Amps", "Wall",
        "Flat-voiced fuzz swelling out of a reverse bloom into a very long room.", {
            { P::wall, 0.7f }, { P::wash, 0.75f }, { P::glide, 0.35f }, { P::blur, 0.25f },
            { P::fuzzVoice, Flat }, { P::fuzzSustain, 0.75f },
            { P::ampCab, Closed412 },
            { P::revOn, 1.0f }, { P::revMode, Swell }, { P::revTime, 450.0f }, { P::revSmear, 0.7f }, { P::revMix, 0.5f },
            { P::focMix, 0.15f }, { P::modMix, 0.1f },
            { P::washDecay, 13.0f }, { P::washSize, 1.0f }, { P::washPre, 60.0f }, { P::washDamp, 5000.0f }, { P::washMix, 0.5f },
        }));

    p.push_back (build ("Octave Halo", "Wall",
        "Octave fuzz ringing above the note, and a hall that keeps climbing after it.", {
            { P::wall, 0.6f }, { P::wash, 0.6f }, { P::glide, 0.3f }, { P::blur, 0.3f },
            { P::fuzzType, Octave }, { P::fuzzVoice, Flat }, { P::fuzzSustain, 0.65f }, { P::fuzzTone, 0.6f }, { P::fuzzStack, 0.3f },
            { P::ampCab, Chime112 },
            { P::focMix, 0.2f }, { P::modMix, 0.1f },
            { P::washDecay, 8.0f }, { P::washShimmer, 0.3f }, { P::washDamp, 8000.0f }, { P::washMix, 0.45f },
        }));

    p.push_back (build ("Velcro Bloom", "Wall",
        "Notes spit and choke off at the fuzz, then open out behind themselves in the reverse swell.", {
            { P::wall, 0.55f }, { P::wash, 0.55f }, { P::glide, 0.3f }, { P::blur, 0.3f },
            { P::fuzzType, Velcro }, { P::fuzzVoice, Full }, { P::fuzzSustain, 0.7f }, { P::fuzzTone, 0.45f }, { P::fuzzGate, 0.05f }, { P::fuzzStack, 0.2f },
            { P::ampCab, Open212 },
            { P::revOn, 1.0f }, { P::revMode, Swell }, { P::revTime, 380.0f }, { P::revMix, 0.6f },
            { P::focMix, 0.15f }, { P::modMix, 0.1f },
            { P::washDecay, 6.0f }, { P::washMix, 0.45f },
        }));

    p.push_back (build ("Blown Speaker Hymn", "Wall",
        "Everything a little too loud for its own good: amp, tape and a low fuzz all leaning on each other.", {
            { P::wall, 0.5f }, { P::wash, 0.45f }, { P::glide, 0.35f }, { P::blur, 0.3f }, { P::warmth, 0.75f },
            { P::fuzzVoice, Flat }, { P::fuzzSustain, 0.35f }, { P::fuzzMix, 0.6f },
            { P::ampDrive, 0.95f }, { P::ampBass, 0.6f }, { P::ampTreble, 0.4f }, { P::ampCab, Closed412 },
            { P::focMix, 0.15f }, { P::modMix, 0.1f },
            { P::washDecay, 4.0f }, { P::washMix, 0.3f },
            { P::tapeDrive, 0.85f }, { P::tapeAge, 0.6f },
        }));

    p.push_back (build ("Static Flowers", "Wall",
        "Backwards echoes and shimmer are fed into an octave fuzz. Unruly on chords, gorgeous on single notes.", {
            { P::wall, 0.65f }, { P::wash, 0.55f }, { P::glide, 0.3f }, { P::blur, 0.45f },
            { P::fuzzType, Octave }, { P::fuzzVoice, Full }, { P::fuzzSustain, 0.55f }, { P::fuzzGate, 0.0f },
            { P::ampCab, Open212 },
            { P::revOn, 1.0f }, { P::revMode, TapeRev }, { P::revTime, 500.0f }, { P::revFeedback, 0.4f }, { P::revMix, 0.5f },
            { P::modMode, Phaser }, { P::modRate, 0.2f }, { P::modMix, 0.3f }, { P::focMix, 0.1f },
            { P::washDecay, 4.0f }, { P::washShimmer, 0.15f }, { P::washMix, 0.55f },
        }, { S::Glide, S::Reverse, S::Wash, S::Fuzz, S::Amp, S::Mod, S::Focus }));

    p.push_back (build ("Blanket Fuzz", "Wall",
        "Tone rolled right down. Woolly, dark and close, like hearing the band through a wall.", {
            { P::wall, 0.7f }, { P::wash, 0.5f }, { P::glide, 0.4f }, { P::blur, 0.45f }, { P::warmth, 0.75f },
            { P::fuzzVoice, Full }, { P::fuzzSustain, 0.7f }, { P::fuzzTone, 0.3f }, { P::fuzzStack, 0.5f },
            { P::ampCab, Open212 },
            { P::focMix, 0.3f }, { P::focTone, 0.4f }, { P::modMix, 0.15f },
            { P::washDecay, 5.0f }, { P::washDamp, 3500.0f }, { P::washMix, 0.4f },
        }));

    // =====================================================================================
    // Ambient - long, slow, mostly reverb
    // =====================================================================================
    p.push_back (build ("Diving Bell", "Ambient",
        "A very large space with soft edges. Play slowly and leave room.", {
            { P::wall, 0.0f }, { P::wash, 0.85f }, { P::glide, 0.3f }, { P::blur, 0.7f },
            { P::inSwell, 0.5f },
            { P::glideWobble, 4.0f }, { P::glideDrift, 8.0f }, { P::glideDip, 0.0f },
            { P::ampDrive, 0.2f },
            { P::modMode, Ensemble }, { P::modMix, 0.3f },
            { P::focTime, 500.0f }, { P::focFeedback, 0.6f }, { P::focHaze, 0.8f }, { P::focMix, 0.45f },
            { P::washDecay, 25.0f }, { P::washSize, 1.0f }, { P::washPre, 80.0f }, { P::washMod, 0.6f }, { P::washShimmer, 0.14f },
            { P::washDamp, 6000.0f }, { P::washDuck, 0.25f }, { P::washMix, 0.65f },
        }));

    p.push_back (build ("Cascade", "Ambient",
        "Octaves stacking on octaves: each pass through the hall climbs another rung.", {
            { P::wall, 0.0f }, { P::wash, 0.8f }, { P::glide, 0.25f }, { P::blur, 0.5f },
            { P::inSwell, 0.35f },
            { P::glideWobble, 3.0f }, { P::glideDrift, 6.0f }, { P::glideDip, 0.0f },
            { P::ampDrive, 0.2f }, { P::ampCab, Chime112 },
            { P::focMix, 0.25f }, { P::modMix, 0.2f },
            { P::washDecay, 12.0f }, { P::washSize, 0.9f }, { P::washShimmer, 0.7f }, { P::washShimMode, OctUp },
            { P::washDamp, 9000.0f }, { P::washMix, 0.6f },
        }));

    p.push_back (build ("Fifths in Fog", "Ambient",
        "The shimmer rises in fifths, so a single note slowly becomes an open chord.", {
            { P::wall, 0.0f }, { P::wash, 0.8f }, { P::glide, 0.3f }, { P::blur, 0.6f }, { P::warmth, 0.6f },
            { P::glideWobble, 4.0f }, { P::glideDrift, 8.0f }, { P::glideDip, 0.0f },
            { P::ampDrive, 0.2f },
            { P::focHaze, 0.7f }, { P::focMix, 0.4f }, { P::modMix, 0.2f },
            { P::washDecay, 18.0f }, { P::washShimmer, 0.35f }, { P::washShimMode, FifthUp }, { P::washDamp, 5000.0f }, { P::washMix, 0.6f },
        }));

    p.push_back (build ("Undertow", "Ambient",
        "Dark and heavy: the hall regenerates an octave down, pulling everything under.", {
            { P::wall, 0.3f }, { P::wash, 0.8f }, { P::glide, 0.35f }, { P::blur, 0.5f }, { P::warmth, 0.75f },
            { P::fuzzVoice, Full }, { P::fuzzSustain, 0.4f }, { P::fuzzTone, 0.3f }, { P::fuzzMix, 0.3f }, { P::fuzzLevel, -4.0f },
            { P::ampDrive, 0.3f },
            { P::focMix, 0.3f }, { P::modMix, 0.15f },
            { P::washDecay, 20.0f }, { P::washShimmer, 0.45f }, { P::washShimMode, OctDown }, { P::washDamp, 2800.0f },
            { P::washLow, 60.0f }, { P::washMix, 0.6f },
        }));

    p.push_back (build ("Frozen Lake", "Ambient",
        "Near-endless decay with backwards echoes skating over it. Hit Freeze to hold a chord indefinitely.", {
            { P::wall, 0.0f }, { P::wash, 0.9f }, { P::glide, 0.25f }, { P::blur, 0.5f },
            { P::glideWobble, 3.0f }, { P::glideDrift, 6.0f }, { P::glideDip, 0.0f },
            { P::ampDrive, 0.2f },
            { P::revOn, 1.0f }, { P::revMode, TapeRev }, { P::revTime, 700.0f }, { P::revFeedback, 0.45f }, { P::revMix, 0.35f },
            { P::focMix, 0.25f }, { P::modMix, 0.15f },
            { P::washDecay, 45.0f }, { P::washSize, 1.0f }, { P::washPre, 0.0f }, { P::washMod, 0.35f }, { P::washMix, 0.7f },
        }));

    p.push_back (build ("Aurora", "Ambient",
        "Shimmer spreading upward and downward at once, very wide, with reversed echoes folded through it.", {
            { P::wall, 0.0f }, { P::wash, 0.75f }, { P::glide, 0.3f }, { P::blur, 0.6f },
            { P::glideWobble, 4.0f }, { P::glideDrift, 8.0f }, { P::glideDip, 0.0f },
            { P::ampDrive, 0.2f }, { P::ampCab, Chime112 },
            { P::revOn, 1.0f }, { P::revMode, Both }, { P::revTime, 800.0f }, { P::revFeedback, 0.5f }, { P::revMix, 0.4f },
            { P::focMix, 0.3f }, { P::modMode, Ensemble }, { P::modMix, 0.25f },
            { P::washDecay, 14.0f }, { P::washShimmer, 0.4f }, { P::washShimMode, UpDown }, { P::washMix, 0.55f },
            { P::outWidth, 1.2f },
        }));

    // =====================================================================================
    // Texture - stranger things
    // =====================================================================================
    p.push_back (build ("Backwards Postcard", "Texture",
        "True reverse delay, almost fully wet: you play, and a moment later it comes back the wrong way round.", {
            { P::wall, 0.0f }, { P::wash, 0.45f }, { P::glide, 0.25f }, { P::blur, 0.3f },
            { P::glideWobble, 3.0f }, { P::glideDrift, 5.0f }, { P::glideDip, 0.0f },
            { P::ampDrive, 0.3f },
            { P::revOn, 1.0f }, { P::revMode, TapeRev }, { P::revTime, 620.0f }, { P::revFeedback, 0.3f }, { P::revSmear, 0.4f }, { P::revMix, 0.9f },
            { P::focMix, 0.15f }, { P::modMix, 0.1f },
            { P::washDecay, 4.0f }, { P::washMix, 0.3f },
            { P::tapeAge, 0.5f },
        }));

    p.push_back (build ("Rack Unit Bloom", "Texture",
        "The late-eighties rack trick on its own: rising reflections, no dry signal, the tone turned down.", {
            { P::wall, 0.0f }, { P::wash, 0.2f }, { P::glide, 0.3f }, { P::blur, 0.35f },
            { P::glideWobble, 5.0f }, { P::glideDrift, 8.0f }, { P::glideDip, 15.0f },
            { P::ampDrive, 0.5f }, { P::ampCab, Closed412 },
            { P::revOn, 1.0f }, { P::revMode, Swell }, { P::revTime, 380.0f }, { P::revFeedback, 0.0f }, { P::revSmear, 0.6f }, { P::revTone, 0.5f }, { P::revMix, 1.0f },
            { P::modMix, 0.3f }, { P::focMix, 0.0f },
            { P::washDecay, 2.0f }, { P::washMix, 0.1f },
        }, { S::Glide, S::Fuzz, S::Amp, S::Reverse, S::Mod, S::Focus, S::Wash }));

    p.push_back (build ("Seasick Lullaby", "Texture",
        "Deep vibrato over wow and flutter. Queasy in the best way.", {
            { P::wall, 0.0f }, { P::wash, 0.45f }, { P::glide, 0.7f }, { P::blur, 0.55f }, { P::warmth, 0.6f },
            { P::glideWobble, 30.0f }, { P::glideRate, 0.35f }, { P::glideDrift, 15.0f }, { P::glideDip, 0.0f },
            { P::ampDrive, 0.3f },
            { P::modMode, Vibrato }, { P::modRate, 2.8f }, { P::modDepth, 0.6f }, { P::modMix, 1.0f },
            { P::focMix, 0.35f },
            { P::washDecay, 3.5f }, { P::washMix, 0.3f },
            { P::tapeWow, 0.7f }, { P::tapeFlutter, 0.5f },
        }));

    p.push_back (build ("Jet Stream", "Texture",
        "A resonant flanger sweeping slowly across a scooped fuzz.", {
            { P::wall, 0.6f }, { P::wash, 0.45f }, { P::glide, 0.25f }, { P::blur, 0.6f },
            { P::fuzzVoice, Scoop }, { P::fuzzSustain, 0.6f },
            { P::ampCab, Closed412 },
            { P::modMode, Flanger }, { P::modRate, 0.12f }, { P::modDepth, 0.8f }, { P::modColor, 0.75f }, { P::modMix, 0.5f },
            { P::focMix, 0.1f },
            { P::washDecay, 4.0f }, { P::washMix, 0.3f },
        }));

    p.push_back (build ("Moth Wings", "Texture",
        "Gated velcro fuzz, a quick phaser and a short reverse flutter behind each note.", {
            { P::wall, 0.45f }, { P::wash, 0.4f }, { P::glide, 0.3f }, { P::blur, 0.5f },
            { P::fuzzType, Velcro }, { P::fuzzVoice, Flat }, { P::fuzzSustain, 0.6f }, { P::fuzzMix, 0.7f }, { P::fuzzGate, 0.05f },
            { P::ampCab, Chime112 },
            { P::modMode, Phaser }, { P::modRate, 1.8f }, { P::modDepth, 0.4f }, { P::modMix, 0.4f },
            { P::revOn, 1.0f }, { P::revMode, Swell }, { P::revTime, 150.0f }, { P::revMix, 0.4f },
            { P::focMix, 0.1f },
            { P::washDecay, 3.0f }, { P::washMix, 0.25f },
        }));

    p.push_back (build ("Pillow Fort", "Texture",
        "Muffled on purpose: worn tape, dark reverb, everything above the mids put to bed.", {
            { P::wall, 0.0f }, { P::wash, 0.55f }, { P::glide, 0.35f }, { P::blur, 0.55f }, { P::warmth, 0.85f },
            { P::glideWobble, 5.0f }, { P::glideDrift, 10.0f }, { P::glideDip, 10.0f },
            { P::ampDrive, 0.35f }, { P::ampTreble, 0.35f },
            { P::focTone, 0.25f }, { P::focMix, 0.35f }, { P::modMix, 0.2f },
            { P::washDecay, 5.0f }, { P::washDamp, 2200.0f }, { P::washMix, 0.4f },
            { P::tapeDrive, 0.5f }, { P::tapeAge, 0.9f }, { P::tapeWow, 0.35f },
            { P::outTilt, -0.25f },
        }));

    // =====================================================================================
    // Utility
    // =====================================================================================
    p.push_back (build ("Clean Slate", "Utility",
        "Amp and a touch of tape, all macros down. Bring them up one at a time to hear what each adds.", {
            { P::wall, 0.0f }, { P::wash, 0.0f }, { P::glide, 0.0f }, { P::blur, 0.0f },
            { P::ampDrive, 0.25f }, { P::tapeDrive, 0.2f }, { P::tapeWow, 0.1f }, { P::tapeFlutter, 0.05f }, { P::tapeAge, 0.2f },
        }));

    p.push_back (build ("DI Amp Only", "Utility",
        "Just the clean amp and cabinet, for making a DI guitar sound like it was in a room.", {
            { P::wall, 0.0f }, { P::wash, 0.0f }, { P::glide, 0.0f }, { P::blur, 0.0f },
            { P::fuzzOn, 0.0f }, { P::glideOn, 0.0f }, { P::modOn, 0.0f }, { P::focOn, 0.0f }, { P::washOn, 0.0f }, { P::tapeOn, 0.0f },
            { P::ampDrive, 0.3f },
        }));

    p.push_back (build ("Just the Fuzz", "Utility",
        "Fuzz into the amp, nothing else. For auditioning types and voicings.", {
            { P::wall, 0.5f }, { P::wash, 0.0f }, { P::glide, 0.0f }, { P::blur, 0.0f },
            { P::glideOn, 0.0f }, { P::modOn, 0.0f }, { P::focOn, 0.0f }, { P::washOn, 0.0f }, { P::tapeOn, 0.0f },
            { P::fuzzStack, 0.0f },
        }));

    p.push_back (build ("Wash Send", "Utility",
        "The hall alone, fully wet, for use on an effects send.", {
            { P::wall, 0.0f }, { P::wash, 0.5f }, { P::glide, 0.0f }, { P::blur, 0.0f },
            { P::fuzzOn, 0.0f }, { P::glideOn, 0.0f }, { P::modOn, 0.0f }, { P::focOn, 0.0f }, { P::ampOn, 0.0f }, { P::tapeOn, 0.0f },
            { P::washMix, 1.0f }, { P::washDecay, 6.0f },
        }));

    // Output trims so the presets sit at comparable loudness (generated by tools/preset_report.py --trims
    // from renders of the strummed test phrase; regenerate after changing presets or gain staging).
    static const std::pair<const char*, float> trims[] = {
#include "PresetTrims.inc"
    };
    for (auto& preset : p)
        for (const auto& [name, trimDb] : trims)
            if (std::string_view (preset.name) == name && std::abs (trimDb) > 0.05f)
                preset.values.emplace_back (P::outGain, clampTo (P::outGain, trimDb));

    return p;
}
} // namespace

const std::vector<PresetDef>& factoryPresets()
{
    static const std::vector<PresetDef> presets = makeFactoryPresets();
    return presets;
}

} // namespace swoon
