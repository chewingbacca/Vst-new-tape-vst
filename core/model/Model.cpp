#include "Model.h"
#include <new>
#include <cstdio>

namespace atm {

void condName (uint8_t c, char* buf, int n)
{
    switch (c)
    {
        case Cond::Always:   std::snprintf (buf, (size_t) n, "--"); return;
        case Cond::Fill:     std::snprintf (buf, (size_t) n, "FILL"); return;
        case Cond::NotFill:  std::snprintf (buf, (size_t) n, "!FILL"); return;
        case Cond::Pre:      std::snprintf (buf, (size_t) n, "PRE"); return;
        case Cond::NotPre:   std::snprintf (buf, (size_t) n, "!PRE"); return;
        case Cond::First:    std::snprintf (buf, (size_t) n, "1ST"); return;
        case Cond::NotFirst: std::snprintf (buf, (size_t) n, "!1ST"); return;
        default: break;
    }
    if (condIsAB (c)) { int a, b; condGetAB (c, a, b); std::snprintf (buf, (size_t) n, "%d:%d", a, b); }
    else std::snprintf (buf, (size_t) n, "?");
}

const char* dirName (int d)
{
    static const char* const n[] = { "Forward", "Reverse", "PingPong", "Random", "Drunk" };
    return n[clamp (d, 0, (int) Dir::count - 1)];
}
const char* ratCurveName (int c)
{
    static const char* const n[] = { "Even", "Accel", "Decel", "Bounce" };
    return n[clamp (c, 0, (int) RatCurve::count - 1)];
}
const char* genName (int g)
{
    static const char* const n[] = { "Euclid", "Cellular", "Density", "Markov", "Mutate" };
    return n[clamp (g, 0, (int) Gen::count - 1)];
}
const char* roleName (int r)
{
    static const char* const n[] = { "Kick", "Snare", "Hat", "Perc", "Bass", "Lead", "Pad", "FX" };
    return n[clamp (r, 0, (int) Role::count - 1)];
}
const char* srcName (int s)
{
    static const char* const n[] = { "-", "LFO 1", "LFO 2", "Mod Env", "Amp Env", "Step Mod", "Velocity", "Note Rnd", "Note",
                                     "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Chaos X", "Chaos Y", "Follower" };
    return n[clamp (s, 0, (int) Src::count - 1)];
}
const char* styleName (int s)
{
    static const char* const n[] = { "Broken Beat", "Drill & Bass", "Ambient Melodic", "Clicks & Cuts", "Acid" };
    return n[clamp (s, 0, (int) Style::count - 1)];
}
const char* styleKey (int s)
{
    static const char* const n[] = { "broken", "drill", "ambient", "clicks", "acid" };
    return n[clamp (s, 0, (int) Style::count - 1)];
}

void setEngineDefaults (TrackSound& s, int engine)
{
    const ParamDesc* d = engineParamDescs (engine);
    for (int i = 0; i < kEngineParams; ++i) s.params[TP::E0 + i] = d[i].key[0] ? d[i].defNorm() : 0.f;
}

void setFxDefaults (float* params, int fxType)
{
    const ParamDesc* d = fxParamDescs (fxType);
    for (int i = 0; i < kFxParams; ++i) params[i] = d[i].key[0] ? d[i].defNorm() : 0.f;
    params[kFxParams] = fxMixDesc().defNorm();
}

void initTrackSound (TrackSound& s, int engine)
{
    s = TrackSound{};
    s.engine = (uint8_t) engine;
    const TrackParamContext ctx { engine, Fx::None, Fx::None };
    for (int i = 0; i < TP::count; ++i)
    {
        const ParamDesc& d = trackParamDesc (i, ctx);
        s.params[i] = d.key[0] ? d.defNorm() : 0.f;
    }
    setFxDefaults (&s.params[TP::FxA0], Fx::None);
    setFxDefaults (&s.params[TP::FxB0], Fx::None);
    for (int l = 0; l < 2; ++l)
        for (int i = 0; i < kLfoDrawPts; ++i)
            s.lfoDraw[l][i] = (i < kLfoDrawPts / 2) ? 1.f - (float) i / (float) (kLfoDrawPts / 2) : 0.f;
    s.params[TP::Lfo2Rate] = 0.3f;
    std::snprintf (s.name, sizeof (s.name), "%s", engineName (engine));
}

void initKit (Kit& k)
{
    // construct in place: never build these large structs as temporaries (thread stacks can be 512 KB)
    k.~Kit(); new (&k) Kit();
    for (int t = 0; t < kTracks; ++t)
    {
        initTrackSound (k.tracks[t], Engine::Drum);
        k.tracks[t].midiCh = (uint8_t) (t + 1);
    }
    for (int i = 0; i < GP::count; ++i) k.gparams[i] = globalParamDesc (i, k.fxType).key[0] ? globalParamDesc (i, k.fxType).defNorm() : 0.f;
    for (int s = kFirstSendSlot; s < kNumFxSlots; ++s) setFxDefaults (&k.gparams[fxSlotParamBase (s)], Fx::None);
}

void setFxType (Kit& k, int slot, int fxType)
{
    if (fxSlotIsInsert (slot))
    {
        TrackSound& ts = k.tracks[slot / kInsertsPerTrack];
        ts.fxType[slot % kInsertsPerTrack] = (uint8_t) fxType;
        setFxDefaults (&ts.params[fxSlotParamBase (slot)], fxType);
    }
    else
    {
        setFxDefaults (&k.gparams[fxSlotParamBase (slot)], fxType);
    }
    k.fxType[slot] = (uint8_t) fxType;
}

void initProject (Project& p)
{
    p.~Project(); new (&p) Project();      // in place: a Project is ~0.8 MB, too big for a temporary on small thread stacks
    initKit (p.kit);
    for (int i = 0; i < kPatterns; ++i)
    {
        std::snprintf (p.patterns[i].name, sizeof (p.patterns[i].name), "%c%d", 'A' + i / 8, i % 8 + 1);
        for (int t = 0; t < kTracks; ++t)
        {
            p.patterns[i].tracks[t].seed = hash32 ((uint32_t) (i * 131 + t * 7 + 1));
            p.patterns[i].tracks[t].gen.seed = hash32 ((uint32_t) (i * 977 + t * 13 + 5));
        }
    }
}

int findTrackParam (const TrackSound& s, const char* key)
{
    if ((key[0] == 'a' || key[0] == 'b') && key[1] == '.')
    {
        const int slot = key[0] == 'a' ? 0 : 1;
        const int base = slot == 0 ? (int) TP::FxA0 : (int) TP::FxB0;
        if (std::strcmp (key + 2, "mix") == 0) return base + kFxParams;
        const ParamDesc* d = fxParamDescs (s.fxType[slot]);
        for (int i = 0; i < kFxParams; ++i) if (d[i].key[0] && std::strcmp (d[i].key, key + 2) == 0) return base + i;
        return -1;
    }
    const ParamDesc* d = engineParamDescs (s.engine);
    for (int i = 0; i < kEngineParams; ++i) if (d[i].key[0] && std::strcmp (d[i].key, key) == 0) return TP::E0 + i;
    return trackParamFromKey (key);
}

bool setTrackParamText (TrackSound& s, const char* key, const char* value)
{
    const int tp = findTrackParam (s, key);
    if (tp < 0) return false;
    float n;
    if (! parseParam (trackParamDesc (tp, s.ctx()), value, n)) return false;
    s.params[tp] = n;
    return true;
}

bool setTrackParamPhys (TrackSound& s, const char* key, float physical)
{
    const int tp = findTrackParam (s, key);
    if (tp < 0) return false;
    s.params[tp] = trackParamDesc (tp, s.ctx()).toNorm (physical);
    return true;
}

static const SpeedDef kSpeeds[kNumSpeeds] = {
    { "1/2", 2, 1 }, { "1/4", 1, 1 }, { "1/4T", 2, 3 }, { "1/8.", 3, 4 }, { "1/8", 1, 2 }, { "1/8T", 1, 3 },
    { "1/16.", 3, 8 }, { "1/16", 1, 4 }, { "1/16T", 1, 6 }, { "5:4", 1, 5 }, { "7:4", 1, 7 }, { "1/32.", 3, 16 },
    { "1/32", 1, 8 }, { "1/32T", 1, 12 }, { "9:8", 2, 9 }, { "1/64", 1, 16 },
};
const SpeedDef& speedDef (int i) { return kSpeeds[clamp (i, 0, kNumSpeeds - 1)]; }
int speedIndex (uint16_t num, uint16_t den)
{
    const double v = (double) num / (double) (den ? den : 1);
    int best = 7; double bd = 1e9;
    for (int i = 0; i < kNumSpeeds; ++i)
    {
        const double d = std::fabs (std::log ((double) kSpeeds[i].num / kSpeeds[i].den) - std::log (v));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

// Timing offsets are in fractions of a step; positive = late.
static const GrooveDef kGrooves[kNumGrooves] = {
    { "None",    { 0 }, { 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1 } },
    { "Push",    { 0,-0.04f,-0.02f,-0.06f, 0,-0.04f,-0.02f,-0.06f, 0,-0.04f,-0.02f,-0.06f, 0,-0.04f,-0.02f,-0.08f },
                 { 1,0.8f,0.92f,0.84f, 1,0.8f,0.92f,0.84f, 1,0.8f,0.92f,0.84f, 1,0.8f,0.92f,0.88f } },
    { "Lazy",    { 0,0.07f,0.03f,0.10f, 0.02f,0.08f,0.03f,0.11f, 0,0.07f,0.04f,0.10f, 0.02f,0.09f,0.03f,0.12f },
                 { 1,0.75f,0.9f,0.7f, 0.96f,0.75f,0.88f,0.72f, 1,0.75f,0.9f,0.7f, 0.96f,0.76f,0.9f,0.78f } },
    { "Limp",    { 0,0.12f,-0.05f,0.16f, 0,0.10f,-0.03f,0.20f, 0,0.12f,-0.06f,0.14f, 0,0.09f,-0.04f,0.22f },
                 { 1,0.7f,0.95f,0.66f, 1,0.72f,0.9f,0.6f, 1,0.7f,0.95f,0.68f, 1,0.74f,0.9f,0.64f } },
    { "Machine", { 0,0.02f,0,0.02f, 0,0.02f,0,0.02f, 0,0.02f,0,0.02f, 0,0.02f,0,0.02f },
                 { 1,0.6f,0.8f,0.6f, 0.9f,0.6f,0.8f,0.6f, 1,0.6f,0.8f,0.6f, 0.9f,0.6f,0.8f,0.66f } },
    { "Stumble", { 0,0.18f,0.05f,-0.08f, 0.04f,0.21f,0,-0.11f, 0,0.15f,0.07f,-0.06f, 0.05f,0.24f,-0.02f,-0.14f },
                 { 1,0.82f,0.7f,0.9f, 0.95f,0.8f,0.72f,0.92f, 1,0.82f,0.7f,0.9f, 0.94f,0.78f,0.74f,0.96f } },
    { "Quint",   { 0,0.2f,-0.1f,0.1f, 0,0.2f,-0.1f,0.1f, 0,0.2f,-0.1f,0.1f, 0,0.2f,-0.1f,0.1f },
                 { 1,0.76f,0.88f,0.8f, 1,0.76f,0.88f,0.8f, 1,0.76f,0.88f,0.8f, 1,0.76f,0.88f,0.8f } },
    { "Rubber",  { 0,-0.06f,0.09f,0.02f, -0.03f,-0.08f,0.11f,0.03f, 0,-0.05f,0.08f,0.02f, -0.04f,-0.09f,0.13f,0.05f },
                 { 1,0.9f,0.7f,0.85f, 0.97f,0.9f,0.68f,0.84f, 1,0.9f,0.7f,0.85f, 0.97f,0.92f,0.66f,0.88f } },
};
const GrooveDef& grooveDef (int i) { return kGrooves[clamp (i, 0, kNumGrooves - 1)]; }

} // namespace atm
