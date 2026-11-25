#include "Session.h"
#include "../factory/Presets.h"
#include <algorithm>

namespace atm {

namespace {
constexpr size_t kUndoByteLimit = 160u * 1024u * 1024u;
constexpr size_t kUndoCountLimit = 400;

template <class T> std::vector<uint8_t> bytesOf (const T& v)
{
    std::vector<uint8_t> b (sizeof (T));
    std::memcpy (b.data(), &v, sizeof (T));
    return b;
}
template <class T> void fromBytes (const std::vector<uint8_t>& b, T& v)
{
    if (b.size() == sizeof (T)) std::memcpy (&v, b.data(), sizeof (T));
}
}

Session::Session (AudioEngine& engine) : eng (engine)
{
    initProject (proj);
    pushWholeProject();
}

Session::~Session()
{
    // collect whatever the engine has already handed back; the engine frees the rest itself
    tick();
}

// ============================================================================ engine sync
void Session::post (const Cmd& c)
{
    if (eng.post (c)) return;
    needResync = true;
    switch (c.type)
    {
        case Cmd::SetTrackPattern: delete static_cast<TrackPattern*> (c.p); break;
        case Cmd::SetTrackSound:   delete static_cast<TrackSound*> (c.p); break;
        case Cmd::SetProject:      delete static_cast<Project*> (c.p); break;
        case Cmd::SetFx:           delete static_cast<Effect*> (c.p); break;
        case Cmd::SetTuning:       delete static_cast<Tuning*> (c.p); break;
        case Cmd::SetSong:         delete static_cast<Song*> (c.p); break;
        case Cmd::SetSample:       pool.audioRelease (static_cast<const SampleData*> (c.p)); break;
        default: break;
    }
}

void Session::pushFx (int slot, int type)
{
    auto fx = createEffect (type);
    if (fx) { fx->prepare (eng.sampleRate()); fx->reset(); }
    Cmd c; c.type = Cmd::SetFx; c.a = (int16_t) slot; c.p = fx.release();
    post (c);
}

void Session::pushSample (int track)
{
    const SampleData* d = pool.data (proj.kit.tracks[track].sampleId);
    pool.audioAcquire (d);
    Cmd c; c.type = Cmd::SetSample; c.a = (int16_t) track; c.p = const_cast<SampleData*> (d);
    post (c);
}

void Session::pushTrackSound (int track, const TrackSound* prev)
{
    const TrackSound& now = proj.kit.tracks[track];
    for (int k = 0; k < kInsertsPerTrack; ++k) proj.kit.fxType[track * kInsertsPerTrack + k] = now.fxType[k];
    Cmd c; c.type = Cmd::SetTrackSound; c.a = (int16_t) track; c.p = new TrackSound (now);
    post (c);
    for (int k = 0; k < kInsertsPerTrack; ++k)
        if (prev == nullptr || prev->fxType[k] != now.fxType[k]) pushFx (track * kInsertsPerTrack + k, now.fxType[k]);
    if (prev == nullptr || prev->sampleId != now.sampleId) pushSample (track);
    if (prev == nullptr || prev->engine != now.engine || prev->fxType[0] != now.fxType[0] || prev->fxType[1] != now.fxType[1]) ++structRev;
}

void Session::pushKitGlobals (const KitGlobals* prev)
{
    const Kit& k = proj.kit;
    for (int s = kFirstSendSlot; s < kNumFxSlots; ++s)
        if (prev == nullptr || prev->fxType[s] != k.fxType[s]) { pushFx (s, k.fxType[s]); ++structRev; }
    for (int i = 0; i < GP::count; ++i)
        if (prev == nullptr || prev->gparams[i] != k.gparams[i])
        { Cmd c; c.type = Cmd::SetGlobalParam; c.b = (int16_t) i; c.f = k.gparams[i]; post (c); }
    for (int i = 0; i < kModSlots; ++i)
        if (prev == nullptr || std::memcmp (&prev->mods[i], &k.mods[i], sizeof (ModSlot)) != 0)
        { Cmd c; c.type = Cmd::SetModSlot; c.a = (int16_t) i; c.mod = k.mods[i]; post (c); }
    if (prev == nullptr || std::memcmp (&prev->tuning, &k.tuning, sizeof (Tuning)) != 0)
    { Cmd c; c.type = Cmd::SetTuning; c.p = new Tuning (k.tuning); post (c); }
}

void Session::pushWholeProject()
{
    for (int t = 0; t < kTracks; ++t)
        for (int k = 0; k < kInsertsPerTrack; ++k) proj.kit.fxType[t * kInsertsPerTrack + k] = proj.kit.tracks[t].fxType[k];
    Cmd c; c.type = Cmd::SetProject; c.p = new Project (proj);
    post (c);
    for (int s = 0; s < kNumFxSlots; ++s) pushFx (s, getFxType (proj.kit, s));
    for (int t = 0; t < kTracks; ++t) pushSample (t);
    ++structRev; ++rev;
}

std::vector<int> Session::usedSampleIds() const
{
    std::vector<int> ids;
    for (const TrackSound& t : proj.kit.tracks) if (t.sampleId != 0) ids.push_back (t.sampleId);
    auto scan = [&ids] (const std::deque<UndoEntry>& st)
    {
        for (const UndoEntry& e : st)
            for (const Snapshot& s : e.snaps)
            {
                auto add = [&ids] (const std::vector<uint8_t>& b, Obj o)
                {
                    if (o == Obj::TrackSound && b.size() == sizeof (TrackSound))
                    { TrackSound ts; std::memcpy (&ts, b.data(), sizeof (ts)); if (ts.sampleId) ids.push_back (ts.sampleId); }
                    else if (o == Obj::Whole && b.size() == sizeof (Project))
                    {
                        const Project* p = reinterpret_cast<const Project*> (b.data());
                        for (int t = 0; t < kTracks; ++t) { int32_t id; std::memcpy (&id, &p->kit.tracks[t].sampleId, sizeof (id)); if (id) ids.push_back (id); }
                    }
                };
                add (s.before, s.obj); add (s.after, s.obj);
            }
    };
    scan (undoStack); scan (redoStack);
    return ids;
}

// ============================================================================ undo
std::vector<uint8_t> Session::capture (Obj obj, int a, int b) const
{
    switch (obj)
    {
        case Obj::TrackPattern: return bytesOf (proj.patterns[a].tracks[b]);
        case Obj::TrackSound:   return bytesOf (proj.kit.tracks[a]);
        case Obj::KitGlobals:
        {
            std::vector<uint8_t> v (sizeof (KitGlobals));
            auto* g = reinterpret_cast<KitGlobals*> (v.data());
            std::memcpy (g->gparams, proj.kit.gparams, sizeof (g->gparams));
            std::memcpy (g->fxType, proj.kit.fxType, sizeof (g->fxType));
            std::memcpy (g->mods, proj.kit.mods, sizeof (g->mods));
            std::memcpy (&g->tuning, &proj.kit.tuning, sizeof (Tuning));
            return v;
        }
        case Obj::Song: return bytesOf (proj.song);
        case Obj::PatternMeta:
        {
            std::vector<uint8_t> v (1 + sizeof (proj.patterns[a].name));
            v[0] = proj.patterns[a].lengthBars;
            std::memcpy (v.data() + 1, proj.patterns[a].name, sizeof (proj.patterns[a].name));
            return v;
        }
        case Obj::Meta:
        {
            MetaData m {};
            std::memcpy (m.name, proj.name, sizeof (m.name));
            m.seed = proj.seed; m.style = proj.style; m.midiPatternCh = proj.midiPatternCh; m.evolveOn = proj.evolveOn;
            return bytesOf (m);
        }
        case Obj::Whole: return bytesOf (proj);
    }
    return {};
}

void Session::touch (Obj obj, int a, int b)
{
    if (applyingUndo) return;
    for (const Snapshot& s : current.snaps)
        if (s.obj == Obj::Whole || (s.obj == obj && s.a == a && s.b == b)) return;
    Snapshot s; s.obj = obj; s.a = a; s.b = b; s.before = capture (obj, a, b);
    current.snaps.push_back (std::move (s));
}

void Session::beginTransaction (const char* name)
{
    lock.lock();
    if (txnDepth++ == 0) { current = UndoEntry{}; current.name = name ? name : ""; }
}

void Session::endTransaction()
{
    if (--txnDepth == 0)
    {
        UndoEntry e = std::move (current);
        current = UndoEntry{};
        std::vector<Snapshot> kept;
        for (Snapshot& s : e.snaps)
        {
            s.after = capture (s.obj, s.a, s.b);
            if (s.after != s.before) kept.push_back (std::move (s));
        }
        if (! kept.empty())
        {
            e.snaps = std::move (kept);
            pushUndo (std::move (e));
            redoStack.clear();
            ++rev;
        }
    }
    lock.unlock();
}

void Session::pushUndo (UndoEntry&& e)
{
    e.bytes = 0;
    for (const Snapshot& s : e.snaps) e.bytes += s.before.size() + s.after.size();
    undoBytes += e.bytes;
    undoStack.push_back (std::move (e));
    while (! undoStack.empty() && (undoBytes > kUndoByteLimit || undoStack.size() > kUndoCountLimit))
    {
        undoBytes -= undoStack.front().bytes;
        undoStack.pop_front();
    }
}

void Session::restore (const Snapshot& s, bool useAfter)
{
    const std::vector<uint8_t>& b = useAfter ? s.after : s.before;
    switch (s.obj)
    {
        case Obj::TrackPattern:
        {
            fromBytes (b, proj.patterns[s.a].tracks[s.b]);
            Cmd c; c.type = Cmd::SetTrackPattern; c.a = (int16_t) s.a; c.b = (int16_t) s.b; c.p = new TrackPattern (proj.patterns[s.a].tracks[s.b]);
            post (c);
            break;
        }
        case Obj::TrackSound:
        {
            const TrackSound prev = proj.kit.tracks[s.a];
            fromBytes (b, proj.kit.tracks[s.a]);
            pushTrackSound (s.a, &prev);
            break;
        }
        case Obj::KitGlobals:
        {
            if (b.size() != sizeof (KitGlobals)) break;
            std::vector<uint8_t> prevBytes = capture (Obj::KitGlobals, 0, 0);
            const auto* g = reinterpret_cast<const KitGlobals*> (b.data());
            std::memcpy (proj.kit.gparams, g->gparams, sizeof (g->gparams));
            for (int sl = kFirstSendSlot; sl < kNumFxSlots; ++sl) proj.kit.fxType[sl] = g->fxType[sl];
            std::memcpy (proj.kit.mods, g->mods, sizeof (g->mods));
            std::memcpy (&proj.kit.tuning, &g->tuning, sizeof (Tuning));
            pushKitGlobals (reinterpret_cast<const KitGlobals*> (prevBytes.data()));
            break;
        }
        case Obj::Song:
        {
            fromBytes (b, proj.song);
            Cmd c; c.type = Cmd::SetSong; c.p = new Song (proj.song); post (c);
            break;
        }
        case Obj::PatternMeta:
        {
            if (b.size() != 1 + sizeof (proj.patterns[s.a].name)) break;
            proj.patterns[s.a].lengthBars = b[0];
            std::memcpy (proj.patterns[s.a].name, b.data() + 1, sizeof (proj.patterns[s.a].name));
            Cmd c; c.type = Cmd::SetPatternMeta; c.a = (int16_t) s.a; c.b = (int16_t) proj.patterns[s.a].lengthBars; post (c);
            break;
        }
        case Obj::Meta:
        {
            MetaData m {}; fromBytes (b, m);
            std::memcpy (proj.name, m.name, sizeof (proj.name));
            proj.seed = m.seed; proj.style = m.style; proj.midiPatternCh = m.midiPatternCh; proj.evolveOn = m.evolveOn;
            Cmd c; c.type = Cmd::SetProjectMeta; c.a = (int16_t) proj.midiPatternCh; c.b = (int16_t) proj.style; c.u = proj.seed; post (c);
            Cmd e; e.type = Cmd::SetEvolve; e.a = (int16_t) proj.evolveOn; post (e);
            break;
        }
        case Obj::Whole:
            fromBytes (b, proj);
            pushWholeProject();
            if (onProjectReplaced) onProjectReplaced();
            break;
    }
}

void Session::applyEntry (const UndoEntry& e, bool useAfter)
{
    applyingUndo = true;
    if (useAfter) for (const Snapshot& s : e.snaps) restore (s, true);
    else for (auto it = e.snaps.rbegin(); it != e.snaps.rend(); ++it) restore (*it, false);
    applyingUndo = false;
    ++rev;
}

bool Session::undo()
{
    std::lock_guard<std::recursive_mutex> g (lock);
    if (undoStack.empty() || txnDepth > 0) return false;
    UndoEntry e = std::move (undoStack.back());
    undoStack.pop_back();
    undoBytes -= e.bytes;
    applyEntry (e, false);
    redoStack.push_back (std::move (e));
    return true;
}

bool Session::redo()
{
    std::lock_guard<std::recursive_mutex> g (lock);
    if (redoStack.empty() || txnDepth > 0) return false;
    UndoEntry e = std::move (redoStack.back());
    redoStack.pop_back();
    applyEntry (e, true);
    undoBytes += e.bytes;
    undoStack.push_back (std::move (e));
    return true;
}

void Session::clearUndo()
{
    std::lock_guard<std::recursive_mutex> g (lock);
    undoStack.clear(); redoStack.clear(); undoBytes = 0;
}

// ============================================================================ edits
void Session::setTrackParam (int t, int tp, float v)
{
    if (t < 0 || t >= kTracks || tp < 0 || tp >= TP::count) return;
    Transaction tx (*this, "Parameter");
    v = clamp01 (v);
    if (proj.kit.tracks[t].params[tp] == v) return;
    touch (Obj::TrackSound, t);
    proj.kit.tracks[t].params[tp] = v;
    Cmd c; c.type = Cmd::SetTrackParam; c.a = (int16_t) t; c.b = (int16_t) tp; c.f = v; post (c);
    ++rev;
}

void Session::setGlobalParam (int gp, float v)
{
    if (gp < 0 || gp >= GP::count) return;
    Transaction tx (*this, "Parameter");
    v = clamp01 (v);
    if (proj.kit.gparams[gp] == v) return;
    touch (Obj::KitGlobals);
    proj.kit.gparams[gp] = v;
    Cmd c; c.type = Cmd::SetGlobalParam; c.b = (int16_t) gp; c.f = v; post (c);
    ++rev;
}

void Session::setStep (int pat, int t, int i, const Step& s)
{
    if (pat < 0 || pat >= kPatterns || t < 0 || t >= kTracks || i < 0 || i >= kMaxSteps) return;
    Transaction tx (*this, "Edit step");
    touch (Obj::TrackPattern, pat, t);
    proj.patterns[pat].tracks[t].steps[i] = s;
    Cmd c; c.type = Cmd::SetStep; c.a = (int16_t) pat; c.b = (int16_t) t; c.c = (int16_t) i; c.step = s; post (c);
    ++rev;
}

void Session::setTrackPattern (int pat, int t, const TrackPattern& tp)
{
    if (pat < 0 || pat >= kPatterns || t < 0 || t >= kTracks) return;
    Transaction tx (*this, "Edit pattern");
    touch (Obj::TrackPattern, pat, t);
    proj.patterns[pat].tracks[t] = tp;
    Cmd c; c.type = Cmd::SetTrackPattern; c.a = (int16_t) pat; c.b = (int16_t) t; c.p = new TrackPattern (tp); post (c);
    ++rev;
}

void Session::replacePattern (int pat, const Pattern& p)
{
    if (pat < 0 || pat >= kPatterns) return;
    Transaction tx (*this, "Replace pattern");
    touch (Obj::PatternMeta, pat);
    proj.patterns[pat].lengthBars = p.lengthBars;
    std::memcpy (proj.patterns[pat].name, p.name, sizeof (p.name));
    Cmd c; c.type = Cmd::SetPatternMeta; c.a = (int16_t) pat; c.b = (int16_t) p.lengthBars; post (c);
    for (int t = 0; t < kTracks; ++t) setTrackPattern (pat, t, p.tracks[t]);
}

void Session::setPatternBars (int pat, int bars)
{
    if (pat < 0 || pat >= kPatterns) return;
    Transaction tx (*this, "Pattern length");
    touch (Obj::PatternMeta, pat);
    proj.patterns[pat].lengthBars = (uint8_t) clamp (bars, 1, 16);
    Cmd c; c.type = Cmd::SetPatternMeta; c.a = (int16_t) pat; c.b = (int16_t) proj.patterns[pat].lengthBars; post (c);
    ++rev;
}

void Session::setTrackSound (int t, const TrackSound& ts)
{
    if (t < 0 || t >= kTracks) return;
    Transaction tx (*this, "Change sound");
    touch (Obj::TrackSound, t);
    const TrackSound prev = proj.kit.tracks[t];
    proj.kit.tracks[t] = ts;
    pushTrackSound (t, &prev);
    ++rev;
}

void Session::setTrackFlags (int t, bool mute, bool solo)
{
    if (t < 0 || t >= kTracks) return;
    Transaction tx (*this, "Mute/solo");
    touch (Obj::TrackSound, t);
    proj.kit.tracks[t].mute = mute; proj.kit.tracks[t].solo = solo;
    Cmd c; c.type = Cmd::SetTrackFlags; c.a = (int16_t) t; c.b = mute; c.c = solo; post (c);
    ++rev;
}

void Session::setTrackLocked (int t, bool locked)
{
    if (t < 0 || t >= kTracks) return;
    TrackSound ts = proj.kit.tracks[t];
    ts.locked = locked;
    setTrackSound (t, ts);
}

void Session::setFxType (int slot, int type)
{
    if (slot < 0 || slot >= kNumFxSlots) return;
    type = clamp (type, 0, (int) Fx::count - 1);
    Transaction tx (*this, "Change effect");
    if (fxSlotIsInsert (slot))
    {
        const int t = slot / kInsertsPerTrack;
        touch (Obj::TrackSound, t);
        const TrackSound prev = proj.kit.tracks[t];
        atm::setFxType (proj.kit, slot, type);
        pushTrackSound (t, &prev);
    }
    else
    {
        touch (Obj::KitGlobals);
        const std::vector<uint8_t> prev = capture (Obj::KitGlobals, 0, 0);
        atm::setFxType (proj.kit, slot, type);
        pushKitGlobals (reinterpret_cast<const KitGlobals*> (prev.data()));
    }
    ++rev;
}

void Session::setModSlot (int i, const ModSlot& m)
{
    if (i < 0 || i >= kModSlots) return;
    Transaction tx (*this, "Modulation");
    touch (Obj::KitGlobals);
    proj.kit.mods[i] = m;
    Cmd c; c.type = Cmd::SetModSlot; c.a = (int16_t) i; c.mod = m; post (c);
    ++rev;
}

int Session::addMod (const ModSlot& m)
{
    std::lock_guard<std::recursive_mutex> g (lock);
    int freeSlot = -1;
    for (int i = 0; i < kModSlots; ++i)
    {
        const ModSlot& s = proj.kit.mods[i];
        if (s.src == m.src && s.dstTrack == m.dstTrack && s.dstParam == m.dstParam && (! srcIsPerTrack (m.src) || s.srcTrack == m.srcTrack))
        { setModSlot (i, m); return i; }
        if (freeSlot < 0 && s.src == Src::None) freeSlot = i;
    }
    if (freeSlot >= 0) setModSlot (freeSlot, m);
    return freeSlot;
}

void Session::removeMod (int i) { setModSlot (i, ModSlot{}); }

void Session::setTuning (const Tuning& t)
{
    Transaction tx (*this, "Tuning");
    touch (Obj::KitGlobals);
    proj.kit.tuning = t;
    Cmd c; c.type = Cmd::SetTuning; c.p = new Tuning (t); post (c);
    ++rev;
}

void Session::setSong (const Song& s)
{
    Transaction tx (*this, "Song");
    touch (Obj::Song);
    proj.song = s;
    Cmd c; c.type = Cmd::SetSong; c.p = new Song (s); post (c);
    ++rev;
}

void Session::setTrackSample (int t, int sampleId)
{
    if (t < 0 || t >= kTracks) return;
    TrackSound ts = proj.kit.tracks[t];
    ts.sampleId = sampleId;
    setTrackSound (t, ts);
}

void Session::applyVoice (int t, const VoicePresetData& v)
{
    if (t < 0 || t >= kTracks) return;
    Transaction tx (*this, "Load voice preset");
    touch (Obj::TrackSound, t);
    touch (Obj::KitGlobals);
    const TrackSound prev = proj.kit.tracks[t];
    const std::vector<uint8_t> prevGlobals = capture (Obj::KitGlobals, 0, 0);
    applyVoicePreset (proj, t, v);
    pushTrackSound (t, &prev);
    pushKitGlobals (reinterpret_cast<const KitGlobals*> (prevGlobals.data()));
    ++rev;
}

void Session::setProjectMeta (const char* name, uint32_t seed, int style, int midiPatternCh)
{
    Transaction tx (*this, "Project settings");
    touch (Obj::Meta);
    if (name) std::snprintf (proj.name, sizeof (proj.name), "%s", name);
    proj.seed = seed; proj.style = (uint8_t) clamp (style, 0, (int) Style::count - 1);
    proj.midiPatternCh = (uint8_t) clamp (midiPatternCh, 0, 16);
    Cmd c; c.type = Cmd::SetProjectMeta; c.a = (int16_t) proj.midiPatternCh; c.b = (int16_t) proj.style; c.u = proj.seed; post (c);
    ++rev;
}

void Session::replaceProject (const Project& p)
{
    {
        Transaction tx (*this, "Replace project");
        touch (Obj::Whole);
        proj = p;
        editPat = clamp ((int) proj.currentPattern, 0, kPatterns - 1);
        pushWholeProject();
    }
    if (onProjectReplaced) onProjectReplaced();
}

// ============================================================================ performance
void Session::selectPattern (int pattern, bool immediate)
{
    std::lock_guard<std::recursive_mutex> g (lock);
    pattern = clamp (pattern, 0, kPatterns - 1);
    proj.currentPattern = (uint8_t) pattern;
    Cmd c; c.type = Cmd::SelectPattern; c.a = (int16_t) pattern; c.b = immediate; post (c);
    ++rev;
}
void Session::setPlay (bool on)  { std::lock_guard<std::recursive_mutex> g (lock); Cmd c; c.type = Cmd::SetPlay; c.a = on; post (c); }
void Session::setFill (bool on)  { std::lock_guard<std::recursive_mutex> g (lock); Cmd c; c.type = Cmd::SetFill; c.a = on; post (c); }
void Session::setBreak (bool on) { std::lock_guard<std::recursive_mutex> g (lock); Cmd c; c.type = Cmd::SetBreak; c.a = on; post (c); }
void Session::setEvolve (bool on)
{
    Transaction tx (*this, "Evolve");
    touch (Obj::Meta);
    proj.evolveOn = on;
    Cmd c; c.type = Cmd::SetEvolve; c.a = on; post (c);
    ++rev;
}
void Session::setSelectedTrack (int t) { std::lock_guard<std::recursive_mutex> g (lock); Cmd c; c.type = Cmd::SetSelectedTrack; c.a = (int16_t) t; post (c); }
void Session::previewNote (int track, int pitchOffset, float velocity, bool on)
{
    std::lock_guard<std::recursive_mutex> g (lock);
    Cmd c; c.type = on ? Cmd::PreviewOn : Cmd::PreviewOff; c.a = (int16_t) track; c.b = (int16_t) pitchOffset; c.f = velocity; post (c);
}
void Session::allNotesOff() { std::lock_guard<std::recursive_mutex> g (lock); Cmd c; c.type = Cmd::AllNotesOff; post (c); }

// ============================================================================ housekeeping
void Session::tick()
{
    std::lock_guard<std::recursive_mutex> g (lock);
    Garbage gb;
    while (eng.popGarbage (gb))
    {
        switch (gb.kind)
        {
            case Garbage::KTrackPattern: delete static_cast<TrackPattern*> (gb.p); break;
            case Garbage::KTrackSound:   delete static_cast<TrackSound*> (gb.p); break;
            case Garbage::KProject:      delete static_cast<Project*> (gb.p); break;
            case Garbage::KEffect:       delete static_cast<Effect*> (gb.p); break;
            case Garbage::KTuning:       delete static_cast<Tuning*> (gb.p); break;
            case Garbage::KSong:         delete static_cast<Song*> (gb.p); break;
            case Garbage::KSample:       pool.audioRelease (static_cast<const SampleData*> (gb.p)); break;
            default: break;
        }
    }
    PatternChange ch;
    while (eng.popPatternChange (ch))
    {
        if (ch.pattern >= 0 && ch.pattern < kPatterns && ch.track >= 0 && ch.track < kTracks)
        {
            proj.patterns[ch.pattern].tracks[ch.track] = ch.data;      // evolve already happened on the audio thread
            ++rev;
        }
    }
    if (needResync && eng.pendingCommands() < 64)
    {
        needResync = false;
        pushWholeProject();
    }
}

// ============================================================================ persistence
Json Session::toJson()
{
    std::lock_guard<std::recursive_mutex> g (lock);
    return projectToJson (proj, &pool);
}

bool Session::fromJson (const Json& j)
{
    {
        std::lock_guard<std::recursive_mutex> g (lock);
        auto p = std::make_unique<Project>();
        if (! projectFromJson (j, *p, &pool)) return false;
        proj = *p;
        editPat = clamp ((int) proj.currentPattern, 0, kPatterns - 1);
        clearUndo();
        pool.clearEmbedded();
        pushWholeProject();
        pool.purge (usedSampleIds());
    }
    if (onProjectReplaced) onProjectReplaced();
    return true;
}

} // namespace atm
