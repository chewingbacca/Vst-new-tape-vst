#include "Engine.h"

namespace splinter
{
static inline double midiToHz (double note) { return 440.0 * std::exp2 ((note - 69.0) / 12.0); }

Engine::Engine()
{
    for (int i = 0; i < kNumSlots; ++i)
    {
        timeCurves[i].clear(); timeCurves[i].add (0.0f, 0.0f); timeCurves[i].add (1.0f, 0.0f);
        volCurves[i].clear(); volCurves[i].add (0.0f, 1.0f); volCurves[i].add (1.0f, 1.0f);
    }
    loopSlices.makeGrid (4.0f, 4);
    liveSlices.makeGrid (8.0f, 4);
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate;
    ring.prepare ((int) (sampleRate * 40.0) + 4 * kSourceGuard);     // 8 bars down to 50 BPM
    harm.prepare (sampleRate);
    harmLatency = harm.fftSize();
    limiter.prepare (sampleRate);
    limLatency = limiter.latency();
    latency = kReaderLookahead + harmLatency + limLatency;

    for (auto& d : mainDelay) d.prepare (harmLatency + 4);
    for (auto& d : dryDelay) d.prepare (harmLatency + 4);
    for (auto& d : inLine) d.prepare (latency + 8);

    int cs = 1; while (cs < kReaderLookahead + harmLatency + 8) cs <<= 1;
    clockDelay.assign ((size_t) cs, 0.0); playDelay.assign ((size_t) cs, 0); clockMask = cs - 1;

    crusher.prepare (sampleRate); comb.prepare (sampleRate); ring_.prepare (sampleRate); sweep.prepare (sampleRate);
    freeze.prepare (sampleRate); delayFx.prepare (sampleRate); reverbFx.prepare (sampleRate);

    const int ramp = std::max (8, (int) (0.02 * sampleRate));
    for (auto* s : { &mixSm, &widthSm, &gainSm, &volDepthSm, &warpDepthSm, &delayLevelSm, &reverbLevelSm, &limiterSm })
        s->setRampSamples (ramp);
    for (auto& e : laneEnv) e.setTime (sampleRate, 5.0);
    laneEnv[laneFilter].setTime (sampleRate, 12.0);
    harmEnv.setTime (sampleRate, 8.0);
    // Playback opens instantly when it starts on quiet audio (a hit sitting on
    // the beat keeps its whole attack), otherwise over about a millisecond.
    loopGain.setTimes (sampleRate, 1.2, 6.0); dryGain.setTimes (sampleRate, 1.2, 6.0);
    sourceFade.setTime (sampleRate, 25.0); warpGain.setTime (sampleRate, 3.5);
    bypassFade.setTime (sampleRate, 12.0); hpMix.setTime (sampleRate, 25.0); lpMix.setTime (sampleRate, 25.0);
    for (auto& v : voices) v.gain.setTime (sampleRate, 4.0);
    reset();
}

void Engine::reset()
{
    ring.clear();
    loopReader.reset(); liveReader.reset(); dryReader.reset();
    for (auto& v : voices) { v.active = false; v.reader.reset(); v.gain.reset (0.0f); }
    harm.reset();
    for (auto* arr : { mainDelay, inLine, dryDelay }) for (int c = 0; c < 2; ++c) arr[c].reset();
    std::fill (clockDelay.begin(), clockDelay.end(), beat);
    std::fill (playDelay.begin(), playDelay.end(), (uint8_t) 0);
    crusher.reset(); comb.reset(); sweep.reset(); freeze.reset(); delayFx.reset(); reverbFx.reset();
    for (auto& e : laneEnv) e.reset (0.0f);
    for (auto& h : held) h = HeldRun();
    harmEnv.reset (0.0f); loopGain.reset (0.0f); sourceFade.reset (0.0f); dryGain.reset (0.0f); warpGain.reset (1.0f); bypassFade.reset (0.0f);
    volSlew.reset (1.0f);
    for (int c = 0; c < 2; ++c) { outHp[c].reset(); outLp[c].reset(); }
    tilt.reset(); limiter.reset();
    mixSm.reset (1.0f); widthSm.reset (1.0f); gainSm.reset (1.0f); volDepthSm.reset (1.0f); warpDepthSm.reset (1.0f);
    delayLevelSm.reset (0.7f); reverbLevelSm.reset (0.6f); limiterSm.reset (1.0f);
    hpMix.reset (0.0f); lpMix.reset (0.0f); outUpdateCounter = 0;
    for (auto& e : evalWarp) e.invalidate();
    for (auto& e : evalPost) e.invalidate();
    freezeWasOn = false; freezeRunStart = -1.0e18;
    firstBlock = true;
}

// ---------------------------------------------------------------- per-block setup
void Engine::updateBlockParams (const EngineParams& p)
{
    float probScale[kNumLanes], inject[kNumLanes];
    for (int i = 0; i < kNumLanes; ++i) { probScale[i] = 1.0f; inject[i] = 0.0f; }
    auto group = [&] (float macro, std::initializer_list<std::pair<int, float>> lanes)
    {
        const float ps = std::min (1.0f, 2.0f * macro), inj = std::max (0.0f, 2.0f * macro - 1.0f);
        for (auto& l : lanes) { probScale[l.first] = ps; inject[l.first] = inj * l.second; }
    };
    group (p.chop, { { laneChop, 1.0f } });
    group (p.stutter, { { laneStutter, 1.0f }, { laneFreeze, 0.35f } });
    group (p.warp, { { laneReverse, 0.8f }, { laneTapeStop, 1.0f }, { lanePitch, 0.6f } });
    group (p.crush, { { laneCrush, 1.0f }, { laneComb, 0.5f }, { laneRing, 0.4f } });

    const float key[8] = { p.chop, p.stutter, p.warp, p.crush, p.chaos, (float) p.seed, 0.0f, 0.0f };
    bool changed = false;
    for (int i = 0; i < 8; ++i) if (key[i] != lastMacroKey[i]) { lastMacroKey[i] = key[i]; changed = true; }
    if (changed) ++seqVersion;

    for (auto* s : { &seqWarp, &seqPost })
    {
        for (int i = 0; i < kNumLanes; ++i) { s->probScale[i] = probScale[i]; s->inject[i] = inject[i]; }
        s->chaos = p.chaos; s->seed = (uint32_t) p.seed;
    }
    seqWarp.pattern = &seqs[curSeqSlotWarp]; seqWarp.version = seqVersion * 16U + (uint32_t) curSeqSlotWarp;
    seqPost.pattern = &seqs[curSeqSlotPost]; seqPost.version = seqVersion * 16U + (uint32_t) curSeqSlotPost;

    // slot selection: a parameter change overrides a MIDI selection and vice versa
    if (p.timeSlot != lastParamTimeSlot) { lastParamTimeSlot = p.timeSlot; wantTimeSlot = clampv (p.timeSlot, 0, kNumSlots - 1); }
    if (p.volSlot != lastParamVolSlot) { lastParamVolSlot = p.volSlot; wantVolSlot = clampv (p.volSlot, 0, kNumSlots - 1); }
    if (p.seqSlot != lastParamSeqSlot) { lastParamSeqSlot = p.seqSlot; wantSeqSlot = clampv (p.seqSlot, 0, kNumSlots - 1); }

    const int lb = clampv (p.liveBars, 1, 8) * 4;
    if ((int) liveSlices.loopBeats != lb) liveSlices.makeGrid ((float) lb, 4);

    mixSm.setTarget (clampv (p.mix, 0.0f, 1.0f));
    widthSm.setTarget (clampv (p.width, 0.0f, 2.0f));
    gainSm.setTarget (dbToGain (p.outGainDb));
    volDepthSm.setTarget (clampv (p.volDepth, 0.0f, 1.0f));
    warpDepthSm.setTarget (std::min (1.0f, 2.0f * clampv (p.warp, 0.0f, 1.0f)));
    delayLevelSm.setTarget (p.delayLevel); reverbLevelSm.setTarget (p.reverbLevel);
    limiterSm.setTarget (p.limiter ? 1.0f : 0.0f);
    volSlew.setTime (sr, std::max (0.5f, p.volSmoothMs));

    hpTarget = clampv (p.hpFreq, 20.0f, 2000.0f); lpTarget = clampv (p.lpFreq, 200.0f, 20000.0f);
    qTarget = clampv (p.filterQ, 0.5f, 4.0f); tiltTarget = clampv (p.tiltDb, -12.0f, 12.0f);
    harm.setFormant (p.formant);
    if (firstBlock)
    {
        firstBlock = false;
        curTimeSlot = wantTimeSlot; curVolSlot = wantVolSlot; curSeqSlotWarp = curSeqSlotPost = wantSeqSlot;
        mixSm.reset (mixSm.target); widthSm.reset (widthSm.target); gainSm.reset (gainSm.target);
        volDepthSm.reset (volDepthSm.target); warpDepthSm.reset (warpDepthSm.target);
        delayLevelSm.reset (delayLevelSm.target); reverbLevelSm.reset (reverbLevelSm.target); limiterSm.reset (limiterSm.target);
        sourceFade.reset (p.sourceMode == 1 ? 1.0f : 0.0f); bypassFade.reset (p.bypass ? 1.0f : 0.0f);
        hpSm = hpTarget; lpSm = lpTarget; qSm = qTarget; tiltSm = tiltTarget;
        hpMix.reset (hpTarget > 21.0f ? 1.0f : 0.0f); lpMix.reset (lpTarget < 19900.0f ? 1.0f : 0.0f);
        for (int c = 0; c < 2; ++c) { outHp[c].set (sr, hpSm, qSm); outLp[c].set (sr, lpSm, qSm); }
        tilt.set (sr, tiltSm);
        seqWarp.pattern = &seqs[curSeqSlotWarp]; seqPost.pattern = &seqs[curSeqSlotPost];
    }
}

// ---------------------------------------------------------------- MIDI
void Engine::handleMidi (const MidiEvent& e, const EngineParams& p)
{
    if (e.allOff)
    {
        for (auto& v : voices) if (v.active) { v.releasing = true; }
        for (auto& h : held) if (h.down) { h.down = false; h.end = beat; }
        return;
    }
    if (p.midiMode == 0)
    {
        const bool live = p.sourceMode == 1;
        const SliceSet& sl = activeSlices (live);
        if (e.on)
        {
            const int s = e.note - 36;
            if (s < 0 || s >= sl.count) return;
            SliceVoice* v = nullptr;
            for (auto& c : voices) if (c.active && c.note == e.note) { v = &c; break; }   // retrigger same key
            if (v == nullptr) for (auto& c : voices) if (! c.active) { v = &c; break; }
            if (v == nullptr) { v = &voices[0]; for (auto& c : voices) if (c.releasing) { v = &c; break; } }
            v->active = true; v->releasing = false; v->note = e.note; v->slice = s; v->fresh = true; v->live = live;
            v->velocity = 0.35f + 0.65f * clampv (e.velocity, 0.0f, 1.0f);
            const double lb = sl.loopBeats;
            double base = sl.startBeat[s];
            if (live)
            {
                base += std::floor (beat / lb) * lb;
                while (base > beat - 1.0e-6) base -= lb;
            }
            v->srcStart = base;
            v->endLen = p.sliceTrig == 1 ? (double) (sl.sliceEnd (s) - sl.startBeat[s]) : 0.0;
        }
        else
            for (auto& c : voices) if (c.active && c.note == e.note && c.endLen <= 0.0) c.releasing = true;
        return;
    }
    // patterns mode
    if (e.note >= 36 && e.note < 36 + kNumSlots) { if (e.on) wantTimeSlot = e.note - 36; }
    else if (e.note >= 48 && e.note < 48 + kNumSlots) { if (e.on) wantVolSlot = e.note - 48; }
    else if (e.note >= 60 && e.note < 60 + kNumSlots) { if (e.on) wantSeqSlot = e.note - 60; }
    else if (e.note >= 72 && e.note < 72 + kNumLanes) holdLane (e.note - 72, e.on);
}

void Engine::holdLane (int lane, bool down)
{
    if (lane < 0 || lane >= kNumLanes) return;
    HeldRun& h = held[lane];
    if (down) { h.down = true; h.start = beat; h.end = 1.0e18; h.var = 0; h.rnd = hash32 (++heldCounter * 2654435761U); }
    else if (h.down) { h.down = false; h.end = beat; }
}

// ---------------------------------------------------------------- lane lookup
const LaneRun& Engine::laneRun (int lane, double t, bool post)
{
    HeldRun& h = held[lane];
    if (h.down && t < h.start - 0.5) h.start = t;      // transport jumped backwards while held
    if (t >= h.start && t < h.end)
    {
        heldScratch.active = true; heldScratch.held = true;
        heldScratch.start = h.start;
        heldScratch.length = (lane == laneFilter) ? 4.0 : 1.0;
        heldScratch.var = h.var; heldScratch.rnd = h.rnd;
        return heldScratch;
    }
    return post ? evalPost[lane].query (seqPost, lane, t) : evalWarp[lane].query (seqWarp, lane, t);
}

// ---------------------------------------------------------------- time maps
double Engine::warpTime (double t, float& gain, const EngineParams& p, bool allowFuture)
{
    // ---- tape stop
    {
        const LaneRun& r = laneRun (laneTapeStop, t, false);
        if (r.active)
        {
            const double u = t - r.start, L = r.length;
            if (r.var == 2) // spin up
            {
                if (u < L)
                {
                    const double c = u / L;
                    t = allowFuture ? r.start + L - (L / 3.0) * (1.0 - c * c * c) : r.start - L / 3.0 + (L / 3.0) * c * c * c;
                    gain *= (float) std::min (1.0, c * 6.0);
                }
            }
            else if (r.var == 3) // half speed drag
                t = r.start + u * 0.5;
            else
            {
                const double D = std::max (0.02, L * clampv ((double) p.tapeTime, 0.05, 1.0));
                if (u < D)
                {
                    const double c = 1.0 - u / D;
                    t = r.start + (D / 3.0) * (1.0 - c * c * c);
                    gain *= (float) std::min (1.0, c * 5.0);
                }
                else { t = r.start + D / 3.0; gain = 0.0f; }
            }
        }
    }
    // ---- stutter
    {
        const LaneRun& r = laneRun (laneStutter, t, false);
        if (r.active)
        {
            const double base = stutterDivBeats (r.var > 0 ? r.var - 1 : p.stutterDiv);
            const double u = std::max (0.0, t - r.start);
            const double L = r.held ? base * 8.0 : r.length;
            const double g = 1.0 - 0.45 * clampv ((double) p.stutterAccel, -1.0, 1.0); // repeat length ratio
            int k; double uk, lenK;
            if (std::fabs (g - 1.0) < 1.0e-3)
            {
                k = (int) std::floor (u / base); uk = u - k * base; lenK = base;
            }
            else
            {
                // repeat k starts at base*(1-g^k)/(1-g)
                const double arg = 1.0 - u * (1.0 - g) / base;
                const double minLen = 1.0 / 64.0;
                k = arg > 1.0e-9 ? (int) std::floor (std::log (arg) / std::log (g) + 1.0e-9) : 1000;
                k = std::max (0, k);
                lenK = base * std::pow (g, (double) k);
                if (lenK < minLen || k >= 1000)
                {
                    // repeats got as short as they may: continue at the minimum length
                    const int kMin = (int) std::ceil (std::log (minLen / base) / std::log (g));
                    const double startMin = base * (1.0 - std::pow (g, (double) kMin)) / (1.0 - g);
                    if (u >= startMin && g < 1.0)
                    {
                        const double l2 = base * std::pow (g, (double) kMin);
                        const int extra = (int) std::floor ((u - startMin) / l2);
                        k = kMin + extra; lenK = l2; uk = (u - startMin) - extra * l2;
                    }
                    else { lenK = std::max (lenK, minLen); uk = std::fmod (u, lenK); }
                }
                else
                    uk = u - base * (1.0 - std::pow (g, (double) k)) / (1.0 - g);
            }
            const int total = std::max (1, (int) std::floor (L / base + 1.0e-6));
            double semis = 0.0;
            if (p.stutterArp > 0)
            {
                const int cnt = scaleDef (p.scale).count;
                const int degs[4] = { 0, 2, 4, cnt };
                int idx = k & 3;
                if (p.stutterArp == 2) idx = 3 - (k & 3);
                else if (p.stutterArp == 3) { static const int ud[6] = { 0, 1, 2, 3, 2, 1 }; idx = ud[k % 6]; }
                else if (p.stutterArp == 4) idx = (int) (hash3 (r.rnd, 17U, (uint32_t) k) & 3U);
                semis = (double) degreeToSemis (p.scale, degs[idx]);
            }
            if (p.stutterPitch != 0.0f && k > 0)
                semis += (double) p.stutterPitch * (double) std::min (k, total - 1) / (double) std::max (1, total - 1);
            const double rate = semis == 0.0 ? 1.0 : semisToRatio (semis);
            t = r.start + uk * rate;
            if (uk > lenK * clampv ((double) p.stutterGate, 0.05, 1.0)) gain = 0.0f;
            if (p.stutterDecay > 0.0f)
                gain *= std::max (0.0f, 1.0f - p.stutterDecay * (float) std::min (k, total) / (float) total);
        }
    }
    // ---- reverse
    {
        const LaneRun& r = laneRun (laneReverse, t, false);
        if (r.active)
        {
            const double u = t - r.start;
            t = (allowFuture && ! r.held) ? r.start + r.length - u : r.start - u;
        }
    }
    // ---- pitch (tape style)
    {
        const LaneRun& r = laneRun (lanePitch, t, false);
        if (r.active)
        {
            int deg = p.pitchDegrees;
            if (r.var > 0) deg = r.var <= 7 ? r.var - 8 : r.var - 7;      // 1..7 => -7..-1, 8..14 => +1..+7
            const double S = (double) degreeToSemis (p.scale, deg);
            const double L = r.length, u = t - r.start;
            const double gl = clampv ((double) p.pitchGlide, 0.0, 1.0);
            const double ln2 = 0.6931471805599453;
            const double c0 = S * (1.0 - gl) / 12.0, c1 = S * gl / (12.0 * L);
            auto P = [&] (double x)
            {
                const double xc = std::min (x, L);
                double v = std::fabs (c1) < 1.0e-9 ? std::exp2 (c0) * xc : std::exp2 (c0) * (std::exp2 (c1 * xc) - 1.0) / (c1 * ln2);
                if (x > L) v += std::exp2 (S / 12.0) * (x - L);
                return v;
            };
            double off = 0.0;
            if (! allowFuture || r.held) off = std::max (0.0, P (L) - L);
            t = r.start + P (u) - off;
        }
    }
    // ---- drawn time curve: y = beats behind
    {
        const Curve& c = timeCurves[curTimeSlot];
        const float depth = warpDepthSm.current;
        if (depth > 0.0f && c.numNodes > 0)
        {
            const double len = std::max (0.25, (double) c.lengthBeats);
            const double ph = floorMod (t, len) / len;
            t -= (double) depth * (double) c.rangeBeats * (double) c.eval (ph);
        }
    }
    return t;
}

double Engine::chopMap (double t, const EngineParams&, bool live)
{
    const LaneRun& r = laneRun (laneChop, t, false);
    if (! r.active) { lastChopSlice = -1; return t; }
    const SliceSet& sl = activeSlices (live);
    if (sl.count <= 0) return t;
    int s;
    if (r.var > 0) s = (r.var - 1) % sl.count;
    else
    {
        if (r.start != chopCacheStart || chopCacheVersion != seqVersion)
        {
            chopCacheStart = r.start; chopCacheVersion = seqVersion;
            // prefer slices that sit on the same part of the beat, so the groove survives
            const double ph = floorMod (r.start, 1.0);
            int cand[kMaxSlices], n = 0;
            for (int i = 0; i < sl.count; ++i)
            {
                const double d = std::fabs (floorMod ((double) sl.startBeat[i], 1.0) - ph);
                if (std::min (d, 1.0 - d) < 0.04) cand[n++] = i;
            }
            const uint32_t h = hash32 (r.rnd + 0x51U);
            chopCacheSlice = (n > 1 && (h & 0xff) < 190) ? cand[(h >> 8) % (uint32_t) n] : (int) ((h >> 8) % (uint32_t) sl.count);
        }
        s = chopCacheSlice;
    }
    lastChopSlice = s;
    const double lb = sl.loopBeats;
    if (! live) return (double) sl.startBeat[s] + (t - r.start);
    double base = std::floor (r.start / lb) * lb + (double) sl.startBeat[s];
    while (base > r.start + 1.0e-9) base -= lb;
    return base + (t - r.start);
}

// ---------------------------------------------------------------- process
void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                      const TransportInfo& transport, const EngineParams& p, const MidiEvent* midi, int numMidi)
{
    updateBlockParams (p);
    const bool live = p.sourceMode == 1;
    bpm = clampv (transport.bpm, 20.0, 999.0);
    spbNow = sr * 60.0 / bpm;
    beatInc = 1.0 / spbNow;
    if (transport.playing) beat = transport.ppq;
    playingNow = transport.playing;

    const bool haveLoop = loop != nullptr && loop->length > 16;
    const SourceView loopView = haveLoop ? loop->view() : SourceView();
    const double spbR = haveLoop ? loop->samplesPerBeat() : spbNow;
    const double loopRate = spbR * beatInc;
    const int fade = std::max (16, (int) (p.declickMs * 0.001 * sr));
    const double q = quantBeats (p.quantize);
    const int postDelay = kReaderLookahead + harmLatency;
    const float harmAmount = clampv (p.harmony, 0.0f, 1.0f);
    const double lookBeats = (double) kReaderLookahead;

    if (harmAmount > 0.0f) harmIdleTime = 0.0f; else harmIdleTime += (float) numSamples;
    harm.setActive (harmIdleTime < (float) (3 * harmLatency));

    int midiIdx = 0;
    float visPeakL = 0.0f, visPeakR = 0.0f, visIn = 0.0f, lastVolGain = 1.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        while (midiIdx < numMidi && midi[midiIdx].offset <= n) handleMidi (midi[midiIdx++], p);

        // The tiny bias makes every "which side of the grid line is this sample on"
        // decision immune to rounding noise in the host position (it is 2e-5 samples).
        const double T = beat + 1.0e-9;
        float xl = inL[n], xr = inR[n];
        if (! (std::fabs (xl) < 1.0e4f)) xl = 0.0f;     // also rejects NaN / Inf, which would poison every filter state
        if (! (std::fabs (xr) < 1.0e4f)) xr = 0.0f;
        ring.write (xl, xr);
        visIn = std::max (visIn, std::max (std::fabs (xl), std::fabs (xr)));

        // ---- quantised slot switching (pre clock)
        if (wantTimeSlot != curTimeSlot || wantSeqSlot != curSeqSlotWarp)
        {
            if (q <= 0.0 || std::floor (T / q) != std::floor ((T - beatInc) / q) || ! playingNow)
            {
                curTimeSlot = wantTimeSlot;
                if (wantSeqSlot != curSeqSlotWarp)
                {
                    curSeqSlotWarp = wantSeqSlot;
                    seqWarp.pattern = &seqs[curSeqSlotWarp]; seqWarp.version = seqVersion * 16U + (uint32_t) curSeqSlotWarp;
                }
            }
        }
        warpDepthSm.next();

        // ---- clocks. The reader runs kReaderLookahead samples behind the host
        // (it needs that much future for band-limited reads of live input); the
        // post stage additionally lags by the harmoniser latency.
        clockDelay[(size_t) clockW] = T; playDelay[(size_t) clockW] = playingNow ? 1 : 0;
        const double Td = clockDelay[(size_t) ((clockW - kReaderLookahead) & clockMask)];
        const bool playingD = playDelay[(size_t) ((clockW - kReaderLookahead) & clockMask)] != 0;
        const int cr = (clockW - postDelay) & clockMask;
        const double Tp = clockDelay[(size_t) cr];
        const bool playingP = playDelay[(size_t) cr] != 0;
        clockW = (clockW + 1) & clockMask;

        // ---- time maps
        float wg = 1.0f;
        const double Tw = warpTime (Td, wg, p, ! live);
        const float wgs = warpGain.process (wg);

        // ---- main voice. Loop and live input have their own readers so that
        // switching source is a plain gain crossfade.
        float sl = 0.0f, sr_ = 0.0f;
        const float lgLive = sourceFade.process (live ? 1.0f : 0.0f);
        const bool wantLoop = playingD && p.loopPlay && haveLoop;
        const SourceView ringView = ring.view();
        const double ringNow = (double) (ring.written() - 1) - lookBeats;
        const bool needPos = wantLoop || loopGain.y > 0.0f || lgLive > 0.0f;
        const double pos = needPos ? chopMap (Tw, p, live) : 0.0;
        if (wantLoop && loopGain.lin <= 0.0f && loopGain.y <= 0.0f)
        {
            float pl, pr;
            readSource (loopView, pos * spbR, 1.0, pl, pr);
            if (std::max (std::fabs (pl), std::fabs (pr)) < 5.0e-4f) loopGain.reset (1.0f);
        }
        const float lgLoop = loopGain.process (wantLoop ? 1.0f : 0.0f) * (1.0f - lgLive);
        if (lgLive > 0.0f)
        {
            float l, r;
            liveReader.process (ringView, ringNow - (Td - pos) * spbNow, 1.0, fade, l, r);
            sl += l * lgLive; sr_ += r * lgLive;
        }
        if (lgLoop > 0.0f && haveLoop)
        {
            float l, r;
            loopReader.process (loopView, pos * spbR, loopRate, fade, l, r);
            sl += l * lgLoop; sr_ += r * lgLoop;
        }
        if (lgLive <= 0.0f && ! liveReader.isSilent()) liveReader.reset();
        if ((lgLoop <= 0.0f || ! haveLoop) && ! loopReader.isSilent()) loopReader.reset();

        // ---- MIDI slice voices
        for (auto& v : voices)
        {
            if (! v.active) continue;
            if (v.fresh) { v.fresh = false; v.warpAtStart = Tw; v.reader.release (fade); }
            const double rel = Tw - v.warpAtStart;
            if (v.endLen > 0.0 && rel >= v.endLen) v.releasing = true;
            const float g = v.gain.process (v.releasing ? 0.0f : v.velocity);
            if (v.releasing && g <= 0.0f) { v.active = false; v.reader.reset(); continue; }
            const double pos = v.srcStart + rel;
            float l, r;
            if (v.live) v.reader.process (ringView, ringNow - (Td - pos) * spbNow, 1.0, fade, l, r);
            else if (haveLoop) v.reader.process (loopView, pos * spbR, loopRate, fade, l, r);
            else { l = r = 0.0f; }
            sl += l * g; sr_ += r * g;
        }
        sl *= wgs; sr_ *= wgs;

        // ---- dry reference: the unglitched source (live input or plain loop playback)
        inLine[0].write (xl); inLine[1].write (xr);
        float dl, dr;
        {
            if (wantLoop && dryGain.lin <= 0.0f && dryGain.y <= 0.0f)
            {
                float pl, pr;
                readSource (loopView, Td * spbR, 1.0, pl, pr);
                if (std::max (std::fabs (pl), std::fabs (pr)) < 5.0e-4f) dryGain.reset (1.0f);
            }
            const float dg = dryGain.process (wantLoop ? 1.0f : 0.0f) * (1.0f - lgLive);
            float l = 0.0f, r = 0.0f;
            if (dg > 0.0f && haveLoop) dryReader.process (loopView, Td * spbR, loopRate, fade, l, r);
            else if (! dryReader.isSilent()) dryReader.reset();
            l = l * dg + inLine[0].tap (kReaderLookahead) * lgLive;
            r = r * dg + inLine[1].tap (kReaderLookahead) * lgLive;
            dl = dryDelay[0].process (l, harmLatency);
            dr = dryDelay[1].process (r, harmLatency);
        }

        // ---- harmony
        float hl = 0.0f, hr = 0.0f;
        {
            float envTarget = harmAmount > 0.0f ? 1.0f : 0.0f;
            int chordVar = 0;
            if (p.harmGate == 1 || held[laneHarmony].down)
            {
                const LaneRun& r = laneRun (laneHarmony, Td, false);
                envTarget = (r.active && harmAmount > 0.0f) ? 1.0f : 0.0f;
                chordVar = r.active ? r.var : 0;
            }
            else
            {
                const LaneRun& r = laneRun (laneHarmony, Td, false);
                chordVar = r.active ? r.var : 0;
            }
            const float he = harmEnv.process (envTarget);
            if ((harmUpdateCounter++ & 63) == 0)
            {
                static const int chordDeg[9] = { 0, 1, 2, 3, 4, -1, -2, -3, -4 };
                static const int chordSemi[9] = { 0, 2, 3, 5, 7, -2, -3, -5, -7 };
                const int cv = clampv (chordVar, 0, 8);
                const int pc = harm.detectedPitchClass();
                for (int i = 0; i < kNumHarmVoices; ++i)
                {
                    Harmonizer::Voice hv;
                    float semis = 0.0f;
                    if (p.voiceOn[i] && harmAmount > 0.0f)
                    {
                        if (p.harmMode == 0)
                        {
                            const int deg = p.voiceInterval[i] + chordDeg[cv];
                            semis = pc >= 0 ? (float) diatonicShift (p.scale, floorModI (pc - p.key, 12), deg)
                                            : (float) degreeToSemis (p.scale, deg);
                        }
                        else semis = (float) (p.voiceInterval[i] + chordSemi[cv]);
                        hv.ratio = (float) semisToRatio ((double) semis + (double) p.voiceDetune[i] * 0.01);
                        const float a = (clampv (p.voicePan[i], -1.0f, 1.0f) + 1.0f) * (float) (kPi * 0.25);
                        const float lvl = p.voiceLevel[i] * harmAmount;
                        hv.gainL = std::cos (a) * lvl * 1.4142f; hv.gainR = std::sin (a) * lvl * 1.4142f;
                    }
                    harm.setVoice (i, hv);
                    visual.harmSemis[i].store (semis, std::memory_order_relaxed);
                    visual.harmLevel[i].store (hv.gainL + hv.gainR > 0.0f ? he : 0.0f, std::memory_order_relaxed);
                }
            }
            harm.process (sl * he, sr_ * he, hl, hr);
        }
        float wl = mainDelay[0].process (sl, harmLatency) + hl;
        float wr = mainDelay[1].process (sr_, harmLatency) + hr;

        // ---- post stage (the audio here is postDelay samples old)
        if (wantSeqSlot != curSeqSlotPost || wantVolSlot != curVolSlot)
        {
            if (q <= 0.0 || std::floor (Tp / q) != std::floor ((Tp - beatInc) / q) || ! playingP)
            {
                curVolSlot = wantVolSlot;
                if (wantSeqSlot != curSeqSlotPost)
                {
                    curSeqSlotPost = wantSeqSlot;
                    seqPost.pattern = &seqs[curSeqSlotPost]; seqPost.version = seqVersion * 16U + (uint32_t) curSeqSlotPost;
                }
            }
        }

        // ---- step effects
        const LaneRun rCrush = laneRun (laneCrush, Tp, true);
        const LaneRun rComb = laneRun (laneComb, Tp, true);
        const LaneRun rRing = laneRun (laneRing, Tp, true);
        const LaneRun rFilter = laneRun (laneFilter, Tp, true);
        const LaneRun rFreeze = laneRun (laneFreeze, Tp, true);
        const LaneRun rDelay = laneRun (laneDelay, Tp, true);
        const LaneRun rReverb = laneRun (laneReverb, Tp, true);
        const float eCrush = laneEnv[laneCrush].process (rCrush.active ? 1.0f : 0.0f);
        const float eComb = laneEnv[laneComb].process (rComb.active ? 1.0f : 0.0f);
        const float eRing = laneEnv[laneRing].process (rRing.active ? 1.0f : 0.0f);
        const float eFilter = laneEnv[laneFilter].process (rFilter.active ? 1.0f : 0.0f);
        const float eFreeze = laneEnv[laneFreeze].process (rFreeze.active ? 1.0f : 0.0f);
        const float eDelay = laneEnv[laneDelay].process (rDelay.active ? 1.0f : 0.0f);
        const float eReverb = laneEnv[laneReverb].process (rReverb.active ? 1.0f : 0.0f);

        const uint32_t activeMask = (rCrush.active ? 1u : 0u) | (rComb.active ? 2u : 0u) | (rRing.active ? 4u : 0u) | (rFilter.active ? 8u : 0u)
                                  | (rDelay.active ? 16u : 0u) | (rReverb.active ? 32u : 0u);
        const bool fxEdge = (activeMask & ~lastActiveMask) != 0;
        lastActiveMask = activeMask;
        if ((fxUpdateCounter++ & 15) == 0 || fxEdge)
        {
            if (rCrush.active)
            {
                static const float bits[5] = { 0, 12.0f, 8.0f, 6.0f, 4.0f }, rate[5] = { 0, 16000.0f, 9000.0f, 5000.0f, 2500.0f };
                const int v = clampv (rCrush.var, 0, 4);
                crusher.set (v ? bits[v] : p.crushBits, v ? rate[v] : p.crushRate);
            }
            if (rComb.active)
            {
                const int deg = rComb.var > 0 ? rComb.var - 1 : p.combDegree;
                comb.set ((float) midiToHz (48.0 + 12.0 * p.combOctave + p.key + degreeToSemis (p.scale, deg)), p.combFeedback, p.combDamp);
            }
            if (rRing.active)
            {
                const int deg = rRing.var > 0 ? rRing.var - 1 : p.ringDegree;
                ring_.set ((float) midiToHz (60.0 + 12.0 * p.ringOctave + p.key + degreeToSemis (p.scale, deg)));
            }
            if (rFilter.active)
                sweep.set (rFilter.var > 0 ? rFilter.var : p.filterShape, (float) ((Tp - rFilter.start) / std::max (1.0e-6, rFilter.length)), p.filterReso);
            {
                const int dv = rDelay.active && rDelay.var > 0 ? rDelay.var - 1 : p.delayDiv;
                if (rDelay.active || ! delayTimeSet) { delayFx.set ((float) (delayDivBeats (dv) * spbNow), p.delayFeedback, p.delayTone, p.delayPingPong); delayTimeSet = true; }
                static const float rt[4] = { 0, 0.8f, 2.0f, 4.5f };
                const int rv = clampv (rReverb.var, 0, 3);
                if (rReverb.active || ! reverbSet) { reverbFx.set (rv ? rt[rv] : p.reverbTime, p.reverbDamp); reverbSet = true; }
            }
        }
        if (rFreeze.active && (! freezeWasOn || rFreeze.start != freezeRunStart))
        {
            static const double fb[5] = { 0, 0.125, 0.25, 0.5, 1.0 };
            const int v = clampv (rFreeze.var, 0, 4);
            const double secs = v ? fb[v] * 60.0 / bpm : (double) p.freezeMs * 0.001;
            freeze.trigger ((float) secs, rFreeze.rnd);
            freezeRunStart = rFreeze.start;
        }
        else if (! rFreeze.active && freezeWasOn) freeze.stop();
        freezeWasOn = rFreeze.active;

        crusher.process (wl, wr, eCrush);
        comb.process (wl, wr, eComb);
        ring_.process (wl, wr, eRing);
        sweep.process (wl, wr, eFilter);
        freeze.process (wl, wr, eFreeze);

        // ---- volume curve
        {
            const Curve& c = volCurves[curVolSlot];
            const double len = std::max (0.25, (double) c.lengthBeats);
            const float v = c.numNodes > 0 ? c.eval (floorMod (Tp, len) / len) : 1.0f;
            const float depth = volDepthSm.next();
            lastVolGain = volSlew.process (1.0f - depth * (1.0f - clampv (v, 0.0f, 1.0f)));
            wl *= lastVolGain; wr *= lastVolGain;
        }

        // ---- throws (after the gate so tails ring through it)
        {
            float tl, tr;
            delayFx.process (wl, wr, eDelay, tl, tr);
            const float dlv = delayLevelSm.next();
            float rl, rr;
            reverbFx.process (wl, wr, eReverb, rl, rr);
            const float rlv = reverbLevelSm.next();
            wl += tl * dlv + rl * rlv; wr += tr * dlv + rr * rlv;
        }

        // ---- output section. The filters always run (their state stays warm) and
        // are blended in, so switching them on or sweeping them cannot click.
        {
            if ((outUpdateCounter++ & 15) == 0)
            {
                hpSm += 0.12f * (hpTarget - hpSm); lpSm += 0.12f * (lpTarget - lpSm);
                qSm += 0.12f * (qTarget - qSm); tiltSm += 0.12f * (tiltTarget - tiltSm);
                for (int c = 0; c < 2; ++c) { outHp[c].set (sr, hpSm, qSm); outLp[c].set (sr, lpSm, qSm); }
                tilt.set (sr, tiltSm);
            }
            const float hm = hpMix.process (hpTarget > 21.0f ? 1.0f : 0.0f), lm = lpMix.process (lpTarget < 19900.0f ? 1.0f : 0.0f);
            float lp, bp, hp;
            outHp[0].process (wl, lp, bp, hp); wl += hm * (hp - wl);
            outHp[1].process (wr, lp, bp, hp); wr += hm * (hp - wr);
            outLp[0].process (wl, lp, bp, hp); wl += lm * (lp - wl);
            outLp[1].process (wr, lp, bp, hp); wr += lm * (lp - wr);
            tilt.process (wl, wr);
            const float w = widthSm.next();
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * w;
            wl = mid + side; wr = mid - side;
        }
        const float mix = mixSm.next();
        const float gOut = gainSm.next();
        float ol = (dl + mix * (wl - dl)) * gOut;
        float orr = (dr + mix * (wr - dr)) * gOut;
        if (p.inputMonitor && ! live) { ol += inLine[0].tap (postDelay); orr += inLine[1].tap (postDelay); }

        // limiter (always in the path so latency is constant)
        {
            const float lim = limiterSm.next();
            limiter.setCeiling (lim > 0.5f ? 0.966f : 64.0f);
            limiter.process (ol, orr);
        }

        // ---- bypass crossfade against the latency-matched input
        const float by = bypassFade.process (p.bypass ? 1.0f : 0.0f);
        if (by > 0.0f) { ol += by * (inLine[0].tap (latency) - ol); orr += by * (inLine[1].tap (latency) - orr); }

        outL[n] = ol; outR[n] = orr;
        visPeakL = std::max (visPeakL, std::fabs (ol)); visPeakR = std::max (visPeakR, std::fabs (orr));

        beat += beatInc;
    }

    // ---- publish visual state
    visual.ringWritten.store (-1, std::memory_order_relaxed);   // marks the pair below as mid-update
    visual.beat.store (beat, std::memory_order_relaxed);
    visual.ringWritten.store (ring.written(), std::memory_order_release);
    visual.bpm.store (bpm, std::memory_order_relaxed);
    visual.playing.store (playingNow, std::memory_order_relaxed);
    {
        const SliceSet& slc = activeSlices (live);
        const double lb = live ? (double) slc.loopBeats : (haveLoop ? loop->beats : 4.0);
        if (live)
            visual.loopPos.store ((float) (((double) (ring.written() - 1) - liveReader.currentPosition()) / spbNow), std::memory_order_relaxed);
        else
            visual.loopPos.store ((float) floorMod (loopReader.currentPosition() / spbR, lb), std::memory_order_relaxed);
        visual.loopGain.store (live ? sourceFade.y : loopGain.y, std::memory_order_relaxed);
        visual.playRate.store ((float) (live ? liveReader.currentRate() : loopReader.currentRate() / std::max (1.0e-9, loopRate)), std::memory_order_relaxed);
        for (int i = 0; i < kMaxSliceVoices; ++i)
        {
            const auto& v = voices[i];
            float pos = -1.0f;
            if (v.active && ! v.releasing)
                pos = v.live ? (float) (((double) (ring.written() - 1) - v.reader.currentPosition()) / spbNow)
                           : (float) floorMod (v.reader.currentPosition() / spbR, lb);
            visual.voicePos[i].store (pos, std::memory_order_relaxed);
        }
    }
    {
        const double T = beat;
        float wgDummy = 1.0f; (void) wgDummy;
        for (int l = 0; l < kNumLanes; ++l)
        {
            const bool warpLane = l <= lanePitch || l == laneHarmony;
            const LaneRun& r = laneRun (l, warpLane ? T : T - (double) postDelay * beatInc, ! warpLane);
            visual.laneLevel[l].store (r.active ? 1.0f : 0.0f, std::memory_order_relaxed);
            visual.laneVar[l].store (l == laneChop && r.active ? lastChopSlice + 1 : r.var, std::memory_order_relaxed);
        }
        const Curve& c = timeCurves[curTimeSlot];
        const double len = std::max (0.25, (double) c.lengthBeats);
        visual.timeOffsetBeats.store (warpDepthSm.current * c.rangeBeats * c.eval (floorMod (T, len) / len), std::memory_order_relaxed);
    }
    visual.volGain.store (lastVolGain, std::memory_order_relaxed);
    visual.timeSlot.store (curTimeSlot, std::memory_order_relaxed);
    visual.volSlot.store (curVolSlot, std::memory_order_relaxed);
    visual.seqSlot.store (curSeqSlotPost, std::memory_order_relaxed);
    visual.pendingTimeSlot.store (wantTimeSlot != curTimeSlot ? wantTimeSlot : -1, std::memory_order_relaxed);
    visual.pendingVolSlot.store (wantVolSlot != curVolSlot ? wantVolSlot : -1, std::memory_order_relaxed);
    visual.pendingSeqSlot.store (wantSeqSlot != curSeqSlotPost ? wantSeqSlot : -1, std::memory_order_relaxed);
    visual.pitchClass.store (harm.isActive() ? harm.detectedPitchClass() : -1, std::memory_order_relaxed);
    visual.peakL.store (std::max (visPeakL, visual.peakL.load (std::memory_order_relaxed) * 0.0f), std::memory_order_relaxed);
    visual.peakR.store (visPeakR, std::memory_order_relaxed);
    visual.inPeak.store (visIn, std::memory_order_relaxed);
    visual.gainReduction.store (limiter.gainReduction(), std::memory_order_relaxed);
}
} // namespace splinter
