#include "Serialize.h"
#include "../seq/Scale.h"

namespace atm {

namespace {
const char* const kSrcKeys[Src::count] = { "none", "lfo1", "lfo2", "modenv", "ampenv", "stepmod", "velocity", "noternd", "note",
                                           "macro1", "macro2", "macro3", "macro4", "chaosx", "chaosy", "follower" };
const char* const kRoleKeys[Role::count] = { "kick", "snare", "hat", "perc", "bass", "lead", "pad", "fx" };

int findKey (const char* const* keys, int n, const std::string& k, int def)
{
    for (int i = 0; i < n; ++i) if (k == keys[i]) return i;
    return def;
}
void copyStr (char* dst, size_t n, const std::string& s) { std::snprintf (dst, n, "%s", s.c_str()); }
}

// ---------------------------------------------------------------- steps
Json stepToJson (const Step& s, int index)
{
    static const Step d;
    Json j = Json::object();
    j.set ("i", index);
    if (s.on) j.set ("on", 1);
    if (s.vel != d.vel) j.set ("v", (int) s.vel);
    if (s.pitch != d.pitch) j.set ("p", (int) s.pitch);
    if (s.prob != d.prob) j.set ("pr", (int) s.prob);
    if (s.cond != d.cond) j.set ("c", (int) s.cond);
    if (s.slide) j.set ("sl", 1);
    if (s.ratCount != d.ratCount) j.set ("rc", (int) s.ratCount);
    if (s.ratCurve != d.ratCurve) j.set ("rk", (int) s.ratCurve);
    if (s.micro != d.micro) j.set ("m", s.micro);
    if (s.len != d.len) j.set ("l", s.len);
    if (s.ratAmt != d.ratAmt) j.set ("ra", s.ratAmt);
    if (s.ratPitch != d.ratPitch) j.set ("rp", s.ratPitch);
    if (s.ratVel != d.ratVel) j.set ("rv", s.ratVel);
    if (s.ratGate != d.ratGate) j.set ("rg", s.ratGate);
    return j;
}

void stepFromJson (const Json& j, Step& s)
{
    s = Step{};
    s.on = (uint8_t) (j["on"].asInt (0) != 0);
    s.vel = (uint8_t) clamp (j["v"].asInt (s.vel), 1, 127);
    s.pitch = (int8_t) clamp (j["p"].asInt (0), -64, 64);
    s.prob = (uint8_t) clamp (j["pr"].asInt (100), 0, 100);
    s.cond = (uint8_t) clamp (j["c"].asInt (0), 0, (int) Cond::count - 1);
    s.slide = (uint8_t) (j["sl"].asInt (0) != 0);
    s.ratCount = (uint8_t) clamp (j["rc"].asInt (1), 1, 16);
    s.ratCurve = (uint8_t) clamp (j["rk"].asInt (0), 0, (int) RatCurve::count - 1);
    s.micro = clamp (j["m"].asFloat (0.f), -0.5f, 0.5f);
    s.len = clamp (j["l"].asFloat (1.f), 0.02f, 64.f);
    s.ratAmt = clamp01 (j["ra"].asFloat (0.5f));
    s.ratPitch = clamp (j["rp"].asFloat (0.f), -48.f, 48.f);
    s.ratVel = clamp (j["rv"].asFloat (0.f), -1.f, 1.f);
    s.ratGate = clamp (j["rg"].asFloat (0.8f), 0.05f, 1.f);
}

static bool stepIsDefault (const Step& s)
{
    static const Step d;
    return std::memcmp (&s, &d, sizeof (Step)) == 0
        || (! s.on && s.vel == d.vel && s.pitch == 0 && s.prob == 100 && s.cond == 0 && ! s.slide && s.ratCount == 1
            && s.ratCurve == 0 && s.micro == 0.f && s.len == 1.f && s.ratAmt == 0.5f && s.ratPitch == 0.f && s.ratVel == 0.f && s.ratGate == 0.8f);
}

static std::string destToKey (int dest)
{
    if (dest == kDestNone) return "";
    if (destIsGlobal (dest)) return std::string ("g:") + globalParamKey (destGlobalIndex (dest));
    return trackParamKey (dest);
}
static int destFromKey (const std::string& k)
{
    if (k.size() > 2 && k[0] == 'g' && k[1] == ':')
    {
        const int g = globalParamFromKey (k.c_str() + 2);
        return g < 0 ? kDestNone : kDestGlobalBase + g;
    }
    const int t = trackParamFromKey (k.c_str());
    return t < 0 ? kDestNone : t;
}

Json trackPatternToJson (const TrackPattern& tp)
{
    Json j = Json::object();
    j.set ("len", (int) tp.length);
    j.set ("dir", (int) tp.dir);
    Json sp = Json::array(); sp.push ((int) tp.spNum); sp.push ((int) tp.spDen);
    j.set ("speed", sp);
    j.set ("swing", tp.swing);
    j.set ("groove", (int) tp.groove);
    j.set ("grooveAmt", tp.grooveAmt);
    j.set ("free", (int) tp.freeRun);
    j.set ("seed", (double) tp.seed);
    Json steps = Json::array();
    for (int i = 0; i < kMaxSteps; ++i) if (! stepIsDefault (tp.steps[i])) steps.push (stepToJson (tp.steps[i], i));
    j.set ("steps", steps);
    Json lanes = Json::array();
    for (const LockLane& l : tp.lanes)
    {
        if (! l.used() || l.mask == 0) continue;
        Json lj = Json::object();
        lj.set ("dst", destToKey (l.dest));
        Json idx = Json::array(), val = Json::array();
        for (int i = 0; i < kMaxSteps; ++i) if (l.has (i)) { idx.push (i); val.push (l.value[i]); }
        lj.set ("i", idx); lj.set ("v", val);
        lanes.push (lj);
    }
    if (lanes.size() > 0) j.set ("locks", lanes);
    bool anyMod = false; for (float v : tp.stepMod) anyMod |= v != 0.f;
    if (anyMod) { Json m = Json::array(); for (float v : tp.stepMod) m.push (v); j.set ("stepmod", m); }
    Json g = Json::object();
    g.set ("type", (int) tp.gen.type); g.set ("seed", (double) tp.gen.seed); g.set ("lock", (int) tp.gen.seedLocked);
    Json gp = Json::array();
    for (int t = 0; t < Gen::count; ++t) for (int k = 0; k < kGenParams; ++k) gp.push (tp.gen.p[t][k]);
    g.set ("p", gp);
    j.set ("gen", g);
    return j;
}

void trackPatternFromJson (const Json& j, TrackPattern& tp)
{
    tp = TrackPattern{};
    tp.length = (uint8_t) clamp (j["len"].asInt (16), 1, kMaxSteps);
    tp.dir = (uint8_t) clamp (j["dir"].asInt (0), 0, (int) Dir::count - 1);
    tp.spNum = (uint16_t) clamp (j["speed"][0].asInt (1), 1, 64);
    tp.spDen = (uint16_t) clamp (j["speed"][1].asInt (4), 1, 64);
    tp.swing = j["swing"].asFloat (-1.f);
    tp.groove = (uint8_t) clamp (j["groove"].asInt (0), 0, kNumGrooves - 1);
    tp.grooveAmt = clamp01 (j["grooveAmt"].asFloat (0.f));
    tp.freeRun = (uint8_t) (j["free"].asInt (0) != 0);
    tp.seed = (uint32_t) j["seed"].asDouble (1.0);
    for (const Json& s : j["steps"].items())
    {
        const int i = s["i"].asInt (-1);
        if (i >= 0 && i < kMaxSteps) stepFromJson (s, tp.steps[i]);
    }
    int lane = 0;
    for (const Json& l : j["locks"].items())
    {
        if (lane >= kLockLanes) break;
        const int dest = destFromKey (l["dst"].asString());
        if (dest == kDestNone) continue;
        LockLane& ll = tp.lanes[lane++];
        ll.dest = (uint16_t) dest;
        const Json& idx = l["i"]; const Json& val = l["v"];
        for (size_t k = 0; k < idx.size() && k < val.size(); ++k)
        {
            const int i = idx[k].asInt (-1);
            if (i >= 0 && i < kMaxSteps) ll.set (i, clamp01 (val[k].asFloat (0.f)));
        }
    }
    const Json& sm = j["stepmod"];
    for (size_t i = 0; i < sm.size() && i < (size_t) kMaxSteps; ++i) tp.stepMod[i] = clamp (sm[i].asFloat (0.f), -1.f, 1.f);
    const Json& g = j["gen"];
    if (g.isObject())
    {
        tp.gen.type = (uint8_t) clamp (g["type"].asInt (0), 0, (int) Gen::count - 1);
        tp.gen.seed = (uint32_t) g["seed"].asDouble (1.0);
        tp.gen.seedLocked = (uint8_t) (g["lock"].asInt (0) != 0);
        const Json& gp = g["p"];
        if (gp.size() == (size_t) (Gen::count * kGenParams))
            for (int t = 0; t < Gen::count; ++t) for (int k = 0; k < kGenParams; ++k) tp.gen.p[t][k] = clamp01 (gp[(size_t) (t * kGenParams + k)].asFloat (0.f));
    }
}

// ---------------------------------------------------------------- sounds
Json trackSoundToJson (const TrackSound& ts, SampleRefIO* samples)
{
    Json j = Json::object();
    j.set ("name", ts.name);
    j.set ("engine", engineKey (ts.engine));
    j.set ("poly", (int) ts.poly);
    j.set ("mute", (int) ts.mute);
    j.set ("solo", (int) ts.solo);
    j.set ("locked", (int) ts.locked);
    j.set ("role", kRoleKeys[clamp ((int) ts.role, 0, (int) Role::count - 1)]);
    j.set ("scale", (int) ts.scaleOn);
    j.set ("midiOut", (int) ts.midiOut);
    j.set ("midiCh", (int) ts.midiCh);
    j.set ("baseNote", (int) ts.baseNote);
    j.set ("directOut", (int) ts.directOut);
    j.set ("fxa", fxKey (ts.fxType[0]));
    j.set ("fxb", fxKey (ts.fxType[1]));
    if (ts.sampleId != 0 && samples != nullptr) j.set ("sample", samples->describeSample (ts.sampleId));
    Json p = Json::object();
    for (int i = 0; i < TP::count; ++i) p.set (trackParamKey (i), ts.params[i]);
    j.set ("params", p);
    Json draw = Json::array();
    for (int l = 0; l < 2; ++l) { Json a = Json::array(); for (float v : ts.lfoDraw[l]) a.push (v); draw.push (a); }
    j.set ("lfoDraw", draw);
    return j;
}

void trackSoundFromJson (const Json& j, TrackSound& ts, SampleRefIO* samples)
{
    int engine = engineFromKey (j["engine"].asString().c_str());
    if (engine < 0) engine = Engine::Drum;
    initTrackSound (ts, engine);
    copyStr (ts.name, sizeof (ts.name), j["name"].asString());
    ts.poly = (uint8_t) (j["poly"].asInt (0) != 0);
    ts.mute = (uint8_t) (j["mute"].asInt (0) != 0);
    ts.solo = (uint8_t) (j["solo"].asInt (0) != 0);
    ts.locked = (uint8_t) (j["locked"].asInt (0) != 0);
    ts.role = (uint8_t) findKey (kRoleKeys, Role::count, j["role"].asString(), Role::Perc);
    ts.scaleOn = (uint8_t) (j["scale"].asInt (0) != 0);
    ts.midiOut = (uint8_t) (j["midiOut"].asInt (0) != 0);
    ts.midiCh = (uint8_t) clamp (j["midiCh"].asInt (1), 1, 16);
    ts.baseNote = (uint8_t) clamp (j["baseNote"].asInt (60), 0, 127);
    ts.directOut = (uint8_t) (j["directOut"].asInt (0) != 0);
    int fa = fxFromKey (j["fxa"].asString().c_str()), fb = fxFromKey (j["fxb"].asString().c_str());
    ts.fxType[0] = (uint8_t) std::max (0, fa); ts.fxType[1] = (uint8_t) std::max (0, fb);
    setFxDefaults (&ts.params[TP::FxA0], ts.fxType[0]);
    setFxDefaults (&ts.params[TP::FxB0], ts.fxType[1]);
    for (auto& kv : j["params"].members())
    {
        const int i = trackParamFromKey (kv.first.c_str());
        if (i >= 0) ts.params[i] = clamp01 (kv.second.asFloat (ts.params[i]));
    }
    const Json& draw = j["lfoDraw"];
    for (int l = 0; l < 2 && (size_t) l < draw.size(); ++l)
        for (size_t i = 0; i < draw[(size_t) l].size() && i < (size_t) kLfoDrawPts; ++i)
            ts.lfoDraw[l][i] = clamp (draw[(size_t) l][i].asFloat (0.f), -1.f, 1.f);
    ts.sampleId = (samples != nullptr && j.has ("sample")) ? samples->resolveSample (j["sample"]) : 0;
}

Json modSlotToJson (const ModSlot& m, const Kit&)
{
    Json j = Json::object();
    j.set ("src", kSrcKeys[clamp ((int) m.src, 0, (int) Src::count - 1)]);
    j.set ("st", (int) m.srcTrack);
    j.set ("dt", (int) m.dstTrack);
    j.set ("dst", m.dstTrack < 0 ? std::string (globalParamKey (m.dstParam)) : std::string (trackParamKey (m.dstParam)));
    j.set ("amt", m.amount);
    return j;
}

bool modSlotFromJson (const Json& j, ModSlot& m)
{
    m = ModSlot{};
    m.src = (uint8_t) findKey (kSrcKeys, Src::count, j["src"].asString(), Src::None);
    m.srcTrack = (uint8_t) clamp (j["st"].asInt (0), 0, kTracks - 1);
    m.dstTrack = (int8_t) clamp (j["dt"].asInt (0), -1, kTracks - 1);
    const int d = m.dstTrack < 0 ? globalParamFromKey (j["dst"].asString().c_str()) : trackParamFromKey (j["dst"].asString().c_str());
    if (d < 0) { m = ModSlot{}; return false; }
    m.dstParam = (uint16_t) d;
    m.amount = clamp (j["amt"].asFloat (0.f), -1.f, 1.f);
    return m.src != Src::None;
}

Json tuningToJson (const Tuning& t)
{
    Json j = Json::object();
    j.set ("root", (int) t.root);
    j.set ("scale", t.scale == 255 ? std::string ("custom") : std::string (scaleDef (t.scale).name));
    j.set ("name", t.name);
    if (t.scale == 255)
    {
        Json c = Json::array();
        for (int i = 0; i < t.customCount; ++i) c.push (t.customCents[i]);
        j.set ("cents", c);
    }
    return j;
}

void tuningFromJson (const Json& j, Tuning& t)
{
    t = Tuning{};
    t.root = (uint8_t) clamp (j["root"].asInt (0), 0, 11);
    const std::string sc = j["scale"].asString();
    if (sc == "custom" && j["cents"].size() > 0)
    {
        t.scale = 255;
        t.customCount = (int32_t) std::min<size_t> (j["cents"].size(), kMaxScaleNotes);
        for (int i = 0; i < t.customCount; ++i) t.customCents[i] = j["cents"][(size_t) i].asFloat (0.f);
    }
    else
    {
        t.scale = 0;
        for (int i = 0; i < kNumScales; ++i) if (sc == scaleDef (i).name) t.scale = (uint8_t) i;
    }
    copyStr (t.name, sizeof (t.name), j["name"].asString().empty() ? std::string (t.scale == 255 ? "Custom" : scaleDef (t.scale).name) : j["name"].asString());
}

// ---------------------------------------------------------------- project
Json projectToJson (const Project& p, SampleRefIO* samples)
{
    Json j = Json::object();
    j.set ("format", "automata");
    j.set ("version", kProjectFormatVersion);
    j.set ("name", p.name);
    j.set ("seed", (double) p.seed);
    j.set ("style", styleKey (p.style));
    j.set ("pattern", (int) p.currentPattern);
    j.set ("evolve", (int) p.evolveOn);
    j.set ("midiPatternCh", (int) p.midiPatternCh);

    Json kit = Json::object();
    Json gp = Json::object();
    for (int i = 0; i < GP::count; ++i) gp.set (globalParamKey (i), p.kit.gparams[i]);
    kit.set ("global", gp);
    Json fx = Json::array();
    for (int s = kFirstSendSlot; s < kNumFxSlots; ++s) fx.push (fxKey (p.kit.fxType[s]));
    kit.set ("busFx", fx);
    Json tracks = Json::array();
    for (int t = 0; t < kTracks; ++t) tracks.push (trackSoundToJson (p.kit.tracks[t], samples));
    kit.set ("tracks", tracks);
    Json mods = Json::array();
    for (const ModSlot& m : p.kit.mods) if (m.src != Src::None) mods.push (modSlotToJson (m, p.kit));
    kit.set ("mods", mods);
    kit.set ("tuning", tuningToJson (p.kit.tuning));
    j.set ("kit", kit);

    Json pats = Json::array();
    for (int i = 0; i < kPatterns; ++i)
    {
        const Pattern& pat = p.patterns[i];
        bool any = pat.lengthBars != 1;
        for (int t = 0; t < kTracks && ! any; ++t)
        {
            const TrackPattern& tp = pat.tracks[t];
            any = tp.countOn() > 0 || tp.length != 16 || tp.spNum != 1 || tp.spDen != 4 || tp.dir != 0 || tp.freeRun;
            for (const LockLane& l : tp.lanes) any |= l.used() && l.mask != 0;
            for (float v : tp.stepMod) any |= v != 0.f;
        }
        if (! any && i != p.currentPattern) continue;
        Json pj = Json::object();
        pj.set ("i", i); pj.set ("name", pat.name); pj.set ("bars", (int) pat.lengthBars);
        Json tr = Json::array();
        for (int t = 0; t < kTracks; ++t) tr.push (trackPatternToJson (pat.tracks[t]));
        pj.set ("tracks", tr);
        pats.push (pj);
    }
    j.set ("patterns", pats);

    Json song = Json::object();
    song.set ("mode", (int) p.song.songMode);
    Json chain = Json::array();
    for (int i = 0; i < p.song.length; ++i) { Json e = Json::array(); e.push ((int) p.song.chain[i].pattern); e.push ((int) p.song.chain[i].repeats); chain.push (e); }
    song.set ("chain", chain);
    j.set ("song", song);
    return j;
}

bool projectFromJson (const Json& j, Project& p, SampleRefIO* samples)
{
    if (! j.isObject() || j["format"].asString() != "automata") return false;
    initProject (p);
    copyStr (p.name, sizeof (p.name), j["name"].asString());
    p.seed = (uint32_t) j["seed"].asDouble (1.0);
    p.style = Style::BrokenBeat;
    for (int i = 0; i < Style::count; ++i) if (j["style"].asString() == styleKey (i)) p.style = (uint8_t) i;
    p.currentPattern = (uint8_t) clamp (j["pattern"].asInt (0), 0, kPatterns - 1);
    p.evolveOn = (uint8_t) (j["evolve"].asInt (0) != 0);
    p.midiPatternCh = (uint8_t) clamp (j["midiPatternCh"].asInt (16), 0, 16);

    const Json& kit = j["kit"];
    const Json& fx = kit["busFx"];
    for (int s = kFirstSendSlot; s < kNumFxSlots; ++s)
    {
        const int type = std::max (0, fxFromKey (fx[(size_t) (s - kFirstSendSlot)].asString().c_str()));
        setFxType (p.kit, s, type);
    }
    for (auto& kv : kit["global"].members())
    {
        const int i = globalParamFromKey (kv.first.c_str());
        if (i >= 0) p.kit.gparams[i] = clamp01 (kv.second.asFloat (p.kit.gparams[i]));
    }
    const Json& tracks = kit["tracks"];
    for (int t = 0; t < kTracks && (size_t) t < tracks.size(); ++t)
    {
        trackSoundFromJson (tracks[(size_t) t], p.kit.tracks[t], samples);
        p.kit.fxType[t * kInsertsPerTrack] = p.kit.tracks[t].fxType[0];
        p.kit.fxType[t * kInsertsPerTrack + 1] = p.kit.tracks[t].fxType[1];
    }
    int mi = 0;
    for (const Json& m : kit["mods"].items())
    {
        if (mi >= kModSlots) break;
        if (modSlotFromJson (m, p.kit.mods[mi])) ++mi;
    }
    if (kit["tuning"].isObject()) tuningFromJson (kit["tuning"], p.kit.tuning);

    for (const Json& pj : j["patterns"].items())
    {
        const int i = pj["i"].asInt (-1);
        if (i < 0 || i >= kPatterns) continue;
        Pattern& pat = p.patterns[i];
        if (! pj["name"].asString().empty()) copyStr (pat.name, sizeof (pat.name), pj["name"].asString());
        pat.lengthBars = (uint8_t) clamp (pj["bars"].asInt (1), 1, 16);
        const Json& tr = pj["tracks"];
        for (int t = 0; t < kTracks && (size_t) t < tr.size(); ++t) trackPatternFromJson (tr[(size_t) t], pat.tracks[t]);
    }
    const Json& song = j["song"];
    p.song = Song{};
    p.song.songMode = (uint8_t) (song["mode"].asInt (0) != 0);
    for (const Json& e : song["chain"].items())
    {
        if (p.song.length >= kChainLen) break;
        p.song.chain[p.song.length].pattern = (uint8_t) clamp (e[0].asInt (0), 0, kPatterns - 1);
        p.song.chain[p.song.length].repeats = (uint8_t) clamp (e[1].asInt (1), 1, 64);
        ++p.song.length;
    }
    return true;
}

// ---------------------------------------------------------------- voice presets
Json voicePresetToJson (const VoicePresetData& v, SampleRefIO* samples)
{
    Json j = Json::object();
    j.set ("format", "automata-voice");
    j.set ("version", kProjectFormatVersion);
    j.set ("category", v.category);
    j.set ("sound", trackSoundToJson (v.sound, samples));
    Json mods = Json::array();
    Kit dummy {};
    for (int i = 0; i < v.numMods; ++i) mods.push (modSlotToJson (v.mods[i], dummy));
    j.set ("mods", mods);
    return j;
}

bool voicePresetFromJson (const Json& j, VoicePresetData& v, SampleRefIO* samples)
{
    if (! j.isObject() || j["format"].asString() != "automata-voice") return false;
    v = VoicePresetData{};
    copyStr (v.category, sizeof (v.category), j["category"].asString());
    trackSoundFromJson (j["sound"], v.sound, samples);
    for (const Json& m : j["mods"].items())
    {
        if (v.numMods >= 8) break;
        if (modSlotFromJson (m, v.mods[v.numMods])) ++v.numMods;
    }
    return true;
}

} // namespace atm
