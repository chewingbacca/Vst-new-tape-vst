#include "OfflineRender.h"
#include <memory>

namespace atm {

void renderOffline (const Project& project, const SampleData* const* samples, const OfflineOptions& o,
                    std::vector<float>& left, std::vector<float>& right)
{
    auto eng = std::make_unique<AudioEngine>();
    const int block = 256;
    eng->prepare (o.sampleRate, block);

    auto* p = new Project (project);
    p->song.songMode = p->song.songMode;     // keep as is: exports follow what would play
    p->evolveOn = 0;                          // exports are reproducible snapshots
    if (o.soloTrack >= 0)
        for (int t = 0; t < kTracks; ++t) { p->kit.tracks[t].solo = (uint8_t) (t == o.soloTrack); p->kit.tracks[t].mute = 0; }
    Cmd c; c.type = Cmd::SetProject; c.p = p; eng->post (c);
    for (int s = 0; s < kNumFxSlots; ++s)
    {
        auto fx = createEffect (getFxType (project.kit, s));
        if (fx) { fx->prepare (o.sampleRate); fx->reset(); }
        Cmd f; f.type = Cmd::SetFx; f.a = (int16_t) s; f.p = fx.release(); eng->post (f);
    }
    for (int t = 0; t < kTracks; ++t)
    {
        Cmd sm; sm.type = Cmd::SetSample; sm.a = (int16_t) t; sm.p = const_cast<SampleData*> (samples ? samples[t] : nullptr);
        eng->post (sm);
    }
    eng->drainCommandsOffline();

    const double spb = o.sampleRate * 60.0 / o.bpm;
    const int64_t playSamples = (int64_t) std::llround (o.bars * 4.0 * spb);
    const int64_t total = playSamples + (int64_t) (o.tailSeconds * o.sampleRate);
    left.assign ((size_t) total, 0.f); right.assign ((size_t) total, 0.f);
    double beat = o.startBar * 4.0;
    for (int64_t done = 0; done < total;)
    {
        const bool playing = done < playSamples;
        int n = (int) std::min<int64_t> (block, total - done);
        if (playing) n = (int) std::min<int64_t> (n, playSamples - done);
        ProcessIO io;
        io.numSamples = n;
        io.main[0] = left.data() + done; io.main[1] = right.data() + done;
        io.transport.valid = true; io.transport.playing = playing; io.transport.bpm = o.bpm;
        io.transport.ppq = beat; io.transport.barStartPpq = std::floor (beat / 4.0) * 4.0;
        eng->process (io);
        if (playing) beat += (double) n / spb;
        done += n;
    }
    if (o.trimTail)
    {
        size_t end = left.size();
        const size_t minEnd = (size_t) playSamples;
        while (end > minEnd && std::fabs (left[end - 1]) < 1.0e-4f && std::fabs (right[end - 1]) < 1.0e-4f) --end;
        end = std::min (left.size(), end + (size_t) (0.01 * o.sampleRate));
        left.resize (end); right.resize (end);
    }
    // free what the engine handed back (it frees its own queues in its destructor; samples are not ours)
    Garbage g;
    while (eng->popGarbage (g))
    {
        switch (g.kind)
        {
            case Garbage::KProject: delete static_cast<Project*> (g.p); break;
            case Garbage::KEffect:  delete static_cast<Effect*> (g.p); break;
            default: break;
        }
    }
}

void collectPatternNotes (const Project& project, int pattern, double bars, std::vector<ExportNote>& out)
{
    out.clear();
    pattern = clamp (pattern, 0, kPatterns - 1);
    const Pattern& pat = project.patterns[pattern];
    ScaleTable scale; scale.build (project.kit.tuning);
    SeqFrame f;
    f.pattern = &pat; f.kit = &project.kit; f.seed = project.seed; f.origin = 0.0;
    f.cycleBeats = (double) pat.lengthBars * 4.0;
    f.globalSwing = (double) globalParamDesc (GP::Swing, nullptr).toPhys (project.kit.gparams[GP::Swing]);
    SeqMacros m;
    m.density = project.kit.gparams[GP::Density] * 2.f - 1.f;
    m.complexity = project.kit.gparams[GP::Complexity];
    m.mutation = project.kit.gparams[GP::Mutation];
    m.chaos = project.kit.gparams[GP::Chaos];
    const double end = bars * 4.0;
    for (int t = 0; t < kTracks; ++t)
    {
        const TrackPattern& tp = pat.tracks[t];
        const TrackSound& ts = project.kit.tracks[t];
        const double d = tp.stepBeats();
        const uint32_t hitSeed = hashN (f.seed, tp.seed, (uint32_t) t);
        SeqEngine::forEachStepInRange (f, tp, -SeqEngine::kMaxLateSteps * d, end + d, [&] (int64_t cycle, int64_t abs)
        {
            StepEval se;
            SeqEngine::evalStep (f, m, t, cycle, abs, 0.f, se);
            if (! se.trig) return;
            SeqHit hits[kMaxRatchets];
            const int n = SeqEngine::expandHits (se, hitSeed, hits);
            for (int i = 0; i < n; ++i)
            {
                if (hits[i].beat < 0.0 || hits[i].beat >= end) continue;
                ExportNote e;
                e.beat = hits[i].beat; e.lengthBeats = hits[i].gateBeats; e.track = t; e.velocity = hits[i].velocity;
                e.note = scale.pitchToNote (ts.baseNote, hits[i].pitch, ts.scaleOn != 0);
                out.push_back (e);
            }
        }, 100000);
    }
}

} // namespace atm
