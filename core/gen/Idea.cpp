// "New Idea": a whole kit + pattern set from (style, seed, tempo range).
//
// Each style is a rule set: a line-up of track roles with candidate voices, a
// pattern writer per role, a scale palette, send/master effects, macro
// settings and a few modulation routings. All choices come from one seeded Rng,
// so the same (style, seed) always gives the same project.
#include "Idea.h"
#include "Generators.h"
#include "../factory/Presets.h"
#include "../seq/Scale.h"
#include <string>
#include <vector>

namespace atm {

void styleTempoRange (int style, float& lo, float& hi)
{
    switch (style)
    {
        case Style::BrokenBeat:     lo = 88.f;  hi = 136.f; break;
        case Style::DrillBass:      lo = 160.f; hi = 184.f; break;
        case Style::AmbientMelodic: lo = 72.f;  hi = 104.f; break;
        case Style::ClicksCuts:     lo = 104.f; hi = 132.f; break;
        case Style::Acid:           lo = 120.f; hi = 142.f; break;
        default:                    lo = 80.f;  hi = 180.f; break;
    }
}

namespace {

// ------------------------------------------------------------------ helpers
struct Slot { int role; std::vector<const char*> voices; };

std::vector<Slot> lineup (int style)
{
    switch (style)
    {
        case Style::BrokenBeat: return {
            { Role::Kick,  { "Dry Thud", "Tape Kick", "Pillow Kick", "Sub Pressure", "FM Kick", "Rubber Kick" } },
            { Role::Snare, { "Tight Snare", "Lo-Fi Snare", "Fat Snare", "Snap Snare", "Wide Snare" } },
            { Role::Hat,   { "Closed Hat", "Dusty Hat", "Shaker", "Pedal Hat", "Glass Hat" } },
            { Role::Perc,  { "Rim Click", "Classic Clap", "Knock", "Woodblock", "Bongo Hi", "Conga Hit" } },
            { Role::Perc,  { "Struck Glass", "FM Clave", "Pipe Hit", "Cowbell Bent", "Wood Bar", "Tabla" } },
            { Role::Bass,  { "Rubber FM Bass", "Saw Bass", "Pluck Bass", "Tape Bass", "Dub FM Bass", "Square Sub" } },
            { Role::Lead,  { "Glass Bell", "DX Marimba", "Kalimba", "Toy Piano", "Nylon Pluck", "Vibes" } },
            { Role::Pad,   { "Warm Tape Pad", "Dusty Strings", "Choir Pad", "Grain Cloud", "Hollow Fifths" } } };
        case Style::DrillBass: return {
            { Role::Kick,  { "Click Kick", "Grit Kick", "Dry Thud", "Folded Kick", "Zap Kick" } },
            { Role::Snare, { "Tight Snare", "Crushed Snare", "Snap Snare", "Metal Snare", "Rim Crack" } },
            { Role::Hat,   { "Tick Hat", "Closed Hat", "Crushed Hat", "Trash Hat" } },
            { Role::Perc,  { "Break Chop", "Break Transient", "Break Chop Rev" } },
            { Role::Perc,  { "Metal Tick", "Bit Chirp", "Clock Tick", "Noise Snap FM", "Laser Perc" } },
            { Role::Bass,  { "Reese", "FM Sub", "Growl FM", "Metal Bass", "Fold Bass" } },
            { Role::Lead,  { "Struck Glass", "Music Box", "Digital Lead", "Sync Lead", "Metallic Lead" } },
            { Role::Fx,    { "Grain Glitch", "Glitch Bleep", "Static Burst", "Grain Stutter" } } };
        case Style::AmbientMelodic: return {
            { Role::Kick,  { "Pillow Kick", "Sub Pressure", "Rubber Kick", "Hollow Kick" } },
            { Role::Hat,   { "Shaker", "Dusty Hat", "Ghost Brush", "Ride Ping" } },
            { Role::Perc,  { "Wood Bar", "Tabla", "Tube Thump", "Kalimba", "Pong" } },
            { Role::Bass,  { "Dub FM Bass", "Sine Drone Bass", "Tape Bass", "Square Sub" } },
            { Role::Lead,  { "Harp Pluck", "Vibes", "Glass Bell", "Thai Bell", "Steel Pluck", "Koto", "Whistle Lead", "Warble Lead" } },
            { Role::Pad,   { "Warm Tape Pad", "Wobbly VHS Pad", "Choir Pad", "Glass Pad", "Dusty Strings" } },
            { Role::Pad,   { "Frozen Choir", "Grain Cloud", "Shimmer Grains", "Rev Grain Pad", "Glass Harmonica", "Bowed Metal" } },
            { Role::Fx,    { "Vinyl Bed", "Tape Loop", "Radio Noise" } } };
        case Style::ClicksCuts: return {
            { Role::Kick,  { "Click Kick", "Dry Thud", "Hollow Kick", "Blip" } },
            { Role::Hat,   { "Tick Hat", "Glass Hat", "Crushed Hat", "Shaker" } },
            { Role::Perc,  { "Rim Click", "Clock Tick", "FM Clave", "Finger Snap", "Pitched Click" } },
            { Role::Perc,  { "Metal Tick", "Bit Chirp", "Glitch Bleep", "Pong", "Bonk" } },
            { Role::Snare, { "Rim Crack", "Tin Snare", "Snap Snare", "Knock" } },
            { Role::Bass,  { "FM Sub", "Sine Drone Bass", "Square Sub", "Pluck FM Bass" } },
            { Role::Fx,    { "Static Burst", "Grain Glitch", "Laser Zap", "Grain Stutter" } },
            { Role::Pad,   { "Glass Pad", "Frozen Choir", "Rev Grain Pad", "Glass Harmonica" } } };
        case Style::Acid: default: return {
            { Role::Kick,  { "Tape Kick", "Dry Thud", "Sub Pressure", "Grit Kick", "Long Boom 808" } },
            { Role::Snare, { "Classic Clap", "Tight Clap", "Room Clap", "Scatter Clap" } },
            { Role::Hat,   { "Closed Hat", "Tick Hat", "Sizzle Hat", "Pedal Hat" } },
            { Role::Hat,   { "Open Hat", "Cymbal Wash", "Ride Ping" } },
            { Role::Bass,  { "Acid Squelch", "Acid Rubber", "Acid Deep" } },
            { Role::Lead,  { "Acid Scream", "Square Lead", "Chip Lead", "Sync Lead" } },
            { Role::Perc,  { "Cowbell Bent", "Rim Click", "Electro Tom", "Laser Perc", "Bongo Lo" } },
            { Role::Pad,   { "Dark Pad", "Pulse Pad", "Organ Pad", "Warm Tape Pad" } } };
    }
}

void setPhys (TrackSound& s, const char* key, float v) { setTrackParamPhys (s, key, v); }
float getPhys (const TrackSound& s, const char* key)
{
    const int tp = findTrackParam (s, key);
    return tp < 0 ? 0.f : trackParamDesc (tp, s.ctx()).toPhys (s.params[tp]);
}
void scalePhys (TrackSound& s, const char* key, float factor)
{
    const int tp = findTrackParam (s, key);
    if (tp >= 0) setPhys (s, key, getPhys (s, key) * factor);
}

void setBusFx (Kit& k, int slot, int type, std::initializer_list<std::pair<const char*, float>> params, float mix = 1.f)
{
    setFxType (k, slot, type);
    const int base = fxSlotParamBase (slot);
    const ParamDesc* d = fxParamDescs (type);
    for (auto& [key, v] : params)
        for (int i = 0; i < kFxParams; ++i)
            if (d[i].key[0] && std::strcmp (d[i].key, key) == 0) k.gparams[base + i] = d[i].toNorm (v);
    k.gparams[base + kFxParams] = mix;
}

void setInsert (TrackSound& s, int slot, int type, std::initializer_list<std::pair<const char*, float>> params, float mix)
{
    s.fxType[slot] = (uint8_t) type;
    const int base = slot == 0 ? (int) TP::FxA0 : (int) TP::FxB0;
    setFxDefaults (&s.params[base], type);
    const ParamDesc* d = fxParamDescs (type);
    for (auto& [key, v] : params)
        for (int i = 0; i < kFxParams; ++i)
            if (d[i].key[0] && std::strcmp (d[i].key, key) == 0) s.params[base + i] = d[i].toNorm (v);
    s.params[base + kFxParams] = mix;
}

// delay `time` when synced: index into the effect's 15-entry division list
float delayDivNorm (int index) { return ((float) index + 0.5f) / 15.f; }
enum { kDiv1_4d = 4, kDiv1_4 = 5, kDiv1_8d = 7, kDiv1_8 = 8, kDiv1_16d = 10, kDiv1_16 = 11 };

void addMod (Kit& k, int src, int srcTrack, int dstTrack, int dstParam, float amount)
{
    if (dstParam < 0) return;
    for (ModSlot& m : k.mods)
        if (m.src == Src::None)
        {
            m.src = (uint8_t) src; m.srcTrack = (uint8_t) clamp (srcTrack, 0, kTracks - 1);
            m.dstTrack = (int8_t) dstTrack; m.dstParam = (uint16_t) dstParam; m.amount = amount;
            return;
        }
}

void on (TrackPattern& tp, int i, int vel, float len = 0.5f)
{
    if (i < 0 || i >= kMaxSteps) return;
    Step& s = tp.steps[i];
    s.on = 1; s.vel = (uint8_t) clamp (vel, 1, 127); s.len = len;
}

void ratchet (Step& s, int count, int curve, float amt, float velRamp, float pitchRamp = 0.f)
{
    s.ratCount = (uint8_t) clamp (count, 1, 16); s.ratCurve = (uint8_t) curve; s.ratAmt = amt; s.ratVel = velRamp; s.ratPitch = pitchRamp;
}

// A melodic motif in scale degrees: small steps, the odd leap, returning home.
std::vector<int> motif (Rng& rng, int n, int range, bool bass)
{
    std::vector<int> m;
    int cur = 0;
    for (int i = 0; i < n; ++i)
    {
        m.push_back (cur);
        const float r = rng.uni();
        if (bass) cur = r < 0.45f ? 0 : (r < 0.6f ? 4 : (r < 0.72f ? 7 : (r < 0.84f ? -3 : (r < 0.92f ? 2 : 7))));
        else cur = clamp (cur + (r < 0.3f ? 1 : (r < 0.55f ? -1 : (r < 0.7f ? 2 : (r < 0.82f ? -2 : (r < 0.9f ? 4 : -4))))), -range, range);
    }
    return m;
}

// ------------------------------------------------------------------ pattern writers
struct Ctx
{
    Rng& rng;
    int style;
    int track;
    TrackSound& sound;
    Kit& kit;
};

void writeKick (TrackPattern& tp, Ctx& c)
{
    Rng& r = c.rng;
    tp.length = 16;
    switch (c.style)
    {
        case Style::Acid:
            for (int i = 0; i < 16; i += 4) on (tp, i, 120);
            if (r.chance (0.35f)) on (tp, 14, 70);
            if (r.chance (0.25f)) { on (tp, 15, 60); tp.steps[15].prob = 60; }
            break;
        case Style::AmbientMelodic:
            tp.length = 32; on (tp, 0, 110, 2.f);
            if (r.chance (0.6f)) on (tp, 11, 80); else on (tp, 14, 80);
            on (tp, 16, 105);
            if (r.chance (0.5f)) { on (tp, 26, 70); tp.steps[26].prob = 70; }
            break;
        case Style::ClicksCuts:
        {
            const float p[4] = { (3.f + (float) r.irange (0, 2)) / 16.f, (float) r.irange (0, 15) / 16.f, 0.2f, 0.3f };
            genEuclid (tp, r.next(), p, Role::Kick);
            tp.steps[0].on = 1; tp.steps[0].vel = 120;
            for (auto& s : tp.steps) s.len = 0.25f;
            break;
        }
        case Style::DrillBass:
        {
            static const int pats[3][4] = { { 0, 10, -1, -1 }, { 0, 7, 10, -1 }, { 0, 3, 10, 13 } };
            const int* pp = pats[r.irange (0, 2)];
            for (int k = 0; k < 4; ++k) if (pp[k] >= 0) on (tp, pp[k], k == 0 ? 124 : 105);
            if (r.chance (0.5f)) { on (tp, 15, 90); ratchet (tp.steps[15], 2, RatCurve::Even, 0.5f, -0.3f); }
            break;
        }
        default:    // broken beat: syncopated, never four on the floor
        {
            on (tp, 0, 122);
            static const int cand[] = { 3, 6, 7, 9, 10, 11, 14 };
            const int n = r.irange (2, 3);
            for (int k = 0; k < n; ++k) on (tp, cand[r.irange (0, 6)], r.irange (88, 118));
            if (r.chance (0.4f)) { on (tp, 15, 64); tp.steps[15].prob = 50; }
            break;
        }
    }
}

void writeSnare (TrackPattern& tp, Ctx& c)
{
    Rng& r = c.rng;
    tp.length = 16;
    switch (c.style)
    {
        case Style::Acid:
            on (tp, 4, 115); on (tp, 12, 115);
            if (r.chance (0.3f)) { on (tp, 15, 70); tp.steps[15].cond = condMakeAB (4, 4); }
            break;
        case Style::AmbientMelodic:
            tp.length = 32; on (tp, 8, 90); on (tp, 24, 95);
            break;
        case Style::ClicksCuts:
        {
            const float p[4] = { (float) r.irange (2, 4) / 16.f, (float) r.irange (1, 15) / 16.f, 0.4f, 0.4f };
            genEuclid (tp, r.next(), p, Role::Snare);
            for (int i = 0; i < 16; ++i) if (tp.steps[i].on) { tp.steps[i].prob = (uint8_t) r.irange (70, 100); tp.steps[i].len = 0.2f; }
            break;
        }
        case Style::DrillBass:
        {
            tp.length = 32;
            on (tp, 4, 118); on (tp, 12, 118); on (tp, 20, 118); on (tp, 28, 118);
            // the drill: rolls with every curve, pitch ramps, ghosts
            const int rolls = r.irange (3, 6);
            for (int k = 0; k < rolls; ++k)
            {
                const int i = r.irange (0, 31);
                if (i % 8 == 4) continue;
                on (tp, i, r.irange (70, 110));
                static const int counts[] = { 3, 4, 4, 6, 8, 5 };
                ratchet (tp.steps[i], counts[r.irange (0, 5)], r.irange (0, RatCurve::count - 1), r.range (0.3f, 0.9f), r.range (-0.7f, 0.3f),
                         r.chance (0.35f) ? (r.chance (0.5f) ? 12.f : -7.f) : 0.f);
                tp.steps[i].ratGate = r.range (0.4f, 0.9f);
                if (r.chance (0.3f)) tp.steps[i].cond = r.chance (0.5f) ? condMakeAB (2, 2) : (uint8_t) Cond::Fill;
            }
            for (int i = 1; i < 32; i += 2) if (! tp.steps[i].on && r.chance (0.18f)) { on (tp, i, r.irange (25, 50)); tp.steps[i].prob = 70; }
            break;
        }
        default:    // broken: backbeat + ghost notes + a flam
            on (tp, 4, 115); on (tp, 12, 118);
            for (int i = 0; i < 16; ++i)
                if (! tp.steps[i].on && (i % 2) == 1 && r.chance (0.35f)) { on (tp, i, r.irange (24, 48)); tp.steps[i].prob = (uint8_t) r.irange (55, 95); }
            if (r.chance (0.5f)) { ratchet (tp.steps[12], 2, RatCurve::Decel, 0.9f, 0.4f); }
            if (r.chance (0.4f)) { on (tp, 15, 80); tp.steps[15].cond = Cond::Fill; ratchet (tp.steps[15], 3, RatCurve::Accel, 0.6f, -0.4f); }
            break;
    }
}

void writeHat (TrackPattern& tp, Ctx& c, bool open)
{
    Rng& r = c.rng;
    tp.length = 16;
    if (open)
    {
        for (int i = 2; i < 16; i += 4) on (tp, i, r.irange (85, 110), 1.f);
        if (r.chance (0.3f)) tp.steps[14].prob = 60;
        return;
    }
    switch (c.style)
    {
        case Style::Acid:
            for (int i = 0; i < 16; ++i) on (tp, i, (i % 4 == 2) ? 105 : (i % 2 ? 62 : 80));
            if (r.chance (0.4f)) ratchet (tp.steps[7], 2, RatCurve::Even, 0.5f, -0.3f);
            break;
        case Style::AmbientMelodic:
            tp.groove = (uint8_t) r.irange (2, 3); tp.grooveAmt = r.range (0.4f, 0.8f);
            for (int i = 0; i < 16; i += 2) { on (tp, i, r.irange (40, 80)); tp.steps[i].prob = (uint8_t) r.irange (55, 100); }
            break;
        case Style::ClicksCuts:
        {
            tp.length = (uint8_t) (r.chance (0.5f) ? 13 : 11); tp.freeRun = 1;
            const float p[4] = { r.range (0.3f, 0.55f), r.uni(), 0.3f, 0.5f };
            genEuclid (tp, r.next(), p, Role::Hat);
            for (int i = 0; i < tp.length; ++i) if (tp.steps[i].on) { tp.steps[i].len = 0.1f; tp.steps[i].micro = r.bip() * 0.12f; }
            break;
        }
        case Style::DrillBass:
        {
            tp.spNum = 1; tp.spDen = 8; tp.length = 32;     // 1/32 hats
            const float p[4] = { 0.45f, 0.2f, 0.25f, 0.6f };
            genDensity (tp, r.next(), p, Role::Hat);
            for (int i = 0; i < 32; i += 4) on (tp, i, 100, 0.4f);
            break;
        }
        default:
            tp.length = (uint8_t) (r.chance (0.35f) ? 12 : 16);
            for (int i = 0; i < tp.length; ++i) on (tp, i, (i % 4 == 0) ? 92 : ((i % 2) ? r.irange (35, 60) : r.irange (60, 80)));
            for (int k = 0; k < 2; ++k) if (r.chance (0.55f)) ratchet (tp.steps[r.irange (0, tp.length - 1)], r.chance (0.5f) ? 2 : 3, r.irange (0, 3), 0.5f, -0.5f);
            if (tp.length == 12) tp.freeRun = 1;
            tp.groove = (uint8_t) (r.chance (0.5f) ? 1 : 5); tp.grooveAmt = r.range (0.2f, 0.6f);
            break;
    }
}

void writePerc (TrackPattern& tp, Ctx& c)
{
    Rng& r = c.rng;
    static const int odd[] = { 5, 7, 9, 11, 13, 10, 12 };
    tp.length = (uint8_t) odd[r.irange (0, 6)];
    tp.freeRun = r.chance (0.7f) ? 1 : 0;
    if (c.style == Style::ClicksCuts && r.chance (0.6f))
    {
        static const float rules[] = { 30.f, 90.f, 110.f, 150.f, 54.f, 60.f };
        const float p[4] = { rules[r.irange (0, 5)] / 255.f, r.range (0.2f, 0.5f), (float) r.irange (3, 12) / 32.f, 0.4f };
        genCellular (tp, r.next(), p, Role::Perc);
    }
    else
    {
        const float p[4] = { r.range (0.2f, 0.45f), r.uni(), 0.3f, 0.35f };
        genEuclid (tp, r.next(), p, Role::Perc);
    }
    for (int i = 0; i < tp.length; ++i)
        if (tp.steps[i].on)
        {
            tp.steps[i].len = 0.3f;
            if (r.chance (0.25f)) tp.steps[i].prob = (uint8_t) r.irange (50, 85);
            if (r.chance (0.15f)) tp.steps[i].pitch = (int8_t) (r.chance (0.5f) ? 7 : -5);
            if (c.style != Style::AmbientMelodic && r.chance (0.12f)) ratchet (tp.steps[i], r.irange (2, 4), r.irange (0, 3), 0.6f, -0.4f);
        }
    if (tp.countOn() == 0) on (tp, 0, 90, 0.3f);
}

void writeBreak (TrackPattern& tp, Ctx& c)
{
    // a sliced break: every step plays a slice, chosen by parameter locks (the classic chop)
    Rng& r = c.rng;
    tp.length = 16;
    const int sliceTp = TP::E7;           // sampler "slice"
    const int revTp = TP::E2;             // sampler "reverse"
    const ParamDesc& sd = engineParamDescs (Engine::Sampler)[7];
    int straight = 0;
    for (int i = 0; i < 16; ++i)
    {
        if (r.chance (0.12f) && i % 4 != 0) continue;
        on (tp, i, r.irange (85, 120), 1.f);
        int slice = i;                                     // straight through by default
        if (r.chance (0.45f)) slice = r.irange (0, 15);    // ... then chopped
        else ++straight;
        tp.setLock (i, sliceTp, sd.toNorm ((float) slice));
        if (r.chance (0.1f)) tp.setLock (i, revTp, 1.f);
        if (r.chance (0.15f)) ratchet (tp.steps[i], r.chance (0.5f) ? 2 : 4, r.irange (0, 3), 0.6f, -0.3f);
    }
    (void) straight;
}

void writeBass (TrackPattern& tp, Ctx& c)
{
    Rng& r = c.rng;
    tp.length = 16;
    switch (c.style)
    {
        case Style::Acid:
        {
            const auto m = motif (r, 16, 7, false);
            for (int i = 0; i < 16; ++i)
            {
                if (r.chance (0.22f)) continue;
                on (tp, i, r.chance (0.3f) ? 127 : 90, r.chance (0.25f) ? 1.f : 0.5f);
                Step& s = tp.steps[i];
                s.pitch = (int8_t) (m[(size_t) i] + (r.chance (0.18f) ? 7 : 0) - (r.chance (0.1f) ? 7 : 0));
                s.slide = r.chance (0.22f);
            }
            if (tp.countOn() < 6) for (int i = 0; i < 16; i += 2) on (tp, i, 100);
            break;
        }
        case Style::AmbientMelodic:
            tp.length = 32;
            on (tp, 0, 100, 7.5f); tp.steps[0].pitch = 0;
            on (tp, 16, 95, 7.5f); tp.steps[16].pitch = (int8_t) (r.chance (0.5f) ? -2 : 3);
            if (r.chance (0.5f)) { on (tp, 12, 70, 3.f); tp.steps[12].pitch = 4; tp.steps[12].prob = 60; }
            break;
        case Style::ClicksCuts:
            for (int i = 0; i < 16; ++i) if (r.chance (0.25f)) { on (tp, i, r.irange (80, 120), 0.4f); tp.steps[i].pitch = (int8_t) (r.chance (0.7f) ? 0 : (r.chance (0.5f) ? 4 : -3)); }
            on (tp, 0, 120, 0.6f);
            break;
        case Style::DrillBass:
            tp.length = 32;
            on (tp, 0, 120, 6.f); tp.steps[0].pitch = 0;
            on (tp, 10, 100, 4.f); tp.steps[10].pitch = (int8_t) (r.chance (0.5f) ? 0 : -2);
            on (tp, 16, 115, 5.f); tp.steps[16].pitch = (int8_t) (r.chance (0.5f) ? 3 : 5);
            on (tp, 26, 100, 4.f); tp.steps[26].pitch = (int8_t) (r.chance (0.5f) ? -1 : 0); tp.steps[26].slide = 1;
            break;
        default:
        {
            const auto m = motif (r, 8, 4, true);
            int k = 0;
            for (int i = 0; i < 16; ++i)
            {
                const bool strong = i == 0 || i == 10 || (i == 6 && r.chance (0.5f)) || (i == 3 && r.chance (0.4f)) || (i == 14 && r.chance (0.3f));
                if (! strong && ! r.chance (0.12f)) continue;
                on (tp, i, strong ? 110 : 80, r.range (0.6f, 1.6f));
                tp.steps[i].pitch = (int8_t) m[(size_t) (k++ % 8)];
                if (r.chance (0.15f)) tp.steps[i].slide = 1;
            }
            break;
        }
    }
}

void writeLead (TrackPattern& tp, Ctx& c)
{
    Rng& r = c.rng;
    static const int lens[] = { 12, 16, 20, 24, 16, 14 };
    tp.length = (uint8_t) lens[r.irange (0, 5)];
    const int n = r.irange (3, 6);
    const auto m = motif (r, n, 5, false);
    // place the motif, then repeat it with small variations
    int pos = r.irange (0, 2);
    int k = 0;
    while (pos < tp.length)
    {
        on (tp, pos, r.irange (70, 110), r.range (0.5f, 2.f));
        Step& s = tp.steps[pos];
        s.pitch = (int8_t) (m[(size_t) (k % n)] + (k >= n && r.chance (0.3f) ? (r.chance (0.5f) ? 2 : -1) : 0));
        if (k >= n && r.chance (0.25f)) s.prob = (uint8_t) r.irange (50, 80);
        if (c.style == Style::AmbientMelodic && r.chance (0.2f)) s.cond = condMakeAB (1, 2);
        ++k;
        pos += r.chance (0.6f) ? r.irange (2, 3) : r.irange (1, 5);
    }
    if (c.style == Style::DrillBass && r.chance (0.5f)) { tp.spNum = 1; tp.spDen = 8; }
    if (c.style == Style::AmbientMelodic) { tp.spNum = 1; tp.spDen = 2; tp.length = (uint8_t) clamp ((int) tp.length, 8, 16); }
}

void writePad (TrackPattern& tp, Ctx& c)
{
    Rng& r = c.rng;
    tp.length = 32;
    static const int chordRoots[4][4] = { { 0, -2, 3, -3 }, { 0, 4, 2, -1 }, { 0, 5, 3, 4 }, { 0, -3, -2, 2 } };
    const int* cr = chordRoots[r.irange (0, 3)];
    if (c.style == Style::AmbientMelodic) { tp.spNum = 1; tp.spDen = 2; }      // 32 eighths = 4 bars
    for (int k = 0; k < 2; ++k)
    {
        const int at = k * 16;
        on (tp, at, r.irange (75, 100), 15.5f);
        tp.steps[at].pitch = (int8_t) cr[k * 2 % 4];
        if (r.chance (0.5f)) { on (tp, at + 8, 70, 7.5f); tp.steps[at + 8].pitch = (int8_t) (cr[(k * 2 + 1) % 4] + 4); tp.steps[at + 8].prob = 70; }
    }
    if (c.style == Style::AmbientMelodic) tp.freeRun = 1;
}

void writeFx (TrackPattern& tp, Ctx& c)
{
    Rng& r = c.rng;
    tp.length = (uint8_t) (c.style == Style::AmbientMelodic ? 16 : 32);
    if (c.sound.engine == Engine::Sampler && (c.sound.params[TP::E3] > 0.2f))
    {
        on (tp, 0, 90, (float) tp.length);             // looping bed: one long note
        return;
    }
    const int hits = r.irange (2, 5);
    for (int k = 0; k < hits; ++k)
    {
        const int i = r.irange (0, tp.length - 1);
        on (tp, i, r.irange (60, 110), r.range (0.3f, 2.f));
        Step& s = tp.steps[i];
        s.prob = (uint8_t) r.irange (40, 90);
        s.cond = r.chance (0.4f) ? condMakeAB (r.irange (1, 4), 4) : (uint8_t) Cond::Always;
        s.pitch = (int8_t) (r.chance (0.5f) ? 0 : r.irange (-7, 12));
    }
}

void vary (TrackSound& s, Rng& r)
{
    // small seeded deviations so two ideas with the same voice still differ
    if (s.engine == Engine::Drum)
    {
        setPhys (s, "tune", clamp (getPhys (s, "tune") + r.range (-2.f, 2.f), -24.f, 24.f));
        scalePhys (s, "decay", r.range (0.8f, 1.25f));
    }
    if (s.params[TP::FltType] > 0.07f) scalePhys (s, "cutoff", r.range (0.8f, 1.25f));
}

} // namespace

float generateIdea (Project& p, const IdeaOptions& o, SampleRefIO* samples)
{
    Rng r (hashN (o.seed, (uint32_t) o.style * 7919u + 17u));
    const int style = clamp (o.style, 0, (int) Style::count - 1);
    float lo = o.tempoMin, hi = o.tempoMax;
    if (hi < lo) std::swap (lo, hi);
    const float bpm = std::round (r.range (lo, hi));

    bool keep[kTracks];
    for (int t = 0; t < kTracks; ++t) keep[t] = o.keepLocked && p.kit.tracks[t].locked;

    p.seed = o.seed; p.style = (uint8_t) style;
    std::snprintf (p.name, sizeof (p.name), "%s %05u", styleKey (style), o.seed % 100000u);
    for (char* c = p.name; *c; ++c) if (c == p.name) *c = (char) std::toupper ((unsigned char) *c);

    // ---- globals: macros, tempo, swing, tuning, buses
    Kit& k = p.kit;
    auto gset = [&k] (int gp, float phys) { k.gparams[gp] = globalParamDesc (gp, k.fxType).toNorm (phys); };
    static const float macros[Style::count][6] = {
        //  density complexity mutation chaos glitch space
        { 0.0f, 0.15f, 0.10f, 0.05f, 0.05f, 0.45f },
        { 0.1f, 0.40f, 0.15f, 0.10f, 0.18f, 0.35f },
        { -0.1f, 0.05f, 0.20f, 0.15f, 0.00f, 0.72f },
        { 0.0f, 0.20f, 0.15f, 0.20f, 0.30f, 0.40f },
        { 0.0f, 0.05f, 0.08f, 0.00f, 0.00f, 0.40f },
    };
    gset (GP::Density, clamp (macros[style][0] + r.bip() * 0.08f, -1.f, 1.f));
    gset (GP::Complexity, clamp01 (macros[style][1] + r.bip() * 0.06f));
    gset (GP::Mutation, clamp01 (macros[style][2] + r.bip() * 0.05f));
    gset (GP::Chaos, clamp01 (macros[style][3] + r.bip() * 0.05f));
    gset (GP::Glitch, clamp01 (macros[style][4] + (macros[style][4] > 0.f ? r.bip() * 0.06f : 0.f)));
    gset (GP::Space, clamp01 (macros[style][5] + r.bip() * 0.05f));
    for (int m = 0; m < 4; ++m) gset (GP::Macro1 + m, 0.f);
    gset (GP::Tempo, bpm);
    static const float swing[Style::count][2] = { { 0.54f, 0.62f }, { 0.5f, 0.54f }, { 0.52f, 0.6f }, { 0.5f, 0.56f }, { 0.5f, 0.55f } };
    gset (GP::Swing, r.range (swing[style][0], swing[style][1]));
    gset (GP::ChaosRate, r.range (0.1f, 1.5f));
    gset (GP::ChaosShape, r.range (0.3f, 0.8f));

    static const int scales[Style::count][4] = {
        { 3, 2, 3, 11 },      // broken: dorian / minor / pent minor
        { 2, 4, 8, 17 },      // drill: minor / phrygian / harmonic minor / phrygian dominant
        { 10, 5, 1, 3 },      // ambient: pent major / lydian / major / dorian
        { 2, 13, 11, 14 },    // clicks: minor / whole tone / pent minor / diminished
        { 2, 4, 12, 3 },      // acid: minor / phrygian / blues / dorian
    };
    Tuning tun;
    tun.root = (uint8_t) r.irange (0, 11);
    tun.scale = (uint8_t) scales[style][r.irange (0, 3)];
    std::snprintf (tun.name, sizeof (tun.name), "%s", scaleDef (tun.scale).name);
    k.tuning = tun;

    // buses
    for (int s = kFirstSendSlot; s < kNumFxSlots; ++s) setFxType (k, s, Fx::None);
    switch (style)
    {
        case Style::AmbientMelodic:
            setBusFx (k, kFirstSendSlot, Fx::Cloud, { { "size", r.range (1.5f, 3.f) }, { "decay", r.range (6.f, 16.f) }, { "shimmer", r.range (0.f, 0.3f) }, { "damphi", 5000.f } });
            setBusFx (k, kFirstSendSlot + 2, Fx::Delay, { { "sync", 1.f }, { "feedback", r.range (0.4f, 0.6f) }, { "tone", -0.5f }, { "moddepth", 0.25f }, { "width", 0.8f } });
            k.gparams[fxSlotParamBase (kFirstSendSlot + 2)] = delayDivNorm (r.chance (0.5f) ? kDiv1_4d : kDiv1_8d);
            setBusFx (k, kFirstSendSlot + 3, Fx::Filter, { { "cutoff", 4500.f } });
            break;
        case Style::DrillBass:
            setBusFx (k, kFirstSendSlot, Fx::Plate, { { "decay", r.range (1.f, 2.2f) }, { "damp", 0.4f } });
            setBusFx (k, kFirstSendSlot + 2, Fx::GrainDelay, { { "time", 60.f / bpm * 0.75f }, { "pitch", r.chance (0.5f) ? 12.f : 0.f }, { "feedback", 0.35f }, { "spray", 0.3f } });
            setBusFx (k, kFirstMasterSlot, Fx::Compressor, { { "thresh", -14.f }, { "ratio", 3.f }, { "attack", 0.01f }, { "release", 0.12f } });
            break;
        case Style::ClicksCuts:
            setBusFx (k, kFirstSendSlot, Fx::Plate, { { "decay", r.range (0.6f, 1.4f) }, { "size", 0.6f } });
            setBusFx (k, kFirstSendSlot + 2, Fx::Delay, { { "sync", 1.f }, { "feedback", 0.45f }, { "tone", 0.3f }, { "width", 1.f } });
            k.gparams[fxSlotParamBase (kFirstSendSlot + 2)] = delayDivNorm (r.chance (0.5f) ? kDiv1_16d : kDiv1_16);
            setBusFx (k, kFirstSendSlot + 3, Fx::Spectral, { { "blur", 0.7f } });
            break;
        case Style::Acid:
            setBusFx (k, kFirstSendSlot, Fx::Plate, { { "decay", r.range (1.2f, 2.5f) } });
            setBusFx (k, kFirstSendSlot + 2, Fx::Delay, { { "sync", 1.f }, { "feedback", r.range (0.35f, 0.55f) }, { "tone", -0.2f }, { "width", 0.9f } });
            k.gparams[fxSlotParamBase (kFirstSendSlot + 2)] = delayDivNorm (kDiv1_8d);
            setBusFx (k, kFirstMasterSlot, Fx::Compressor, { { "thresh", -12.f }, { "ratio", 2.5f }, { "attack", 0.02f }, { "release", 0.15f } });
            break;
        default:
            setBusFx (k, kFirstSendSlot, Fx::Plate, { { "decay", r.range (1.4f, 2.6f) } });
            setBusFx (k, kFirstSendSlot + 2, Fx::Delay, { { "sync", 1.f }, { "feedback", r.range (0.3f, 0.5f) }, { "tone", -0.3f }, { "moddepth", 0.15f } });
            k.gparams[fxSlotParamBase (kFirstSendSlot + 2)] = delayDivNorm (r.chance (0.5f) ? kDiv1_8d : kDiv1_16d);
            setBusFx (k, kFirstMasterSlot, Fx::Compressor, { { "thresh", -16.f }, { "ratio", 2.f }, { "attack", 0.015f }, { "release", 0.2f } });
            break;
    }
    k.gparams[GP::SendAReturn] = 0.62f;
    k.gparams[GP::SendBReturn] = 0.55f;

    // drop modulation that belongs to unlocked tracks
    for (ModSlot& m : k.mods)
    {
        if (m.src == Src::None) continue;
        const bool srcUnlocked = srcIsPerTrack (m.src) && ! keep[m.srcTrack];
        const bool dstUnlocked = m.dstTrack < 0 || ! keep[m.dstTrack];
        if (srcUnlocked || dstUnlocked) m = ModSlot{};
    }

    // ---- tracks
    const auto slots = lineup (style);
    int kickTrack = -1;
    for (int t = 0; t < kTracks; ++t)
    {
        const Slot& sl = slots[(size_t) t];
        if (keep[t]) { if (p.kit.tracks[t].role == Role::Kick) kickTrack = t; continue; }
        const char* vname = sl.voices[(size_t) r.irange (0, (int) sl.voices.size() - 1)];
        VoicePresetData v;
        const int idx = findVoicePreset (vname);
        if (idx < 0 || ! loadVoicePreset (idx, v, samples)) initTrackSound (v.sound, Engine::Drum);
        v.sound.role = (uint8_t) sl.role;
        v.sound.midiCh = (uint8_t) (t + 1);
        applyVoicePreset (p, t, v);
        TrackSound& ts = p.kit.tracks[t];
        ts.locked = 0; ts.mute = 0; ts.solo = 0;
        ts.midiCh = (uint8_t) (t + 1);
        vary (ts, r);
        if (sl.role == Role::Kick && kickTrack < 0) kickTrack = t;

        // mix: levels and sends by role
        static const float lvl[Role::count] = { 0.72f, 0.66f, 0.56f, 0.58f, 0.66f, 0.58f, 0.52f, 0.5f };
        ts.params[TP::Level] = lvl[sl.role] + r.bip() * 0.03f;
        const float panSpread = (sl.role == Role::Hat || sl.role == Role::Perc || sl.role == Role::Lead) ? r.bip() * 0.35f : 0.f;
        ts.params[TP::Pan] = 0.5f + panSpread * 0.5f;
        static const float sendA[Role::count] = { 0.05f, 0.25f, 0.12f, 0.3f, 0.02f, 0.35f, 0.5f, 0.3f };
        static const float sendB[Role::count] = { 0.0f, 0.12f, 0.1f, 0.25f, 0.0f, 0.4f, 0.25f, 0.2f };
        ts.params[TP::SendA] = commonTrackParamDesc (TP::SendA).toNorm (clamp01 (sendA[sl.role] * r.range (0.7f, 1.3f)));
        ts.params[TP::SendB] = commonTrackParamDesc (TP::SendB).toNorm (clamp01 (sendB[sl.role] * r.range (0.6f, 1.4f)));
        ts.params[TP::GlitchSens] = (sl.role == Role::Pad || sl.role == Role::Bass) ? 0.2f : (sl.role == Role::Kick ? 0.35f : 0.6f);
        if (sl.role == Role::Bass) ts.baseNote = 36;
        if (sl.role == Role::Lead) ts.baseNote = (uint8_t) (r.chance (0.5f) ? 60 : 72);
        if (sl.role == Role::Pad) ts.baseNote = 48;

        // ---- patterns: A1 main, A2 variation, A3 break-down, A4 build
        Ctx c { r, style, t, ts, k };
        TrackPattern main;
        main.seed = hashN (o.seed, (uint32_t) t, 101u);
        main.gen.seed = hashN (o.seed, (uint32_t) t, 202u);
        const bool isBreak = ts.engine == Engine::Sampler && (std::strncmp (ts.name, "Break", 5) == 0 || std::strncmp (ts.name, "Lazy Break", 10) == 0);
        switch (sl.role)
        {
            case Role::Kick:  writeKick (main, c); break;
            case Role::Snare: writeSnare (main, c); break;
            case Role::Hat:   writeHat (main, c, t > 0 && slots[(size_t) t - 1].role == Role::Hat); break;
            case Role::Perc:  if (isBreak) writeBreak (main, c); else writePerc (main, c); break;
            case Role::Bass:  writeBass (main, c); break;
            case Role::Lead:  if (style == Style::Acid && ts.engine == Engine::Synth) writeBass (main, c); else writeLead (main, c); break;
            case Role::Pad:   writePad (main, c); break;
            default:          writeFx (main, c); break;
        }
        main.gen.type = (uint8_t) (sl.role == Role::Bass || sl.role == Role::Lead ? Gen::Markov : (sl.role == Role::Perc ? Gen::Euclid : Gen::Mutate));

        TrackPattern varn = main;
        genMutate (varn, hashN (o.seed, (uint32_t) t, 303u), sl.role == Role::Kick ? 0.15f : 0.3f, 0.6f, 0.5f, 0.6f, sl.role);
        TrackPattern down = main;
        if (sl.role == Role::Kick || sl.role == Role::Snare || sl.role == Role::Hat || sl.role == Role::Perc)
            for (int i = 0; i < down.length; ++i)
                if (down.steps[i].on && (i % 8 != 0 || sl.role != Role::Kick) && r.chance (sl.role == Role::Kick ? 0.8f : 0.55f)) down.steps[i].on = 0;
        TrackPattern build = main;
        if (sl.role == Role::Snare || sl.role == Role::Hat || sl.role == Role::Perc || sl.role == Role::Kick)
        {
            const int L = build.length;
            for (int i = L * 3 / 4; i < L; ++i)
            {
                if (! build.steps[i].on && r.chance (0.5f)) on (build, i, r.irange (60, 100), 0.3f);
                if (build.steps[i].on && r.chance (0.5f)) ratchet (build.steps[i], r.irange (2, 6), RatCurve::Accel, 0.6f, -0.2f, sl.role == Role::Snare && r.chance (0.3f) ? 12.f : 0.f);
            }
        }
        p.patterns[0].tracks[t] = main;
        p.patterns[1].tracks[t] = varn;
        p.patterns[2].tracks[t] = down;
        p.patterns[3].tracks[t] = build;
        for (int pi = 4; pi < kPatterns; ++pi)
        {
            TrackPattern empty;
            empty.seed = hashN (o.seed, (uint32_t) t, (uint32_t) pi);
            empty.gen = main.gen;
            p.patterns[pi].tracks[t] = empty;
        }

        // ---- per-style sound design touches
        if (style == Style::Acid && sl.role == Role::Bass)
        {
            // cutoff locks on a few steps + macro 1 opens the filter
            for (int i = 0; i < main.length; ++i)
                if (main.steps[i].on && r.chance (0.2f))
                    for (int pi = 0; pi < 4; ++pi) p.patterns[pi].tracks[t].setLock (i, TP::FltCut, clamp01 (ts.params[TP::FltCut] + r.range (0.08f, 0.25f)));
        }
        if (style == Style::ClicksCuts && (sl.role == Role::Hat || sl.role == Role::Perc) && ts.fxType[0] == Fx::None)
        {
            setInsert (ts, 0, Fx::Stutter, { { "length", 5.f }, { "pitch", r.chance (0.5f) ? 0.f : -1.f }, { "decay", 0.2f } }, 1.f);
            for (int pi = 0; pi < 4; ++pi)
            {
                TrackPattern& tp = p.patterns[pi].tracks[t];
                for (int i = 0; i < tp.length; ++i) if (tp.steps[i].on && r.chance (0.12f)) tp.setLock (i, TP::FxA0, 1.f);
            }
        }
        if (style == Style::DrillBass && sl.role == Role::Snare && ts.fxType[0] == Fx::None)
            setInsert (ts, 0, Fx::Reverse, { { "length", 4.f } }, 1.f);
        if (style == Style::AmbientMelodic && (sl.role == Role::Lead || sl.role == Role::Pad) && ts.fxType[1] == Fx::None && r.chance (0.5f))
            setInsert (ts, 1, Fx::TapeStop, { { "wow", r.range (0.1f, 0.35f) } }, 1.f);
        if (style == Style::BrokenBeat && sl.role == Role::Lead && ts.fxType[1] == Fx::None)
            setInsert (ts, 1, Fx::Delay, { { "sync", 1.f }, { "feedback", 0.35f }, { "width", 1.f } }, 0.25f);
        // sidechain ducking from the kick on bass and pads
        if (kickTrack >= 0 && (sl.role == Role::Bass || sl.role == Role::Pad) && style != Style::AmbientMelodic && ts.fxType[1] == Fx::None)
        {
            setInsert (ts, 1, Fx::Compressor, { { "thresh", -24.f }, { "ratio", 6.f }, { "attack", 0.002f }, { "release", 0.16f } }, 1.f);
            ts.params[TP::FxB0 + 6] = fxParamDescs (Fx::Compressor)[6].toNorm ((float) (kickTrack + 1));
        }
        p.kit.fxType[t * kInsertsPerTrack] = ts.fxType[0];
        p.kit.fxType[t * kInsertsPerTrack + 1] = ts.fxType[1];

        // ---- modulation
        if (sl.role == Role::Bass) addMod (k, Src::Macro1, t, t, TP::FltCut, 0.35f);
        if (sl.role == Role::Lead) addMod (k, Src::Macro2, t, t, TP::SendB, 0.45f);
        if (sl.role == Role::Pad)  addMod (k, Src::Lfo2, t, t, TP::FltCut, 0.1f);
        if (sl.role == Role::Hat && ts.engine == Engine::Drum) addMod (k, Src::ChaosX, t, t, TP::E2, 0.12f);
        if (sl.role == Role::Snare || sl.role == Role::Perc) addMod (k, Src::Macro3, t, t, TP::DrvAmt, 0.4f);
        if (sl.role == Role::Kick) addMod (k, Src::Macro4, t, t, TP::E3, 0.3f);
    }

    // pattern lengths: two bars when a track spans more than one
    for (int pi = 0; pi < kPatterns; ++pi)
    {
        double longest = 0.0;
        for (int t = 0; t < kTracks; ++t)
        {
            const TrackPattern& tp = p.patterns[pi].tracks[t];
            if (! tp.freeRun) longest = std::max (longest, tp.stepBeats() * tp.length);
        }
        p.patterns[pi].lengthBars = (uint8_t) clamp ((int) std::ceil (longest / 4.0 - 1.0e-6), 1, 4);
        static const char* const names[4] = { "Main", "Vary", "Down", "Build" };
        if (pi < 4) std::snprintf (p.patterns[pi].name, sizeof (p.patterns[pi].name), "%s", names[pi]);
        else std::snprintf (p.patterns[pi].name, sizeof (p.patterns[pi].name), "%c%d", 'A' + pi / 8, pi % 8 + 1);
    }
    // song: main x2, vary, main, down, build
    p.song = Song{};
    const uint8_t chain[6][2] = { { 0, 2 }, { 1, 1 }, { 0, 1 }, { 2, 1 }, { 3, 1 }, { 1, 1 } };
    for (auto& e : chain) { p.song.chain[p.song.length].pattern = e[0]; p.song.chain[p.song.length].repeats = e[1]; ++p.song.length; }
    p.song.songMode = 0;
    p.currentPattern = 0;
    return bpm;
}

} // namespace atm
