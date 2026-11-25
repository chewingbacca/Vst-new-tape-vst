#include "AudioEngine.h"
#include "../gen/Generators.h"
#include <algorithm>

namespace atm {

namespace {
inline float levelGain (float norm) { const float g = norm * 1.41421356f; return g * g; }
constexpr int kCtrl = 16;                 // control-rate period in samples
constexpr int kGainRamp = 64;             // samples over which gains glide to a new target
}

AudioEngine::AudioEngine()
{
    rt = new Project();
    initProject (*rt);
    scale.build (rt->kit.tuning);
    events.resize (kMaxEvents);
    for (auto& p : pendingOffs) p.used = false;
    for (int t = 0; t < kTracks; ++t)
    {
        shared.step[t] = -1; shared.trigCount[t] = 0; shared.trackPeak[t] = 0.f; shared.glitchCount[t] = 0;
        tracks[t].numGLocks = 0;
    }
    shared.mainPeak[0] = shared.mainPeak[1] = 0.f;
    for (auto& v : shared.modValue) v = 0.f;
    for (auto& v : shared.globalModValue) v = 0.f;
    for (auto& v : shared.srcValue) v = 0.f;
    std::memcpy (gcur, rt->kit.gparams, sizeof (gcur));
    rebuildModIndex();
    frame.pattern = &rt->patterns[0];
    frame.kit = &rt->kit;
}

AudioEngine::~AudioEngine()
{
    // free anything still in flight (samples are owned by the sample pool, never by us)
    Cmd c;
    while (cmds.pop (c))
    {
        switch (c.type)
        {
            case Cmd::SetTrackPattern: delete static_cast<TrackPattern*> (c.p); break;
            case Cmd::SetTrackSound:   delete static_cast<TrackSound*> (c.p); break;
            case Cmd::SetProject:      delete static_cast<Project*> (c.p); break;
            case Cmd::SetFx:           delete static_cast<Effect*> (c.p); break;
            case Cmd::SetTuning:       delete static_cast<Tuning*> (c.p); break;
            case Cmd::SetSong:         delete static_cast<Song*> (c.p); break;
            default: break;
        }
    }
    Garbage g;
    while (garbage.pop (g))
    {
        switch (g.kind)
        {
            case Garbage::KTrackPattern: delete static_cast<TrackPattern*> (g.p); break;
            case Garbage::KTrackSound:   delete static_cast<TrackSound*> (g.p); break;
            case Garbage::KProject:      delete static_cast<Project*> (g.p); break;
            case Garbage::KEffect:       delete static_cast<Effect*> (g.p); break;
            case Garbage::KTuning:       delete static_cast<Tuning*> (g.p); break;
            case Garbage::KSong:         delete static_cast<Song*> (g.p); break;
            default: break;
        }
    }
    delete rt;
}

void AudioEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate; maxBlock = maxBlockSize;
    for (int t = 0; t < kTracks; ++t)
    {
        TrackRt& tr = tracks[t];
        for (auto& v : tr.voices) { v.prepare (sr); v.setSample (tr.sample); }
        tr.glitch.prepare (sr);
        for (int l = 0; l < 2; ++l) tr.lfo[l].reset (hashN (0xC0FFEEu, (uint32_t) t, (uint32_t) l));
        for (auto& f : tr.fx) if (f) f->prepare (sr);
        tr.gainL.snap (0.f); tr.gainR.snap (0.f); tr.sendA.snap (0.f); tr.sendB.snap (0.f);
        for (auto& m : tr.fxMix) m.snap (1.f);
        tr.lastNote = -1000.f; tr.curStep = -1; tr.stepModCur = tr.stepModTarget = 0.f;
        clearLocks (tr);
        std::memcpy (tr.cur, rt->kit.tracks[t].params, sizeof (tr.cur));
    }
    for (auto& f : busFx) if (f) f->prepare (sr);
    for (auto& m : busFxMix) m.snap (1.f);
    limiter.prepare ((float) sr);
    chaos.reset(); follower.reset();
    masterGain.snap (levelGain (rt->kit.gparams[GP::Master]));
    retA.snap (levelGain (rt->kit.gparams[GP::SendAReturn]));
    retB.snap (levelGain (rt->kit.gparams[GP::SendBReturn]));
    limDrive.snap (1.f);
    ctrlPhase = 0; wasPlaying = false; lastBarHandled = INT64_MIN;
    for (auto& p : pendingOffs) p.used = false;
    std::memcpy (gcur, rt->kit.gparams, sizeof (gcur));
}

// ============================================================================ commands
void AudioEngine::trash (uint8_t kind, void* p)
{
    if (p == nullptr) return;
    Garbage g; g.kind = kind; g.p = p;
    garbage.push (g);       // if this ever fails the object leaks rather than being freed on the audio thread
}

void AudioEngine::drainCommands()
{
    Cmd c;
    while (cmds.pop (c)) handleCommand (c);
}

void AudioEngine::rebuildModIndex()
{
    for (auto& n : modCount) n = 0;
    for (int i = 0; i < kModSlots; ++i)
    {
        const ModSlot& s = rt->kit.mods[i];
        if (! s.active()) continue;
        const int d = (s.dstTrack < 0 || s.dstTrack >= kTracks) ? kTracks : s.dstTrack;
        const int limit = d == kTracks ? (int) GP::count : (int) TP::count;
        if (s.dstParam >= limit) continue;
        modIndex[d][modCount[d]++] = (uint8_t) i;
    }
}

void AudioEngine::handleCommand (const Cmd& c)
{
    const int a = c.a, b = c.b;
    switch (c.type)
    {
        case Cmd::SetTrackParam:
            if (a >= 0 && a < kTracks && b >= 0 && b < TP::count) rt->kit.tracks[a].params[b] = clamp01 (c.f);
            break;
        case Cmd::SetGlobalParam:
            if (b >= 0 && b < GP::count) rt->kit.gparams[b] = clamp01 (c.f);
            break;
        case Cmd::SetStep:
            if (a >= 0 && a < kPatterns && b >= 0 && b < kTracks && c.c >= 0 && c.c < kMaxSteps) rt->patterns[a].tracks[b].steps[c.c] = c.step;
            break;
        case Cmd::SetTrackPattern:
            if (a >= 0 && a < kPatterns && b >= 0 && b < kTracks && c.p) rt->patterns[a].tracks[b] = *static_cast<TrackPattern*> (c.p);
            trash (Garbage::KTrackPattern, c.p);
            break;
        case Cmd::SetPatternMeta:
            if (a >= 0 && a < kPatterns) rt->patterns[a].lengthBars = (uint8_t) clamp (b, 1, 16);
            break;
        case Cmd::SetTrackSound:
            if (a >= 0 && a < kTracks && c.p)
            {
                const TrackSound& n = *static_cast<TrackSound*> (c.p);
                const bool engineChanged = n.engine != rt->kit.tracks[a].engine;
                rt->kit.tracks[a] = n;
                if (engineChanged) for (auto& v : tracks[a].voices) v.kill();
            }
            trash (Garbage::KTrackSound, c.p);
            break;
        case Cmd::SetTrackFlags:
            if (a >= 0 && a < kTracks) { rt->kit.tracks[a].mute = (uint8_t) (b != 0); rt->kit.tracks[a].solo = (uint8_t) (c.c != 0); }
            break;
        case Cmd::SetProject:
            if (c.p)
            {
                Project* old = rt;
                rt = static_cast<Project*> (c.p);
                trash (Garbage::KProject, old);
                scale.build (rt->kit.tuning);
                rebuildModIndex();
                allNotesOff (true);
                for (auto& tr : tracks) { clearLocks (tr); tr.glitch.reset(); }
                pendingPattern = -1; lastBarHandled = INT64_MIN; frameValidFrom = -1.0e18;
                frame.pattern = &rt->patterns[clamp ((int) rt->currentPattern, 0, kPatterns - 1)];
                frame.kit = &rt->kit;
                shared.currentPattern = rt->currentPattern;
                shared.queuedPattern = -1;
            }
            break;
        case Cmd::SetSample:
            if (a >= 0 && a < kTracks)
            {
                TrackRt& tr = tracks[a];
                const SampleData* old = tr.sample;
                for (auto& v : tr.voices) v.reset();          // nothing may keep reading the old data
                tr.sample = static_cast<const SampleData*> (c.p);
                for (auto& v : tr.voices) v.setSample (tr.sample);
                trash (Garbage::KSample, const_cast<SampleData*> (old));
            }
            break;
        case Cmd::SetFx:
            if (a >= 0 && a < kNumFxSlots)
            {
                Effect* old;
                if (fxSlotIsInsert (a))
                {
                    auto& slot = tracks[a / kInsertsPerTrack].fx[a % kInsertsPerTrack];
                    old = slot.release(); slot.reset (static_cast<Effect*> (c.p));
                }
                else
                {
                    auto& slot = busFx[a - kFirstSendSlot];
                    old = slot.release(); slot.reset (static_cast<Effect*> (c.p));
                }
                trash (Garbage::KEffect, old);
            }
            else trash (Garbage::KEffect, c.p);
            break;
        case Cmd::SetModSlot:
            if (a >= 0 && a < kModSlots) { rt->kit.mods[a] = c.mod; rebuildModIndex(); }
            break;
        case Cmd::SetTuning:
            if (c.p) { rt->kit.tuning = *static_cast<Tuning*> (c.p); scale.build (rt->kit.tuning); }
            trash (Garbage::KTuning, c.p);
            break;
        case Cmd::SetSong:
            if (c.p) { rt->song = *static_cast<Song*> (c.p); lastBarHandled = INT64_MIN; }
            trash (Garbage::KSong, c.p);
            break;
        case Cmd::SelectPattern:
            if (a >= 0 && a < kPatterns)
            {
                if (b != 0 || ! running)
                {
                    rt->currentPattern = (uint8_t) a; pendingPattern = -1; lastBarHandled = INT64_MIN;
                    frame.pattern = &rt->patterns[a];
                    shared.currentPattern = a; shared.queuedPattern = -1;
                }
                else { pendingPattern = a; shared.queuedPattern = a; }
            }
            break;
        case Cmd::SetFill:  uiFill = a != 0; if (a) fillLatched = true; break;
        case Cmd::SetBreak: uiBreak = a != 0; if (a) breakLatched = true; break;
        case Cmd::SetPlay:
            if ((a != 0) != internalPlay) { internalPlay = a != 0; internalBeat = 0.0; }
            break;
        case Cmd::SetEvolve: rt->evolveOn = (uint8_t) (a != 0); break;
        case Cmd::SetSelectedTrack: selectedTrack = clamp (a, 0, kTracks - 1); break;
        case Cmd::PreviewOn:
            if (a >= 0 && a < kTracks)
            {
                computeTrackParams (a);
                const TrackSound& ts = rt->kit.tracks[a];
                startVoice (a, scale.pitchToNote (ts.baseNote, (float) b, ts.scaleOn != 0), clamp01 (c.f), 0, false,
                            hashN (0xBEEFu, (uint32_t) ++eventOrder), 1000 + b);
            }
            break;
        case Cmd::PreviewOff:
            if (a >= 0 && a < kTracks) releaseTag (a, 1000 + b);
            break;
        case Cmd::AllNotesOff: allNotesOff (false); break;
        case Cmd::SetProjectMeta:
            rt->midiPatternCh = (uint8_t) clamp (a, 0, 16); rt->seed = c.u; rt->style = (uint8_t) clamp (b, 0, (int) Style::count - 1);
            break;
        default: break;
    }
}

// ============================================================================ notes
void AudioEngine::clearLocks (TrackRt& tr)
{
    if (tr.anyLock) { for (auto& l : tr.lockOn) l = false; tr.anyLock = false; }
    tr.numGLocks = 0;
}

void AudioEngine::allNotesOff (bool hard)
{
    for (auto& tr : tracks)
        for (int i = 0; i < kVoicesPerTrack; ++i)
        {
            if (hard) tr.voices[i].kill(); else tr.voices[i].gateOff();
            tr.voiceTag[i] = -1;
        }
    for (auto& p : pendingOffs)
        if (p.used) { sendMidi (0, (uint8_t) (0x80 | (p.ch - 1)), p.note, 0); p.used = false; }
}

void AudioEngine::sendMidi (int sample, uint8_t status, uint8_t d1, uint8_t d2)
{
    if (curIO == nullptr || curIO->midiOut == nullptr || curIO->numMidiOut >= curIO->midiOutCapacity) return;
    MidiMsg& m = curIO->midiOut[curIO->numMidiOut++];
    m.sample = clamp (sample, 0, std::max (0, curIO->numSamples - 1));
    m.data[0] = status; m.data[1] = d1; m.data[2] = d2;
}

void AudioEngine::advancePendingOffs (int n)
{
    for (auto& p : pendingOffs)
    {
        if (! p.used) continue;
        if (p.samplesLeft < n) { sendMidi (p.samplesLeft, (uint8_t) (0x80 | (p.ch - 1)), p.note, 0); p.used = false; }
        else p.samplesLeft -= n;
    }
}

void AudioEngine::releaseTag (int track, int tag)
{
    TrackRt& tr = tracks[track];
    for (int i = 0; i < kVoicesPerTrack; ++i)
        if (tr.voiceTag[i] == tag) { tr.voices[i].gateOff(); tr.voiceTag[i] = -1; }
}

void AudioEngine::startVoice (int track, float note, float vel, int gateSamples, bool slide, uint32_t seed, int tag)
{
    TrackRt& tr = tracks[track];
    const TrackSound& ts = rt->kit.tracks[track];
    const bool mono = ts.poly == 0;

    VoiceStart vs;
    vs.note = note; vs.velocity = vel; vs.slide = slide; vs.seed = seed; vs.gateSamples = gateSamples;
    vs.glideFrom = tr.lastNote;

    if (slide && mono)
    {
        // legato: retarget the voice that is still holding its gate
        for (int i = 0; i < kVoicesPerTrack; ++i)
            if (tr.voices[i].sounding() || tr.voices[i].gateHeld())
            {
                tr.voices[i].slideTo (vs, tr.cur);
                tr.voiceTag[i] = tag; tr.lastNote = note;
                tr.src[Src::Velocity] = vel; tr.src[Src::Note] = (note - 60.f) / 36.f;
                return;
            }
    }

    // make room: mono cuts everything that is still audible; poly keeps at most three sounding
    if (mono)
    {
        for (auto& v : tr.voices) if (v.active() && ! v.dying()) v.kill();
    }
    else
    {
        for (;;)
        {
            int sounding = 0, oldest = -1;
            for (int i = 0; i < kVoicesPerTrack; ++i)
                if (tr.voices[i].active() && ! tr.voices[i].dying())
                {
                    ++sounding;
                    if (oldest < 0 || tr.voices[i].age < tr.voices[oldest].age) oldest = i;
                }
            if (sounding < kVoicesPerTrack - 1 || oldest < 0) break;
            tr.voices[oldest].kill();
        }
    }
    // choose: idle first, else the quietest voice that is already fading
    int pick = -1;
    for (int i = 0; i < kVoicesPerTrack; ++i) if (! tr.voices[i].active()) { pick = i; break; }
    if (pick < 0)
    {
        float best = 1.0e9f;
        for (int i = 0; i < kVoicesPerTrack; ++i)
            if (tr.voices[i].dying() && tr.voices[i].level() < best) { best = tr.voices[i].level(); pick = i; }
    }
    if (pick < 0)
    {
        pick = 0;
        for (int i = 1; i < kVoicesPerTrack; ++i) if (tr.voices[i].age < tr.voices[pick].age) pick = i;
    }

    for (int l = 0; l < 2; ++l)
    {
        const int mode = commonTrackParamDesc (l == 0 ? TP::Lfo1Mode : TP::Lfo2Mode).toIndex (tr.cur[l == 0 ? TP::Lfo1Mode : TP::Lfo2Mode]);
        if (mode != 0) tr.lfo[l].retrigger();
    }

    Voice& v = tr.voices[pick];
    v.start (vs, ts.engine, tr.cur);
    v.age = ++tr.voiceClock;
    tr.voiceTag[pick] = tag;
    tr.lastNote = note;
    tr.src[Src::Velocity] = vel;
    tr.src[Src::NoteRandom] = v.noteRandom();
    tr.src[Src::Note] = clamp ((note - 60.f) / 36.f, -1.f, 1.f);
    shared.trigCount[track].fetch_add (1, std::memory_order_relaxed);
}

void AudioEngine::handleStepStart (const RtEvent& e)
{
    TrackRt& tr = tracks[e.track];
    const TrackPattern& tp = frame.pattern->tracks[e.track];
    tr.curStep = e.index;
    shared.step[e.track].store (e.index, std::memory_order_relaxed);
    clearLocks (tr);
    for (const LockLane& lane : tp.lanes)
    {
        if (! lane.used() || ! lane.has (e.index)) continue;
        const float v = clamp01 (lane.value[e.index]);
        if (lane.dest < TP::count) { tr.lockVal[lane.dest] = v; tr.lockOn[lane.dest] = true; tr.anyLock = true; }
        else if (destIsGlobal (lane.dest) && destGlobalIndex (lane.dest) < GP::count && tr.numGLocks < kLockLanes)
        {
            tr.gLockParam[tr.numGLocks] = destGlobalIndex (lane.dest);
            tr.gLockVal[tr.numGLocks] = v;
            ++tr.numGLocks;
        }
    }
    tr.stepModTarget = clamp (tp.stepMod[e.index], -1.f, 1.f);
    if (e.glitch.type != GlitchType::None && tr.audible)
    {
        tr.glitch.trigger (e.glitch, curBpm);
        shared.glitchCount[e.track].fetch_add (1, std::memory_order_relaxed);
    }
}

void AudioEngine::handleNoteOn (const RtEvent& e)
{
    TrackRt& tr = tracks[e.track];
    if (! tr.audible) return;
    const TrackSound& ts = rt->kit.tracks[e.track];
    computeTrackParams (e.track);
    const float note = scale.pitchToNote (ts.baseNote, e.pitch, ts.scaleOn != 0);
    startVoice (e.track, note, e.vel, e.gateSamples, e.slide, e.seed, -1);

    if (ts.midiOut && curIO != nullptr && curIO->midiOut != nullptr)
    {
        const uint8_t ch = (uint8_t) clamp ((int) ts.midiCh, 1, 16);
        const uint8_t nn = (uint8_t) clamp ((int) std::lround (note), 0, 127);
        const uint8_t vv = (uint8_t) clamp ((int) std::lround (e.vel * 127.f), 1, 127);
        // a still-pending off for the same note must come first
        for (auto& p : pendingOffs)
            if (p.used && p.ch == ch && p.note == nn) { sendMidi (e.sample, (uint8_t) (0x80 | (ch - 1)), nn, 0); p.used = false; }
        sendMidi (e.sample, (uint8_t) (0x90 | (ch - 1)), nn, vv);
        const int offAt = e.sample + std::max (1, e.gateSamples);
        for (auto& p : pendingOffs)
            if (! p.used) { p.used = true; p.ch = ch; p.note = nn; p.samplesLeft = offAt; break; }
    }
}

void AudioEngine::handleMidiIn (const MidiMsg& m)
{
    const uint8_t status = m.data[0] & 0xF0;
    const int ch = (m.data[0] & 0x0F) + 1;
    const bool on = status == 0x90 && m.data[2] > 0;
    const bool off = status == 0x80 || (status == 0x90 && m.data[2] == 0);
    if (status == 0xB0 && (m.data[1] == 123 || m.data[1] == 120)) { allNotesOff (m.data[1] == 120); return; }
    if (! on && ! off) return;
    const int n = m.data[1];
    if (rt->midiPatternCh != 0 && ch == rt->midiPatternCh)
    {
        if (n >= 36 && n < 36 + kPatterns && on)
        {
            const int p = n - 36;
            if (running) { pendingPattern = p; shared.queuedPattern = p; }
            else { rt->currentPattern = (uint8_t) p; frame.pattern = &rt->patterns[p]; shared.currentPattern = p; lastBarHandled = INT64_MIN; }
        }
        else if (n == 60) { uiFill = on; if (on) fillLatched = true; }
        else if (n == 62) { uiBreak = on; if (on) breakLatched = true; }
        return;
    }
    if (ch >= 1 && ch <= kTracks)
    {
        const int t = ch - 1;
        if (on)
        {
            computeTrackParams (t);
            const TrackSound& ts = rt->kit.tracks[t];
            startVoice (t, (float) ts.baseNote + (float) (n - 60), (float) m.data[2] / 127.f, 0, false,
                        hashN (0xABCDu, (uint32_t) n, ++eventOrder), n);
        }
        else releaseTag (t, n);
    }
}

// ============================================================================ sequencing
SeqMacros AudioEngine::currentMacros() const
{
    SeqMacros m;
    const float fillAmt = fillOn ? gcur[GP::FillAmt] : 0.f;
    const float brkAmt = breakOn ? gcur[GP::BreakAmt] : 0.f;
    m.density    = clamp ((gcur[GP::Density] * 2.f - 1.f) + fillAmt * 0.6f, -1.f, 1.f);
    m.complexity = clamp01 (gcur[GP::Complexity] + fillAmt * 0.6f + brkAmt * 0.5f);
    m.mutation   = clamp01 (gcur[GP::Mutation] + brkAmt * 0.5f);
    m.chaos      = gcur[GP::Chaos];
    m.glitch     = clamp01 (gcur[GP::Glitch] + brkAmt * 0.8f);
    m.fill       = fillOn;
    return m;
}

SeqFrame AudioEngine::frameForBar (int64_t barIndex)
{
    SeqFrame f;
    f.kit = &rt->kit;
    f.seed = rt->seed;
    f.globalSwing = (double) globalParamDesc (GP::Swing, nullptr).toPhys (gcur[GP::Swing]);
    const Song& song = rt->song;
    if (song.songMode && song.length > 0)
    {
        int64_t total = 0;
        for (int i = 0; i < song.length; ++i)
            total += (int64_t) std::max (1, (int) song.chain[i].repeats) * rt->patterns[song.chain[i].pattern % kPatterns].lengthBars;
        if (total > 0)
        {
            const int64_t pos = wrapMod64 (barIndex, total);
            int64_t acc = 0;
            for (int i = 0; i < song.length; ++i)
            {
                const int pat = song.chain[i].pattern % kPatterns;
                const int64_t len = (int64_t) std::max (1, (int) song.chain[i].repeats) * rt->patterns[pat].lengthBars;
                if (pos < acc + len)
                {
                    f.pattern = &rt->patterns[pat];
                    f.origin = barOffset + (double) (barIndex - (pos - acc)) * beatsPerBar;
                    f.cycleBeats = (double) rt->patterns[pat].lengthBars * beatsPerBar;
                    rt->currentPattern = (uint8_t) pat;
                    shared.currentPattern.store (pat, std::memory_order_relaxed);
                    shared.songEntry.store (i, std::memory_order_relaxed);
                    frameIsSong = true;
                    return f;
                }
                acc += len;
            }
        }
    }
    frameIsSong = false;
    shared.songEntry.store (-1, std::memory_order_relaxed);
    const int pat = clamp ((int) rt->currentPattern, 0, kPatterns - 1);
    f.pattern = &rt->patterns[pat];
    f.origin = barOffset;
    f.cycleBeats = (double) rt->patterns[pat].lengthBars * beatsPerBar;
    return f;
}

void AudioEngine::onBarLine (int64_t barIndex)
{
    const bool crossed = lastBarHandled != INT64_MIN;
    const double barStart = barOffset + (double) barIndex * beatsPerBar;
    if (crossed)
    {
        fillLatched = uiFill; breakLatched = uiBreak;          // latched presses end on the bar line
        if (rt->evolveOn)
        {
            static constexpr int rates[5] = { 1, 2, 4, 8, 16 };
            const int rate = rates[globalParamDesc (GP::EvolveRate, nullptr).toIndex (gcur[GP::EvolveRate])];
            if (wrapMod64 (barIndex, rate) == 0)
            {
                const int pat = clamp ((int) rt->currentPattern, 0, kPatterns - 1);
                const float amt = gcur[GP::EvolveAmt];
                for (int t = 0; t < kTracks; ++t)
                {
                    if (rt->kit.tracks[t].locked) continue;
                    TrackPattern& tp = rt->patterns[pat].tracks[t];
                    const uint32_t seed = hashN (rt->seed, tp.seed, (uint32_t) (uint64_t) barIndex, (uint32_t) t);
                    const int role = rt->kit.tracks[t].role;
                    if (evolveTrackPattern (tp, amt, seed, role))
                    {
                        PatternChange ch; ch.pattern = (int16_t) pat; ch.track = (int16_t) t; ch.data = tp;
                        changes.push (ch);
                    }
                }
            }
        }
    }
    if (pendingPattern >= 0)
    {
        rt->currentPattern = (uint8_t) pendingPattern;
        shared.currentPattern.store (pendingPattern, std::memory_order_relaxed);
        shared.queuedPattern.store (-1, std::memory_order_relaxed);
        pendingPattern = -1;
    }
    const SeqFrame nf = frameForBar (barIndex);
    if (crossed && nf.pattern != frame.pattern) frameValidFrom = barStart - 1.0e-9;
    else if (! crossed) frameValidFrom = -1.0e18;
    frame = nf;
    lastBarHandled = barIndex;
}

void AudioEngine::pushEvent (const RtEvent& e)
{
    if (numEvents >= kMaxEvents) return;
    events[(size_t) numEvents] = e;
    events[(size_t) numEvents].order = eventOrder++;
    ++numEvents;
}

void AudioEngine::sequenceSegment (double s0, double s1, double blockB0, double bps, int numSamples)
{
    if (s1 <= s0 || frame.pattern == nullptr) return;
    const SeqMacros macros = currentMacros();
    frame.globalSwing = (double) globalParamDesc (GP::Swing, nullptr).toPhys (gcur[GP::Swing]);
    const double spb = 1.0 / bps;
    auto toSample = [&] (double beat)
    {
        return clamp ((int) std::ceil ((beat - blockB0) * spb - 1.0e-6), 0, numSamples - 1);
    };

    for (int t = 0; t < kTracks; ++t)
    {
        const TrackPattern& tp = frame.pattern->tracks[t];
        const double d = tp.stepBeats();
        if (d <= 0.0) continue;
        const float sens = tracks[t].cur[TP::GlitchSens];
        const uint32_t hitSeed = hashN (frame.seed, tp.seed, (uint32_t) t);
        const double lo = s0 - SeqEngine::kMaxLateSteps * d, hi = s1 + SeqEngine::kMaxEarlySteps * d;
        SeqEngine::forEachStepInRange (frame, tp, lo, hi, [&] (int64_t cycle, int64_t abs)
        {
            const double nominal = SeqEngine::nominalTime (frame, tp, cycle, abs);
            if (nominal < frameValidFrom) return;
            StepEval se;
            SeqEngine::evalStep (frame, macros, t, cycle, abs, sens, se);
            if (se.time >= s0 && se.time < s1)
            {
                RtEvent e {};
                e.sample = toSample (se.time); e.type = 0; e.track = (uint8_t) t; e.index = (uint8_t) se.index;
                e.glitch = se.glitch;
                pushEvent (e);
            }
            if (! se.trig) return;
            SeqHit hits[kMaxRatchets];
            const int n = SeqEngine::expandHits (se, hitSeed, hits);
            for (int i = 0; i < n; ++i)
            {
                const SeqHit& h = hits[i];
                if (h.beat < s0 || h.beat >= s1) continue;
                RtEvent e {};
                e.sample = toSample (h.beat); e.type = 1; e.track = (uint8_t) t; e.index = (uint8_t) se.index;
                e.slide = h.slide; e.pitch = h.pitch; e.vel = h.velocity; e.seed = h.seed;
                e.gateSamples = std::max (1, (int) std::lround (h.gateBeats * spb));
                pushEvent (e);
            }
        });
    }
}

void AudioEngine::sequenceBlock (int numSamples, double b0, double bps, bool continuous)
{
    // An event at beat t plays on the first sample at or after t, i.e. sample ceil((t - b0) / bps).
    // So this block owns the beats (b0 - bps, b0 + (n-1) bps]; consecutive blocks tile exactly.
    const double eps = bps * 1.0e-6;
    const double rangeEnd = b0 + (double) (numSamples - 1) * bps + eps;
    const double rangeStart = continuous ? std::min (prevRangeEnd, rangeEnd) : b0 - bps + eps;
    prevRangeEnd = rangeEnd;
    double s = rangeStart;
    int guard = 0;
    while (s < rangeEnd && guard++ < 64)
    {
        const int64_t bar = (int64_t) std::floor ((s - barOffset) / beatsPerBar + 1.0e-9);
        if (bar != lastBarHandled) onBarLine (bar);
        const double nextBar = barOffset + (double) (bar + 1) * beatsPerBar;
        const double e = std::min (nextBar, rangeEnd);
        sequenceSegment (s, e, b0, bps, numSamples);
        s = e > s ? e : rangeEnd;
    }
    std::sort (events.begin(), events.begin() + numEvents, [] (const RtEvent& a, const RtEvent& b)
    {
        if (a.sample != b.sample) return a.sample < b.sample;
        if (a.type != b.type) return a.type < b.type;
        return a.order < b.order;
    });
}

// ============================================================================ modulation / params
float AudioEngine::sourceValue (const ModSlot& s) const
{
    if (srcIsPerTrack (s.src)) return tracks[s.srcTrack % kTracks].src[s.src];
    return gsrc[s.src];
}

void AudioEngine::computeTrackParams (int t)
{
    TrackRt& tr = tracks[t];
    const float* base = rt->kit.tracks[t].params;
    if (tr.anyLock) { for (int i = 0; i < TP::count; ++i) tr.cur[i] = tr.lockOn[i] ? tr.lockVal[i] : base[i]; }
    else std::memcpy (tr.cur, base, sizeof (tr.cur));

    for (int k = 0; k < modCount[t]; ++k)
    {
        const ModSlot& s = rt->kit.mods[modIndex[t][k]];
        tr.cur[s.dstParam] += sourceValue (s) * s.amount;
    }
    // Chaos macro: the attractor nudges timbre, position and pitch, a little differently per track
    const float c = gcur[GP::Chaos];
    if (c > 0.001f)
    {
        const float sx = (t & 1) ? -1.f : 1.f, sy = (t & 2) ? -1.f : 1.f;
        tr.cur[TP::FltCut] += 0.20f * c * gsrc[Src::ChaosX] * sx;
        tr.cur[TP::Pan]    += 0.30f * c * gsrc[Src::ChaosY] * sy;
        tr.cur[TP::Fine]   += 0.10f * c * gsrc[Src::ChaosX] * sy;
    }
    if (modCount[t] > 0 || c > 0.001f)
        for (int i = 0; i < TP::count; ++i) tr.cur[i] = clamp01 (tr.cur[i]);
}

void AudioEngine::controlTick (int samplesUntilNext)
{
    const double dt = (double) kCtrl / sr;
    const float* gp = rt->kit.gparams;

    // ---- global sources
    {
        const float rate = globalParamDesc (GP::ChaosRate, nullptr).toPhys (gp[GP::ChaosRate]);
        const int type = globalParamDesc (GP::ChaosType, nullptr).toIndex (gp[GP::ChaosType]);
        chaos.tick (type, rate, gp[GP::ChaosShape], dt);
        gsrc[Src::ChaosX] = chaos.outX; gsrc[Src::ChaosY] = chaos.outY;
        for (int i = 0; i < 4; ++i) gsrc[Src::Macro1 + i] = gp[GP::Macro1 + i];
        follower.set (globalParamDesc (GP::FollowAtk, nullptr).toPhys (gp[GP::FollowAtk]),
                      globalParamDesc (GP::FollowRel, nullptr).toPhys (gp[GP::FollowRel]),
                      globalParamDesc (GP::FollowGain, nullptr).toPhys (gp[GP::FollowGain]), (float) sr);
        gsrc[Src::Follower] = follower.value();
    }

    // ---- per-track sources
    bool anySolo = false;
    for (int t = 0; t < kTracks; ++t) anySolo |= rt->kit.tracks[t].solo != 0;
    for (int t = 0; t < kTracks; ++t)
    {
        TrackRt& tr = tracks[t];
        const TrackSound& ts = rt->kit.tracks[t];
        tr.audible = ! ts.mute && (! anySolo || ts.solo);
        for (int l = 0; l < 2; ++l)
        {
            const int o = l == 0 ? (int) TP::Lfo1Rate : (int) TP::Lfo2Rate;
            LfoParams lp;
            lp.rate = tr.cur[o];
            lp.shape = commonTrackParamDesc (o + 1).toIndex (ts.params[o + 1]);
            lp.sync = ts.params[o + 2] >= 0.5f;
            lp.phase = ts.params[o + 3];
            lp.mode = commonTrackParamDesc (o + 4).toIndex (ts.params[o + 4]);
            tr.src[Src::Lfo1 + l] = tr.lfo[l].tick (lp, ts.lfoDraw[l], dt, curBpm, curBeat, running);
        }
        {
            const float glide = tr.cur[TP::StepGlide];
            const float coef = glide <= 0.001f ? 1.f : 1.f - std::exp (-(float) dt / (0.002f + glide * glide * 0.5f));
            tr.stepModCur += coef * (tr.stepModTarget - tr.stepModCur);
            tr.src[Src::StepMod] = tr.stepModCur;
        }
        // envelope sources come from the newest active voice
        int newest = -1;
        for (int i = 0; i < kVoicesPerTrack; ++i)
            if (tr.voices[i].active() && (newest < 0 || tr.voices[i].age > tr.voices[newest].age)) newest = i;
        tr.src[Src::ModEnv] = newest >= 0 ? tr.voices[newest].modEnvLevel() : 0.f;
        tr.src[Src::AmpEnv] = newest >= 0 ? tr.voices[newest].ampEnvLevel() : 0.f;
    }

    // ---- global params: base, then locks, then modulation
    std::memcpy (gcur, gp, sizeof (gcur));
    for (int t = 0; t < kTracks; ++t)
        for (int k = 0; k < tracks[t].numGLocks; ++k) gcur[tracks[t].gLockParam[k]] = tracks[t].gLockVal[k];
    for (int k = 0; k < modCount[kTracks]; ++k)
    {
        const ModSlot& s = rt->kit.mods[modIndex[kTracks][k]];
        gcur[s.dstParam] = clamp01 (gcur[s.dstParam] + sourceValue (s) * s.amount);
    }
    fillOn = hostFill || uiFill || fillLatched;
    breakOn = hostBreak || uiBreak || breakLatched;

    // Space macro scales the sends
    const float space = gcur[GP::Space] * 2.f;
    const float sendScale = space * std::sqrt (space);

    FxContext ctx;
    ctx.sampleRate = sr; ctx.bpm = curBpm; ctx.beat = curBeat; ctx.playing = running;

    for (int t = 0; t < kTracks; ++t)
    {
        TrackRt& tr = tracks[t];
        computeTrackParams (t);
        for (auto& v : tr.voices) if (v.active()) v.control (tr.cur, samplesUntilNext);

        const float lvl = tr.audible ? levelGain (tr.cur[TP::Level]) : 0.f;
        const float pan = tr.cur[TP::Pan];                      // 0..1
        const float gl = lvl * std::cos (pan * 0.5f * kPiF) * 1.41421356f;
        const float gr = lvl * std::sin (pan * 0.5f * kPiF) * 1.41421356f;
        tr.gainL.set (gl, kGainRamp); tr.gainR.set (gr, kGainRamp);
        const float sa = tr.cur[TP::SendA], sb = tr.cur[TP::SendB];
        tr.sendA.set (sa * sa * sendScale, kGainRamp); tr.sendB.set (sb * sb * sendScale, kGainRamp);
        for (int k = 0; k < kInsertsPerTrack; ++k)
            if (tr.fx[k])
            {
                const int base = k == 0 ? (int) TP::FxA0 : (int) TP::FxB0;
                tr.fx[k]->setParams (&tr.cur[base], ctx);
                tr.fxMix[k].set (tr.cur[base + kFxParams], kGainRamp);
            }
    }
    for (int s = 0; s < kSendBuses * kSendSlots + kMasterSlots; ++s)
        if (busFx[s])
        {
            const int base = fxSlotParamBase (kFirstSendSlot + s);
            busFx[s]->setParams (&gcur[base], ctx);
            busFxMix[s].set (gcur[base + kFxParams], kGainRamp);
        }
    masterGain.set (levelGain (gcur[GP::Master]), kGainRamp);
    retA.set (levelGain (gcur[GP::SendAReturn]), kGainRamp);
    retB.set (levelGain (gcur[GP::SendBReturn]), kGainRamp);
    limDrive.set (dbToGain (globalParamDesc (GP::LimitDrive, nullptr).toPhys (gcur[GP::LimitDrive])), kGainRamp);

    // ---- publish for the UI now and then
    if ((++publishCounter & 15u) == 0)
    {
        const TrackRt& tr = tracks[selectedTrack];
        for (int i = 0; i < TP::count; ++i) shared.modValue[i].store (tr.cur[i], std::memory_order_relaxed);
        for (int i = 0; i < GP::count; ++i) shared.globalModValue[i].store (gcur[i], std::memory_order_relaxed);
        for (int i = 0; i < Src::count; ++i)
            shared.srcValue[i].store (srcIsPerTrack (i) ? tr.src[i] : gsrc[i], std::memory_order_relaxed);
    }
}

// ============================================================================ rendering
static inline void runSlot (Effect& fx, LinearSmooth& mix, float* l, float* r, float* tl, float* tr, int n, const FxContext& ctx)
{
    if (! mix.moving() && mix.cur >= 0.999f) { fx.process (l, r, n, ctx); return; }
    std::memcpy (tl, l, sizeof (float) * (size_t) n);
    std::memcpy (tr, r, sizeof (float) * (size_t) n);
    fx.process (tl, tr, n, ctx);
    for (int i = 0; i < n; ++i)
    {
        const float m = mix.tick();
        l[i] += (tl[i] - l[i]) * m;
        r[i] += (tr[i] - r[i]) * m;
    }
}

void AudioEngine::renderSlice (ProcessIO& io, int pos, int n)
{
    std::memset (mixL, 0, sizeof (float) * (size_t) n);
    std::memset (mixR, 0, sizeof (float) * (size_t) n);
    for (int b = 0; b < kSendBuses; ++b)
    {
        std::memset (sendL[b], 0, sizeof (float) * (size_t) n);
        std::memset (sendR[b], 0, sizeof (float) * (size_t) n);
    }

    FxContext ctx;
    ctx.sampleRate = sr; ctx.bpm = curBpm; ctx.beat = curBeat; ctx.playing = running;

    // external sidechain + follower
    if (io.sidechain[0] != nullptr)
    {
        ctx.scL[9] = io.sidechain[0] + pos;
        ctx.scR[9] = (io.sidechain[1] ? io.sidechain[1] : io.sidechain[0]) + pos;
        follower.process (ctx.scL[9], ctx.scR[9], n);
    }
    else follower.decay (n);

    // ---- 1. voices, level and pan; keep a pre-insert tap for sidechains
    for (int t = 0; t < kTracks; ++t)
    {
        TrackRt& tr = tracks[t];
        std::memset (tr.bufL, 0, sizeof (float) * (size_t) n);
        std::memset (tr.bufR, 0, sizeof (float) * (size_t) n);
        for (auto& v : tr.voices) if (v.active()) v.render (tr.bufL, tr.bufR, n);
        for (int i = 0; i < n; ++i)
        {
            tr.bufL[i] *= tr.gainL.tick();
            tr.bufR[i] *= tr.gainR.tick();
        }
        std::memcpy (tr.preL, tr.bufL, sizeof (float) * (size_t) n);
        std::memcpy (tr.preR, tr.bufR, sizeof (float) * (size_t) n);
        ctx.scL[1 + t] = tr.preL; ctx.scR[1 + t] = tr.preR;
    }

    // ---- 2. glitcher, inserts, sends, routing
    for (int t = 0; t < kTracks; ++t)
    {
        TrackRt& tr = tracks[t];
        const TrackSound& ts = rt->kit.tracks[t];
        tr.glitch.process (tr.bufL, tr.bufR, n);
        for (int k = 0; k < kInsertsPerTrack; ++k)
            if (tr.fx[k]) runSlot (*tr.fx[k], tr.fxMix[k], tr.bufL, tr.bufR, tmpL, tmpR, n, ctx);

        float pk = tr.peak;
        const bool direct = ts.directOut != 0 && io.trackOut[t][0] != nullptr;
        for (int i = 0; i < n; ++i)
        {
            float l = tr.bufL[i], r = tr.bufR[i];
            if (! (std::fabs (l) < 64.f)) l = tr.bufL[i] = 0.f;          // contain anything pathological
            if (! (std::fabs (r) < 64.f)) r = tr.bufR[i] = 0.f;
            pk = std::max (pk, std::max (std::fabs (l), std::fabs (r)));
            const float sa = tr.sendA.tick(), sb = tr.sendB.tick();
            sendL[0][i] += l * sa; sendR[0][i] += r * sa;
            sendL[1][i] += l * sb; sendR[1][i] += r * sb;
            if (! direct) { mixL[i] += l; mixR[i] += r; }
        }
        tr.peak = pk;
        if (io.trackOut[t][0] != nullptr)
        {
            std::memcpy (io.trackOut[t][0] + pos, tr.bufL, sizeof (float) * (size_t) n);
            if (io.trackOut[t][1] != nullptr) std::memcpy (io.trackOut[t][1] + pos, tr.bufR, sizeof (float) * (size_t) n);
        }
    }

    // ---- 3. send buses
    for (int b = 0; b < kSendBuses; ++b)
    {
        for (int k = 0; k < kSendSlots; ++k)
        {
            const int s = b * kSendSlots + k;
            if (busFx[s]) runSlot (*busFx[s], busFxMix[s], sendL[b], sendR[b], tmpL, tmpR, n, ctx);
        }
        LinearSmooth& ret = b == 0 ? retA : retB;
        // a send bus with no effect loaded would just double the dry signal: keep it silent
        const bool hasFx = busFx[b * kSendSlots] || busFx[b * kSendSlots + 1];
        for (int i = 0; i < n; ++i)
        {
            const float g = ret.tick();
            if (hasFx) { mixL[i] += sendL[b][i] * g; mixR[i] += sendR[b][i] * g; }
        }
    }

    // ---- 4. master chain
    for (int k = 0; k < kMasterSlots; ++k)
    {
        const int s = kSendBuses * kSendSlots + k;
        if (busFx[s]) runSlot (*busFx[s], busFxMix[s], mixL, mixR, tmpL, tmpR, n, ctx);
    }
    const bool limOn = gcur[GP::LimitOn] >= 0.5f;
    float* outL = io.main[0] + pos;
    float* outR = io.main[1] ? io.main[1] + pos : nullptr;
    for (int i = 0; i < n; ++i)
    {
        const float g = masterGain.tick();
        float l = mixL[i] * g, r = mixR[i] * g;
        if (! (std::fabs (l) < 64.f)) l = 0.f;
        if (! (std::fabs (r) < 64.f)) r = 0.f;
        const float drive = limDrive.tick();
        if (limOn) limiter.process (l, r, drive);
        mainPeak[0] = std::max (mainPeak[0], std::fabs (l));
        mainPeak[1] = std::max (mainPeak[1], std::fabs (r));
        outL[i] = l;
        if (outR) outR[i] = r;
    }
}

void AudioEngine::process (ProcessIO& io)
{
    curIO = &io;
    io.numMidiOut = 0;
    drainCommands();
    const int n = io.numSamples;
    if (n <= 0 || io.main[0] == nullptr) { curIO = nullptr; return; }

    // ---- transport
    const TransportInfo& tp = io.transport;
    const bool hostPlaying = tp.valid && tp.playing;
    double b0 = 0.0;
    if (hostPlaying)
    {
        curBpm = tp.bpm > 1.0 ? tp.bpm : 120.0;
        b0 = tp.ppq;
        beatsPerBar = std::max (0.25, (double) tp.tsNum * 4.0 / (double) std::max (1, tp.tsDen));
        barOffset = tp.barStartPpq - std::floor (tp.barStartPpq / beatsPerBar) * beatsPerBar;
        if (barOffset > beatsPerBar - 1.0e-6) barOffset = 0.0;
    }
    else if (internalPlay)
    {
        curBpm = (tp.valid && tp.bpm > 1.0) ? tp.bpm : (double) globalParamDesc (GP::Tempo, nullptr).toPhys (rt->kit.gparams[GP::Tempo]);
        b0 = internalBeat;
        beatsPerBar = 4.0; barOffset = 0.0;
    }
    else if (tp.valid && tp.bpm > 1.0) curBpm = tp.bpm;
    else curBpm = (double) globalParamDesc (GP::Tempo, nullptr).toPhys (rt->kit.gparams[GP::Tempo]);

    running = hostPlaying || internalPlay;
    beatsPerSample = curBpm / 60.0 / sr;
    numEvents = 0;
    if (running)
    {
        bool continuous = wasPlaying && std::fabs (b0 - prevEndBeat) < std::max (2.0 * beatsPerSample, 1.0e-7);
        if (! wasPlaying)
        {
            chaos.reset();
            for (int t = 0; t < kTracks; ++t)
                for (int l = 0; l < 2; ++l) tracks[t].lfo[l].reset (hashN (rt->seed, (uint32_t) t, (uint32_t) l));
            lastBarHandled = INT64_MIN;
        }
        else if (! continuous)
        {
            allNotesOff (false);
            lastBarHandled = INT64_MIN;
        }
        blockBeat = b0; curBeat = std::round (b0 * 4294967296.0) / 4294967296.0;
        sequenceBlock (n, b0, beatsPerSample, continuous);
        prevEndBeat = b0 + (double) n * beatsPerSample;
        if (! hostPlaying) internalBeat = prevEndBeat;
    }
    else if (wasPlaying)
    {
        allNotesOff (false);
        for (int t = 0; t < kTracks; ++t)
        {
            clearLocks (tracks[t]); tracks[t].curStep = -1; tracks[t].stepModTarget = 0.f;
            shared.step[t].store (-1, std::memory_order_relaxed);
        }
        fillLatched = breakLatched = false;
    }
    wasPlaying = running;

    // ---- render, split at events and control ticks
    int pos = 0, ev = 0, mi = 0;
    while (pos < n)
    {
        while (mi < io.numMidiIn && io.midiIn[mi].sample <= pos) handleMidiIn (io.midiIn[mi++]);
        while (ev < numEvents && events[(size_t) ev].sample <= pos)
        {
            const RtEvent& e = events[(size_t) ev++];
            if (e.type == 0) handleStepStart (e); else handleNoteOn (e);
        }
        if (ctrlPhase == 0) controlTick (kCtrl);
        int len = std::min (n - pos, kCtrl - ctrlPhase);
        if (ev < numEvents) len = std::min (len, events[(size_t) ev].sample - pos);
        if (mi < io.numMidiIn) len = std::min (len, io.midiIn[mi].sample - pos);
        len = std::max (1, len);
        curSlicePos = pos;
        renderSlice (io, pos, len);
        pos += len;
        ctrlPhase = (ctrlPhase + len) % kCtrl;
        if (running)
        {
            // From the block start (not accumulated), snapped to a 2^-32 beat grid: the same sample gets the
            // same position whatever the host's block boundaries, so synced LFOs and effects are split-independent.
            const double b = blockBeat + (double) pos * beatsPerSample;
            curBeat = std::round (b * 4294967296.0) / 4294967296.0;
        }
    }
    advancePendingOffs (n);

    // ---- publish
    for (int t = 0; t < kTracks; ++t)
    {
        shared.trackPeak[t].store (tracks[t].peak, std::memory_order_relaxed);
        tracks[t].peak *= 0.0f;
    }
    shared.mainPeak[0].store (mainPeak[0], std::memory_order_relaxed);
    shared.mainPeak[1].store (mainPeak[1], std::memory_order_relaxed);
    mainPeak[0] = mainPeak[1] = 0.f;
    shared.gainReduction.store (limiter.gainReduction(), std::memory_order_relaxed);
    shared.playing.store (running, std::memory_order_relaxed);
    shared.fill.store (fillOn, std::memory_order_relaxed);
    shared.brk.store (breakOn, std::memory_order_relaxed);
    shared.beat.store (curBeat, std::memory_order_relaxed);
    shared.bpm.store ((float) curBpm, std::memory_order_relaxed);
    curIO = nullptr;
}

} // namespace atm
