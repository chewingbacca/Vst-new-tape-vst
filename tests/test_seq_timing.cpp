#include "TestFramework.h"
#include "TestHost.h"
using namespace atm;

static int expectedSample (double beat, double bpm, double sr) { return (int) std::ceil (beat * 60.0 / bpm * sr - 1.0e-6); }

TEST (timing_quarter_notes_sample_exact)
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (double bpm : { 87.0, 120.0, 171.3 })
        {
            TestHost h (sr, bpm);
            Project p = makeClickProject();
            for (int i = 0; i < 16; i += 4) p.patterns[0].tracks[0].steps[i].on = 1;
            h.setProject (p);
            h.render ((int) h.beatToSample (8.0), 0);
            const auto on = findOnsets (h.L, 1.0e-6f, (int) (sr * 0.02));
            CHECK_MSG (on.size() == 8, "got %d onsets at sr %g bpm %g", (int) on.size(), sr, bpm);
            for (size_t i = 0; i < on.size() && i < 8; ++i)
            {
                const int e = expectedSample ((double) i, bpm, sr);
                // the voice's first output sample is at the event sample (oscillator starts at phase 0 -> first non-zero one sample later)
                CHECK_MSG (on[i] >= e && on[i] <= e + 1, "onset %d at %d expected %d (sr %g bpm %g)", (int) i, on[i], e, sr, bpm);
            }
        }
}

TEST (timing_block_size_independent)
{
    Project p = makeClickProject();
    TrackPattern& tp = p.patterns[0].tracks[0];
    for (int i = 0; i < 16; i += 2) tp.steps[i].on = 1;
    tp.steps[6].ratCount = 4; tp.steps[6].ratCurve = RatCurve::Accel;
    tp.steps[10].micro = 0.31f; tp.steps[14].micro = -0.2f;
    std::vector<float> ref;
    for (int block : { 64, 1, 7, 512, 4096, 0 })
    {
        TestHost h (48000.0, 133.0);
        h.setProject (p);
        h.render (48000 * 2, block);
        if (ref.empty()) { ref = h.L; continue; }
        double maxDiff = 0;
        for (size_t i = 0; i < ref.size(); ++i) maxDiff = std::max (maxDiff, (double) std::fabs (ref[i] - h.L[i]));
        size_t firstDiff = 0;
        for (size_t i = 0; i < ref.size(); ++i) if (std::fabs (ref[i] - h.L[i]) > 1.0e-6) { firstDiff = i; break; }
        CHECK_MSG (maxDiff < 1.0e-6, "block %d differs from reference by %g, first at sample %d", block, maxDiff, (int) firstDiff);
    }
}

TEST (timing_ratchets_and_microtiming)
{
    const double sr = 48000.0, bpm = 120.0;
    TestHost h (sr, bpm);
    Project p = makeClickProject();
    TrackPattern& tp = p.patterns[0].tracks[0];
    tp.steps[0].on = 1; tp.steps[0].ratCount = 4;                       // even: 0, 1/16, 2/16, 3/16 beats
    tp.steps[4].on = 1; tp.steps[4].micro = 0.25f;                      // +1/4 step late
    tp.steps[8].on = 1; tp.steps[8].micro = -0.5f;                      // half a step early
    tp.steps[12].on = 1; tp.steps[12].ratCount = 3; tp.steps[12].ratCurve = RatCurve::Decel; tp.steps[12].ratAmt = 0.5f;
    for (auto& s : tp.steps) { s.ratGate = 0.3f; s.len = 0.2f; }
    h.setProject (p);
    h.render ((int) h.beatToSample (4.0), 128);
    const auto on = findOnsets (h.L, 1.0e-6f, 64);
    std::vector<double> beats = { 0.0, 0.0625, 0.125, 0.1875, 1.0 + 0.0625, 2.0 - 0.125 };
    for (int i = 0; i < 3; ++i) beats.push_back (3.0 + 0.25 * SeqEngine::ratchetPos (i, 3, RatCurve::Decel, 0.5f));
    CHECK_MSG (on.size() == beats.size(), "got %d onsets, expected %d", (int) on.size(), (int) beats.size());
    for (size_t i = 0; i < on.size() && i < beats.size(); ++i)
    {
        const int e = expectedSample (beats[i], bpm, sr);
        CHECK_MSG (on[i] >= e && on[i] <= e + 1, "onset %d at %d expected %d", (int) i, on[i], e);
    }
}

TEST (timing_swing_and_polymeter)
{
    const double sr = 48000.0, bpm = 100.0;
    TestHost h (sr, bpm);
    Project p = makeClickProject();
    TrackPattern& tp = p.patterns[0].tracks[0];
    tp.length = 3; tp.freeRun = 1; tp.swing = 0.66f;                    // 3-step loop against 4/4, heavy swing
    tp.steps[0].on = 1; tp.steps[1].on = 1;
    for (auto& s : tp.steps) s.len = 0.2f;
    h.setProject (p);
    h.render ((int) h.beatToSample (3.0), 333);
    const auto on = findOnsets (h.L, 1.0e-6f, 64);
    std::vector<double> beats;
    for (int abs = 0; abs < 12; ++abs)
    {
        const int idx = abs % 3;
        if (idx == 2) continue;
        double t = abs * 0.25;
        if (abs & 1) t += (0.66 - 0.5) * 2.0 * 0.25;
        beats.push_back (t);
    }
    CHECK_MSG (on.size() == beats.size(), "got %d onsets, expected %d", (int) on.size(), (int) beats.size());
    for (size_t i = 0; i < on.size() && i < beats.size(); ++i)
    {
        const int e = expectedSample (beats[i], bpm, sr);
        CHECK_MSG (on[i] >= e && on[i] <= e + 1, "onset %d at %d expected %d", (int) i, on[i], e);
    }
}

TEST (timing_host_loop_and_jump)
{
    // Play bar 1, jump back to 0 (a host loop), play again: the same events must appear both times.
    const double sr = 48000.0, bpm = 140.0;
    TestHost h (sr, bpm);
    Project p = makeClickProject();
    TrackPattern& tp = p.patterns[0].tracks[0];
    for (int i : { 0, 3, 6, 10, 13 }) tp.steps[i].on = 1;
    tp.steps[3].prob = 50; tp.steps[10].prob = 50;       // probability is tied to position, so it must repeat too
    h.setProject (p);
    const int bar = (int) h.beatToSample (4.0);
    h.render (bar, 256);
    h.pos = 0;
    h.render (bar, 256);
    const auto on = findOnsets (h.L, 1.0e-6f, 64);
    CHECK (on.size() >= 6 && on.size() % 2 == 0);
    const size_t half = on.size() / 2;
    for (size_t i = 0; i < half; ++i) CHECK_MSG (on[i + half] - on[i] == bar, "event %d: %d vs %d", (int) i, on[i], on[i + half]);
}

TEST (timing_tempo_change)
{
    // Tempo doubles at beat 2: events still fall on their beats.
    const double sr = 48000.0;
    TestHost h (sr, 120.0);
    Project p = makeClickProject();
    for (int i = 0; i < 16; i += 4) p.patterns[0].tracks[0].steps[i].on = 1;
    h.setProject (p);
    const int first = (int) h.beatToSample (2.0);       // 48000 samples
    h.render (first, 100);
    // continue at 240 bpm from beat 2
    h.bpm = 240.0; h.pos = (int64_t) std::llround (2.0 * 60.0 / 240.0 * sr);
    h.render ((int) (2.0 * 60.0 / 240.0 * sr), 100);
    const auto on = findOnsets (h.L, 1.0e-6f, 64);
    CHECK_MSG (on.size() == 4, "got %d onsets", (int) on.size());
    if (on.size() == 4)
    {
        CHECK (on[0] <= 1); CHECK (std::abs (on[1] - 24000) <= 1);
        CHECK (std::abs (on[2] - 48000) <= 1); CHECK (std::abs (on[3] - 60000) <= 1);
    }
}
