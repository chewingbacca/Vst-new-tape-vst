// Splinter — DSP unit tests (JUCE-free).
#include "TestUtil.h"
#include "dsp/DemoLoops.h"
#include "dsp/Engine.h"
#include "dsp/Stretch.h"

using namespace splinter;
using namespace testutil;

namespace
{
struct Rig
{
    Engine engine;
    EngineParams params;
    TransportInfo transport;
    double sr = 48000.0;
    std::vector<float> outL, outR;
    std::vector<MidiEvent> midi;   // offsets are absolute sample positions
    int64_t rendered = 0;
    RenderedLoop loop;

    explicit Rig (double sampleRate = 48000.0, double bpm = 120.0)
    {
        sr = sampleRate; transport.bpm = bpm; transport.playing = true; transport.ppq = 0.0;
        engine.prepare (sr, 512);
        params.mix = 1.0f; params.chop = params.stutter = params.warp = params.crush = 0.5f;
    }
    // a loop that is already at the engine tempo, filled by fn(sampleIndex, beat)
    void makeLoop (double beats, const std::function<void (int64_t, double, float&, float&)>& fn)
    {
        const int64_t n = (int64_t) std::llround (beats * 60.0 / transport.bpm * sr);
        loop.allocate (n); loop.beats = beats; loop.bpm = transport.bpm; loop.sampleRate = sr;
        for (int64_t i = 0; i < n; ++i) fn (i, (double) i * beats / (double) n, loop.channel (0)[i], loop.channel (1)[i]);
        loop.fixGuards();
        engine.setLoop (&loop);
        SliceSet s; s.makeGrid ((float) beats, 4); engine.setSlices (s);
    }
    void sineLoop (double beats, double hz, float amp = 0.5f)
    {
        // choose a frequency that is periodic over the loop so wrap is seamless
        const double dur = beats * 60.0 / transport.bpm;
        const double f = std::round (hz * dur) / dur;
        makeLoop (beats, [=, this] (int64_t i, double, float& l, float& r) { l = r = amp * (float) std::sin (kTwoPi * f * (double) i / sr); });
    }
    void render (int64_t numSamples, const std::function<void (int64_t, float&, float&)>& input = nullptr, int blockSize = 0)
    {
        std::vector<float> il, ir, ol, orr;
        Rng rng (99);
        int64_t done = 0;
        while (done < numSamples)
        {
            int n = blockSize > 0 ? blockSize : 1 + rng.range (700);
            n = (int) std::min<int64_t> (n, numSamples - done);
            il.assign ((size_t) n, 0.0f); ir.assign ((size_t) n, 0.0f); ol.assign ((size_t) n, 0.0f); orr.assign ((size_t) n, 0.0f);
            if (input) for (int i = 0; i < n; ++i) input (rendered + i, il[(size_t) i], ir[(size_t) i]);
            std::vector<MidiEvent> ev;
            for (auto& m : midi) if (m.offset >= rendered && m.offset < rendered + n) { MidiEvent e = m; e.offset = (int) (m.offset - rendered); ev.push_back (e); }
            engine.process (il.data(), ir.data(), ol.data(), orr.data(), n, transport, params, ev.data(), (int) ev.size());
            outL.insert (outL.end(), ol.begin(), ol.end()); outR.insert (outR.end(), orr.begin(), orr.end());
            if (transport.playing) transport.ppq += (double) n * transport.bpm / (60.0 * sr);
            rendered += n; done += n;
        }
    }
    double spb() const { return sr * 60.0 / transport.bpm; }
};

SeqPattern emptyPattern() { SeqPattern p; p.clear(); p.numSteps = 16; p.stepsPerBeat = 4; return p; }
void setStep (SeqPattern& p, int lane, int step, int var = 0, int prob = 100) { p.steps[lane][step].on = 1; p.steps[lane][step].var = (uint8_t) var; p.steps[lane][step].prob = (uint8_t) prob; }
} // namespace

// ================================================================ foundations
TEST_CASE (fft_roundtrip)
{
    FFT fft (10);
    std::vector<float> re (1024), im (1024, 0.0f), orig;
    Rng rng (5);
    for (auto& v : re) v = rng.bipolar();
    orig = re;
    fft.transform (re.data(), im.data(), false);
    fft.transform (re.data(), im.data(), true);
    double err = 0.0;
    for (int i = 0; i < 1024; ++i) err = std::max (err, (double) std::fabs (re[(size_t) i] / 1024.0f - orig[(size_t) i]));
    CHECK_MSG (err < 1.0e-4, "err %g", err);
    // single bin
    for (int i = 0; i < 1024; ++i) { re[(size_t) i] = (float) std::cos (kTwoPi * 37.0 * i / 1024.0); im[(size_t) i] = 0.0f; }
    fft.transform (re.data(), im.data(), false);
    CHECK (std::fabs (re[37] - 512.0f) < 0.5f);
    CHECK (std::fabs (re[36]) < 0.5f);
}

TEST_CASE (scales)
{
    CHECK (degreeToSemis (0, 2) == 4);     // major third
    CHECK (degreeToSemis (1, 2) == 3);     // minor third
    CHECK (degreeToSemis (0, 7) == 12);
    CHECK (degreeToSemis (0, -1) == -1);
    CHECK (degreeToSemis (1, -2) == -4);
    CHECK (diatonicShift (0, 0, 2) == 4);  // C -> E in C major
    CHECK (diatonicShift (0, 2, 2) == 3);  // D -> F
    CHECK (diatonicShift (0, 4, 2) == 3);  // E -> G
    CHECK (diatonicShift (0, 11, 2) == 3); // B -> D
    CHECK (diatonicShift (0, 1, 0) == -1 || diatonicShift (0, 1, 0) == 1); // C# pulled into scale
    for (int s = 0; s < kNumScales; ++s)
        for (int pc = 0; pc < 12; ++pc)
            for (int d = -7; d <= 7; ++d)
            {
                const int target = floorModI (pc + diatonicShift (s, pc, d), 12);
                bool inScale = false;
                for (int i = 0; i < scaleDef (s).count; ++i) if (scaleDef (s).steps[i] == target) inScale = true;
                CHECK (inScale);
            }
}

TEST_CASE (curve_eval)
{
    Curve c; c.clear();
    c.add (0.0f, 0.0f); c.add (0.5f, 1.0f); c.add (0.5f, 0.25f); c.add (1.0f, 0.25f);
    CHECK (std::fabs (c.eval (0.25) - 0.5f) < 1.0e-5f);
    CHECK (std::fabs (c.eval (0.4999) - 1.0f) < 1.0e-2f);
    CHECK (std::fabs (c.eval (0.5) - 0.25f) < 1.0e-6f);
    CHECK (std::fabs (c.eval (0.9) - 0.25f) < 1.0e-6f);
    CHECK (std::fabs (c.eval (0.1) - 0.2f) < 1.0e-5f); // backwards query with a stale hint
    Curve h; h.clear(); h.add (0.0f, 0.2f, 0.0f, true); h.add (0.5f, 0.8f);
    CHECK (std::fabs (h.eval (0.3) - 0.2f) < 1.0e-6f);
    CHECK (std::fabs (h.eval (0.7) - 0.8f) < 1.0e-6f);
}

TEST_CASE (sequencer_is_deterministic_and_merges_runs)
{
    SeqPattern p = emptyPattern();
    setStep (p, laneStutter, 4, 4); setStep (p, laneStutter, 5, 4); setStep (p, laneStutter, 6, 2);
    setStep (p, laneReverse, 10, 0, 50);
    SeqSettings s; s.pattern = &p; s.seed = 7; s.version = 1;
    LaneEvaluator ev;
    const LaneRun& r = ev.query (s, laneStutter, 1.30); // step 5
    CHECK (r.active); CHECK (std::fabs (r.start - 1.0) < 1.0e-9); CHECK (std::fabs (r.length - 0.5) < 1.0e-9); CHECK (r.var == 4);
    const LaneRun r2 = ev.query (s, laneStutter, 1.55);
    CHECK (r2.active && std::fabs (r2.start - 1.5) < 1.0e-9 && std::fabs (r2.length - 0.25) < 1.0e-9);
    CHECK (! ev.query (s, laneStutter, 0.1).active);
    // probability: about half of the bars fire, and the same seed repeats exactly
    int fired = 0; std::vector<int> a, b;
    for (int bar = 0; bar < 400; ++bar) { const bool on = evalStep (s, laneReverse, bar * 16 + 10).on; fired += on; a.push_back (on); }
    for (int bar = 0; bar < 400; ++bar) b.push_back (evalStep (s, laneReverse, bar * 16 + 10).on);
    CHECK (a == b);
    CHECK_MSG (fired > 150 && fired < 250, "fired %d / 400", fired);
    s.seed = 8; int diff = 0;
    for (int bar = 0; bar < 400; ++bar) diff += (int) (evalStep (s, laneReverse, bar * 16 + 10).on != (bool) a[(size_t) bar]);
    CHECK (diff > 100);
    // chaos never touches a locked lane; injection respects the downbeat rule for chop
    s.chaos = 1.0f; p.locked[laneStutter] = 1;
    for (int st = 0; st < 64; ++st)
    {
        const StepState x = evalStep (s, laneStutter, st);
        const int idx = st % 16;
        CHECK (x.on == (idx == 4 || idx == 5 || idx == 6));
    }
    s.inject[laneChop] = 1.0f;
    for (int bar = 0; bar < 64; ++bar) CHECK (! evalStep (s, laneChop, bar * 16).on || p.steps[laneChop][0].on);
}

// ================================================================ reader
TEST_CASE (reader_is_transparent_at_unity)
{
    RenderedLoop loop; loop.allocate (4800);
    Rng rng (3);
    for (int i = 0; i < 4800; ++i) loop.channel (0)[i] = loop.channel (1)[i] = rng.bipolar();
    loop.fixGuards();
    Reader rd; float l, r; double maxErr = 0.0;
    for (int i = 0; i < 20000; ++i)
    {
        rd.process (loop.view(), (double) i, 1.0, 64, l, r);
        if (i > 200) maxErr = std::max (maxErr, (double) std::fabs (l - loop.channel (0)[i % 4800]));
    }
    CHECK_MSG (maxErr < 1.0e-6, "maxErr %g", maxErr);
}

TEST_CASE (reader_crossfades_jumps_without_clicks)
{
    const double sr = 48000.0;
    RenderedLoop loop; loop.allocate (48000);
    for (int i = 0; i < 48000; ++i) loop.channel (0)[i] = loop.channel (1)[i] = 0.5f * (float) std::sin (kTwoPi * 220.0 * i / sr);
    loop.fixGuards();
    Reader rd; std::vector<float> out; float l, r;
    Rng rng (11); double pos = 0.0;
    for (int i = 0; i < 96000; ++i)
    {
        if (i % 3000 == 2999) pos = (double) rng.range (40000);   // arbitrary jumps, random phase
        rd.process (loop.view(), pos, 1.0, 192, l, r); pos += 1.0;
        out.push_back (l);
    }
    const ClickResult c = detectClicks (out, sr, 1000);
    CHECK_MSG (c.worstRatio < 6.0, "click ratio %.2f at %zu", c.worstRatio, c.position);
    CHECK (peakAbs (out) < 0.75f);
    // sanity: the detector does fire on a hard cut
    std::vector<float> hard;
    for (int i = 0; i < 20000; ++i) hard.push_back (0.5f * (float) std::sin (kTwoPi * 220.0 * (i + (i > 10000 ? 57 : 0)) / sr));
    CHECK (detectClicks (hard, sr).worstRatio > 20.0);
}

TEST_CASE (reader_gains_sum_to_one_under_rapid_jumps)
{
    // a constant source: whatever the reader does, the output must stay at that constant
    RenderedLoop loop; loop.allocate (48000);
    for (int i = 0; i < 48000; ++i) loop.channel (0)[i] = loop.channel (1)[i] = 0.5f;
    loop.fixGuards();
    Reader rd; float l, r; Rng rng (21); double pos = 0.0; float lo = 1.0f, hi = 0.0f;
    for (int i = 0; i < 200000; ++i)
    {
        if (rng.range (60) == 0) pos = (double) rng.range (40000);      // jumps far more often than the 192-sample fade
        rd.process (loop.view(), pos, 1.0, 192, l, r); pos += 1.0;
        lo = std::min (lo, l); hi = std::max (hi, l);
    }
    CHECK_MSG (lo > 0.4999f && hi < 0.5001f, "gain sum range %.5f .. %.5f", lo * 2.0f, hi * 2.0f);
    rd.release (192);
    for (int i = 0; i < 400; ++i) rd.process (loop.view(), pos, 1.0, 192, l, r);
}

TEST_CASE (reader_fast_playback_does_not_alias)
{
    const double sr = 48000.0;
    RenderedLoop loop; loop.allocate (48000);
    // 15 kHz tone: at 2x speed it would land at 30 kHz -> must be filtered out, not aliased to 18 kHz
    for (int i = 0; i < 48000; ++i) loop.channel (0)[i] = loop.channel (1)[i] = 0.5f * (float) std::sin (kTwoPi * 15000.0 * i / sr);
    loop.fixGuards();
    Reader rd; std::vector<float> out; float l, r;
    for (int i = 0; i < 24000; ++i) { rd.process (loop.view(), 2.0 * i, 2.0, 64, l, r); out.push_back (l); }
    CHECK_MSG (rms (out, 2000) < 0.01, "alias rms %g", rms (out, 2000));
    // and a 5 kHz tone doubles to 10 kHz at nearly full level
    for (int i = 0; i < 48000; ++i) loop.channel (0)[i] = loop.channel (1)[i] = 0.5f * (float) std::sin (kTwoPi * 5000.0 * i / sr);
    loop.fixGuards(); rd.reset(); out.clear();
    for (int i = 0; i < 24000; ++i) { rd.process (loop.view(), 2.0 * i, 2.0, 64, l, r); out.push_back (l); }
    CHECK_MSG (rms (out, 2000) > 0.3, "rms %g", rms (out, 2000));
    CHECK (std::fabs (centsBetween (dominantFrequency (out, sr, 4000, 8192, 30.0, 16000.0), 10000.0)) < 3.0);
}

// ================================================================ engine basics
TEST_CASE (live_passthrough_is_exact_and_latency_matches)
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        Rig rig (sr, 128.0);
        rig.params.sourceMode = 1; rig.params.limiter = false;
        Rng rng (4);
        std::vector<float> in;
        const int n = (int) sr;
        for (int i = 0; i < n; ++i) in.push_back (0.4f * rng.bipolar());
        rig.render (n, [&] (int64_t i, float& l, float& r) { l = r = in[(size_t) i]; });
        const int lat = rig.engine.latencySamples();
        double maxErr = 0.0;
        for (int i = lat + 2000; i < n; ++i) maxErr = std::max (maxErr, (double) std::fabs (rig.outL[(size_t) i] - in[(size_t) (i - lat)]));
        CHECK_MSG (maxErr < 2.0e-5, "sr %.0f latency %d maxErr %g", sr, lat, maxErr);
        CHECK (peakAbs (rig.outL, 0, 64) < 1.0e-6f);
    }
}

TEST_CASE (loop_playback_is_sample_locked_to_the_host)
{
    Rig rig (48000.0, 137.0);
    rig.params.limiter = false;
    // an impulse on every beat of a 4 beat loop
    const double spb = rig.spb();
    rig.makeLoop (4.0, [&] (int64_t i, double, float& l, float& r)
    {
        l = r = 0.0f;
        for (int b = 0; b < 4; ++b) if (i == (int64_t) std::llround (b * spb)) l = r = 0.8f;
    });
    rig.render (48000 * 6);
    const int lat = rig.engine.latencySamples();
    // every beat for 6 seconds must appear exactly lat samples after its ideal time
    int found = 0; double worst = 0.0;
    for (int b = 1; b < 13; ++b)
    {
        const double ideal = b * spb + lat;
        const int centre = (int) std::llround (ideal);
        int best = centre - 50; float bv = 0.0f;
        for (int i = centre - 50; i <= centre + 50; ++i) if (std::fabs (rig.outL[(size_t) i]) > bv) { bv = std::fabs (rig.outL[(size_t) i]); best = i; }
        if (bv > 0.3f) ++found;
        worst = std::max (worst, std::fabs ((double) best - ideal));
    }
    CHECK (found == 12);
    CHECK_MSG (worst <= 1.0, "worst drift %.3f samples", worst);
}

TEST_CASE (transport_start_keeps_the_first_transient)
{
    // a loop that opens with an impulse-like hit on the one: pressing play must
    // reproduce it at full level on the exact sample
    Rig rig (48000.0, 120.0);
    rig.params.limiter = false;
    rig.makeLoop (4.0, [] (int64_t i, double, float& l, float& r) { l = r = (i >= 2 && i < 40) ? 0.8f * (float) std::exp (-(double) (i - 2) / 8.0) : 0.0f; });
    rig.transport.playing = false; rig.render (20000);
    rig.transport.playing = true; rig.transport.ppq = 0.0; rig.render (20000);
    const int at = 20000 + rig.engine.latencySamples() + 2;
    CHECK_MSG (std::fabs (rig.outL[(size_t) at] - 0.8f) < 0.01f, "first sample of the hit: %.4f", rig.outL[(size_t) at]);
    CHECK (std::fabs (rig.outL[(size_t) at - 1]) < 1.0e-4f);
}

TEST_CASE (host_loop_and_seek_do_not_click)
{
    Rig rig (48000.0, 120.0);
    rig.sineLoop (4.0, 330.0);
    rig.params.limiter = false;
    for (int i = 0; i < 12; ++i)
    {
        rig.render (9000 + i * 517);
        rig.transport.ppq = (i % 3 == 0) ? 0.0 : 1.37 * i;      // host loops / seeks
    }
    const ClickResult c = detectClicks (rig.outL, 48000.0, 6000);
    CHECK_MSG (c.worstRatio < 6.0, "click ratio %.2f at %zu", c.worstRatio, c.position);
}

TEST_CASE (transport_stop_and_start_fade_cleanly)
{
    Rig rig (48000.0, 120.0);
    rig.sineLoop (4.0, 200.0);
    rig.render (30000);
    rig.transport.playing = false; rig.render (30000);
    CHECK_MSG (peakAbs (rig.outL, 50000, 60000) < 1.0e-4f, "not silent when stopped: %g", peakAbs (rig.outL, 50000, 60000));
    // restart in the middle of the waveform: must fade in, not step
    rig.transport.playing = true; rig.transport.ppq = 2.0321; rig.render (30000);
    CHECK (rms (rig.outL, 80000, 90000) > 0.2);
    const ClickResult c = detectClicks (rig.outL, 48000.0, 4000);
    CHECK_MSG (c.worstRatio < 9.0, "click ratio %.2f at %zu", c.worstRatio, c.position);
}


// ================================================================ glitch stages
// Helper: a loop whose sample value encodes its own position (a ramp), so the
// output tells us exactly which source position is being played.
static void rampLoop (Rig& rig, double beats)
{
    rig.makeLoop (beats, [=] (int64_t, double beat, float& l, float& r) { l = r = (float) (beat / beats); });
}
// source beat heard at output sample i (ramp loop), given loop length
static double heardBeat (const Rig& rig, int64_t i, double beats) { return (double) rig.outL[(size_t) i] * beats; }
static int64_t sampleAtBeat (const Rig& rig, double beat) { return (int64_t) std::llround (beat * rig.spb()) + rig.engine.latencySamples(); }

TEST_CASE (stutter_repeats_the_first_division_on_the_grid)
{
    Rig rig (48000.0, 120.0);
    rig.params.limiter = false; rig.params.declickMs = 1.0f;
    rampLoop (rig, 4.0);
    SeqPattern p = emptyPattern();
    for (int s = 8; s < 12; ++s) setStep (p, laneStutter, s, 4);     // beat 2..3, 1/16 repeats
    rig.engine.setSeqPattern (0, p);
    rig.render (48000 * 2);
    // before the run: real time
    CHECK (std::fabs (heardBeat (rig, sampleAtBeat (rig, 1.5), 4.0) - 1.5) < 0.002);
    // inside the run each 1/16 replays beat 2.0 .. 2.25
    for (int k = 0; k < 4; ++k)
    {
        const double t = 2.0 + k * 0.25 + 0.125;
        CHECK_MSG (std::fabs (heardBeat (rig, sampleAtBeat (rig, t), 4.0) - 2.125) < 0.002, "repeat %d heard %.4f", k, heardBeat (rig, sampleAtBeat (rig, t), 4.0));
    }
    // after the run: back in real time
    CHECK (std::fabs (heardBeat (rig, sampleAtBeat (rig, 3.5), 4.0) - 3.5) < 0.002);
    // the repeat boundary lands within one crossfade of the grid
    const int64_t b = sampleAtBeat (rig, 2.25);
    CHECK (heardBeat (rig, b - 60, 4.0) > 2.2);
    CHECK (heardBeat (rig, b + 60, 4.0) < 2.05);
}

TEST_CASE (reverse_plays_the_run_backwards)
{
    Rig rig (48000.0, 120.0);
    rig.params.limiter = false;
    rampLoop (rig, 4.0);
    SeqPattern p = emptyPattern();
    setStep (p, laneReverse, 4); setStep (p, laneReverse, 5);         // beat 1.0 .. 1.5
    rig.engine.setSeqPattern (0, p);
    rig.render (48000 * 2);
    CHECK_MSG (std::fabs (heardBeat (rig, sampleAtBeat (rig, 1.1), 4.0) - 1.4) < 0.003, "heard %.4f", heardBeat (rig, sampleAtBeat (rig, 1.1), 4.0));
    CHECK (std::fabs (heardBeat (rig, sampleAtBeat (rig, 1.4), 4.0) - 1.1) < 0.003);
    CHECK (std::fabs (heardBeat (rig, sampleAtBeat (rig, 1.75), 4.0) - 1.75) < 0.003);
}

TEST_CASE (tape_stop_slows_to_silence_then_recovers)
{
    Rig rig (48000.0, 120.0);
    rig.params.limiter = false; rig.params.tapeTime = 1.0f;
    rig.sineLoop (4.0, 440.0);
    SeqPattern p = emptyPattern();
    for (int s = 12; s < 16; ++s) setStep (p, laneTapeStop, s);       // last beat of the bar
    rig.engine.setSeqPattern (0, p);
    rig.render (48000 * 3);
    const double f0 = dominantFrequency (rig.outL, 48000.0, (size_t) sampleAtBeat (rig, 2.0), 4096);
    const double f1 = dominantFrequency (rig.outL, 48000.0, (size_t) sampleAtBeat (rig, 3.25), 2048, 30.0, 2000.0);
    const double f2 = dominantFrequency (rig.outL, 48000.0, (size_t) sampleAtBeat (rig, 3.6), 2048, 20.0, 2000.0);
    CHECK_MSG (std::fabs (centsBetween (f0, 440.0)) < 5.0, "f0 %.2f", f0);
    CHECK_MSG (f1 < 0.75 * f0 && f2 < f1, "f1 %.1f f2 %.1f", f1, f2);
    CHECK (rms (rig.outL, (size_t) sampleAtBeat (rig, 3.985), (size_t) sampleAtBeat (rig, 3.999)) < 0.06);
    CHECK (rms (rig.outL, (size_t) sampleAtBeat (rig, 4.1), (size_t) sampleAtBeat (rig, 4.4)) > 0.2);
    const ClickResult c = detectClicks (rig.outL, 48000.0, 6000);
    CHECK_MSG (c.worstRatio < 6.0, "click ratio %.2f at %zu", c.worstRatio, c.position);
}

TEST_CASE (chop_plays_the_chosen_slice)
{
    Rig rig (48000.0, 120.0);
    rig.params.limiter = false;
    rampLoop (rig, 4.0);
    SeqPattern p = emptyPattern();
    setStep (p, laneChop, 2, 9);      // step 2 plays slice 8 (beat 2.0)
    setStep (p, laneChop, 3, 9);      // tied: slice keeps running
    setStep (p, laneChop, 8, 1);      // step 8 plays slice 0
    rig.engine.setSeqPattern (0, p);
    rig.render (48000 * 2);
    CHECK_MSG (std::fabs (heardBeat (rig, sampleAtBeat (rig, 0.6), 4.0) - 2.1) < 0.003, "heard %.4f", heardBeat (rig, sampleAtBeat (rig, 0.6), 4.0));
    CHECK (std::fabs (heardBeat (rig, sampleAtBeat (rig, 0.9), 4.0) - 2.4) < 0.003);
    CHECK (std::fabs (heardBeat (rig, sampleAtBeat (rig, 2.1), 4.0) - 0.1) < 0.003);
    CHECK (std::fabs (heardBeat (rig, sampleAtBeat (rig, 1.5), 4.0) - 1.5) < 0.003);
    // Chop macro at zero disables rearrangement
    Rig off (48000.0, 120.0); off.params.limiter = false; off.params.chop = 0.0f;
    rampLoop (off, 4.0); off.engine.setSeqPattern (0, p); off.render (48000);
    CHECK (std::fabs (heardBeat (off, sampleAtBeat (off, 0.6), 4.0) - 0.6) < 0.003);
}

TEST_CASE (time_curve_half_speed_drops_an_octave_and_stays_on_grid)
{
    Rig rig (48000.0, 120.0);
    rig.params.limiter = false; rig.params.warp = 0.5f;   // 0.5 = curve exactly as drawn
    rig.sineLoop (8.0, 440.0);
    Curve c; c.clear(); c.lengthBeats = 4.0f; c.rangeBeats = 4.0f;
    c.add (0.0f, 0.0f); c.add (1.0f, 0.5f);               // falls 2 beats behind over 4 beats = half speed
    rig.engine.setTimeCurve (0, c);
    rig.render (48000 * 4);
    const double f = dominantFrequency (rig.outL, 48000.0, (size_t) sampleAtBeat (rig, 5.0), 8192);
    CHECK_MSG (std::fabs (centsBetween (f, 220.0)) < 5.0, "half speed freq %.3f", f);
    const ClickResult ck = detectClicks (rig.outL, 48000.0, 6000);
    CHECK_MSG (ck.worstRatio < 6.0, "click ratio %.2f at %zu", ck.worstRatio, ck.position);

    // position check with a ramp: at bar start we are back in real time
    Rig r2 (48000.0, 120.0); r2.params.limiter = false; rampLoop (r2, 8.0);
    r2.engine.setTimeCurve (0, c); r2.render (48000 * 4);
    CHECK (std::fabs (heardBeat (r2, sampleAtBeat (r2, 4.02), 8.0) - 4.01) < 0.01);
    CHECK_MSG (std::fabs (heardBeat (r2, sampleAtBeat (r2, 6.0), 8.0) - 5.0) < 0.01, "heard %.4f", heardBeat (r2, sampleAtBeat (r2, 6.0), 8.0));
    // Warp macro at 0 bypasses the curve
    Rig r3 (48000.0, 120.0); r3.params.limiter = false; r3.params.warp = 0.0f; rampLoop (r3, 8.0);
    r3.engine.setTimeCurve (0, c); r3.render (48000 * 4);
    CHECK (std::fabs (heardBeat (r3, sampleAtBeat (r3, 6.0), 8.0) - 6.0) < 0.003);
}

TEST_CASE (volume_curve_gates_without_clicks)
{
    Rig rig (48000.0, 120.0);
    rig.params.limiter = false;
    rig.sineLoop (4.0, 300.0);
    Curve c; c.clear(); c.lengthBeats = 4.0f;
    for (int i = 0; i < 16; ++i) { c.add (i / 16.0f, 1.0f, 0.0f, true); c.add ((i + 0.5f) / 16.0f, 0.0f, 0.0f, true); }   // hard 1/32 gate
    rig.engine.setVolCurve (0, c);
    rig.render (48000 * 2);
    const int64_t on = sampleAtBeat (rig, 1.0 + 0.0625), off = sampleAtBeat (rig, 1.125 + 0.0625);
    CHECK (rms (rig.outL, (size_t) (on - 300), (size_t) (on + 300)) > 0.25);
    CHECK (rms (rig.outL, (size_t) (off - 300), (size_t) (off + 300)) < 0.005);
    const ClickResult ck = detectClicks (rig.outL, 48000.0, 6000);
    CHECK_MSG (ck.worstRatio < 6.0, "click ratio %.2f at %zu", ck.worstRatio, ck.position);
}

TEST_CASE (slot_switch_is_quantised)
{
    Rig rig (48000.0, 120.0);
    rig.params.limiter = false; rig.params.quantize = 4;   // bar
    rampLoop (rig, 8.0);
    Curve c; c.clear(); c.lengthBeats = 4.0f; c.rangeBeats = 4.0f; c.add (0.0f, 0.25f); c.add (1.0f, 0.25f);   // constant 1 beat behind
    rig.engine.setTimeCurve (3, c);
    rig.render ((int64_t) (rig.spb() * 1.5));
    rig.params.timeSlot = 3;                                // asked for mid-bar
    rig.render (48000 * 3);
    CHECK (std::fabs (heardBeat (rig, sampleAtBeat (rig, 3.5), 8.0) - 3.5) < 0.003);   // not yet
    CHECK_MSG (std::fabs (heardBeat (rig, sampleAtBeat (rig, 4.5), 8.0) - 3.5) < 0.003, "heard %.4f", heardBeat (rig, sampleAtBeat (rig, 4.5), 8.0));
}

TEST_CASE (midi_slices_and_pattern_keys)
{
    Rig rig (48000.0, 120.0);
    rig.params.limiter = false; rig.params.loopPlay = false;
    rampLoop (rig, 4.0);
    rig.midi.push_back ({ 24000, true, 36 + 8, 1.0f, false });      // slice 8 = beat 2.0
    rig.midi.push_back ({ 36000, false, 36 + 8, 0.0f, false });
    rig.render (60000);
    const int lat = rig.engine.latencySamples();
    CHECK (peakAbs (rig.outL, 0, 24000) < 1.0e-5f);
    CHECK_MSG (std::fabs (rig.outL[(size_t) (24000 + lat + 6000)] * 4.0 - (2.0 + 6000.0 / rig.spb())) < 0.01, "heard %.4f", rig.outL[(size_t) (24000 + lat + 6000)] * 4.0);
    CHECK (peakAbs (rig.outL, (size_t) (36000 + lat + 2000), 60000) < 1.0e-5f);

    // patterns mode: note 37 selects time slot 1, note 73 holds a stutter
    Rig r2 (48000.0, 120.0); r2.params.limiter = false; r2.params.midiMode = 1; r2.params.quantize = 0;
    rampLoop (r2, 4.0);
    r2.midi.push_back ({ 24000, true, 73, 1.0f, false });
    r2.midi.push_back ({ 48000, false, 73, 0.0f, false });
    r2.render (72000);
    const double held = 24000.0 / r2.spb();
    const double h1 = r2.outL[(size_t) (24000 + lat + 3000)] * 4.0, h2 = r2.outL[(size_t) (24000 + lat + 3000 + 6000)] * 4.0;
    CHECK_MSG (std::fabs (h1 - (held + 3000.0 / r2.spb())) < 0.01 && std::fabs (h1 - h2) < 0.01, "h1 %.4f h2 %.4f", h1, h2);
    CHECK (std::fabs (r2.outL[(size_t) (60000 + lat)] * 4.0 - 60000.0 / r2.spb()) < 0.01);
}

// ================================================================ effects: sanity for every lane
TEST_CASE (every_lane_is_finite_bounded_and_clickless_on_a_tone)
{
    for (int lane = 0; lane < kNumLanes; ++lane)
    {
        Rig rig (48000.0, 120.0);
        rig.sineLoop (4.0, 261.6, 0.4f);
        rig.params.harmony = lane == laneHarmony ? 0.8f : 0.0f; rig.params.harmGate = 1;
        SeqPattern p = emptyPattern();
        setStep (p, lane, 2); setStep (p, lane, 3); setStep (p, lane, 6); setStep (p, lane, 9, 2); setStep (p, lane, 12, 3); setStep (p, lane, 13, 3);
        rig.engine.setSeqPattern (0, p);
        rig.render (48000 * 6);
        CHECK_MSG (! hasNonFinite (rig.outL) && ! hasNonFinite (rig.outR), "lane %s non finite", laneName (lane));
        CHECK_MSG (peakAbs (rig.outL) <= 0.9661f && peakAbs (rig.outR) <= 0.9661f, "lane %s peak %g", laneName (lane), peakAbs (rig.outL));
        CHECK_MSG (rms (rig.outL, 48000) > 0.02, "lane %s silent", laneName (lane));
        // crush / ring / comb / freeze / reverb are *meant* to add rough or dense
        // content, so the click metric only applies to the clean lanes
        if (lane != laneCrush && lane != laneRing && lane != laneComb && lane != laneFreeze && lane != laneReverb && lane != laneDelay)
        {
            const ClickResult c = detectClicks (rig.outL, 48000.0, 6000);
            CHECK_MSG (c.worstRatio < 8.0, "lane %s click ratio %.2f at %zu (beat %.3f)", laneName (lane), c.worstRatio, c.position, (c.position - rig.engine.latencySamples()) / rig.spb());
        }
    }
}

TEST_CASE (limiter_never_exceeds_ceiling)
{
    Limiter lim; lim.prepare (48000.0);
    Rng rng (8); float worst = 0.0f;
    for (int i = 0; i < 200000; ++i)
    {
        float l = rng.bipolar() * ((i / 5000) % 2 ? 6.0f : 0.5f), r = (i % 9000 == 0) ? 20.0f : l * 0.3f;
        lim.process (l, r);
        worst = std::max (worst, std::max (std::fabs (l), std::fabs (r)));
    }
    CHECK_MSG (worst <= 0.9661f, "worst %g", worst);
    // transparent below the ceiling, apart from its delay
    lim.reset(); double err = 0.0; std::vector<float> in;
    for (int i = 0; i < 20000; ++i)
    {
        const float x = 0.5f * (float) std::sin (0.05 * i); in.push_back (x);
        float l = x, r = x; lim.process (l, r);
        if (i >= lim.latency() + 200) err = std::max (err, (double) std::fabs (l - in[(size_t) (i - lim.latency())]));
    }
    CHECK_MSG (err < 1.0e-6, "err %g", err);
}


// ================================================================ harmoniser
TEST_CASE (harmoniser_pitch_accuracy_and_latency)
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        for (double semis : { 3.0, 4.0, 7.0, 12.0, -5.0, -12.0 })
        {
            Harmonizer h; h.prepare (sr); h.setActive (true); h.setFormant (0.0f);
            Harmonizer::Voice v; v.ratio = (float) semisToRatio (semis); v.gainL = v.gainR = 1.0f;
            h.setVoice (0, v);
            std::vector<float> out;
            const double f0 = 330.0;
            for (int i = 0; i < (int) sr; ++i)
            {
                const float x = 0.5f * (float) std::sin (kTwoPi * f0 * i / sr);
                float l, r; h.process (x, x, l, r); out.push_back (l);
            }
            const double target = f0 * semisToRatio (semis);
            const double f = dominantFrequency (out, sr, (size_t) (sr * 0.4), (size_t) (sr * 0.4), 60.0, 3000.0);
            CHECK_MSG (std::fabs (centsBetween (f, target)) < 3.0, "sr %.0f shift %+.0f st: %.3f Hz (want %.3f, %.2f cents)", sr, semis, f, target, centsBetween (f, target));
            const double level = rms (out, (size_t) (sr * 0.4), (size_t) (sr * 0.9));
            CHECK_MSG (level > 0.28 && level < 0.42, "sr %.0f shift %+.0f st level %.3f", sr, semis, level);
            // steadiness: envelope ripple of the shifted tone
            double lo = 1e9, hi = 0.0;
            for (size_t b = (size_t) (sr * 0.4); b + 512 < (size_t) (sr * 0.9); b += 512) { const double e = rms (out, b, b + 512); lo = std::min (lo, e); hi = std::max (hi, e); }
            CHECK_MSG (hi / lo < 1.2, "sr %.0f shift %+.0f st ripple %.3f", sr, semis, hi / lo);
        }
        // unity ratio reproduces the input delayed by exactly fftSize
        Harmonizer h; h.prepare (sr); h.setActive (true); h.setFormant (0.0f);
        Harmonizer::Voice v; v.ratio = 1.0f; v.gainL = v.gainR = 1.0f; h.setVoice (0, v);
        Rng rng (2); std::vector<float> in, out;
        for (int i = 0; i < 30000; ++i) { const float x = 0.3f * rng.bipolar(); in.push_back (x); float l, r; h.process (x, x, l, r); out.push_back (l); }
        double err = 0.0;
        for (int i = h.fftSize() * 3; i < 30000; ++i) err = std::max (err, (double) std::fabs (out[(size_t) i] - in[(size_t) (i - h.fftSize())]));
        CHECK_MSG (err < 2.0e-2, "sr %.0f unity err %g", sr, err);
    }
}

TEST_CASE (harmoniser_keeps_transients_tight)
{
    // decaying noise bursts: the shifted voice should start with the burst, with
    // little energy smeared ahead of it (pre-echo)
    const double sr = 48000.0;
    Harmonizer h; h.prepare (sr); h.setActive (true); h.setFormant (0.0f);
    Harmonizer::Voice v; v.ratio = (float) semisToRatio (7.0); v.gainL = v.gainR = 1.0f; h.setVoice (0, v);
    Rng rng (6); std::vector<float> out;
    const int period = 12000, lat = h.fftSize();
    for (int i = 0; i < period * 12; ++i)
    {
        const int k = i % period;
        const float x = 0.6f * rng.bipolar() * (float) std::exp (-(double) k / 700.0);
        float l, r; h.process (x, x, l, r); out.push_back (l);
    }
    double worstPre = 0.0, meanPre = 0.0; int n = 0;
    for (int b = 3; b < 11; ++b)
    {
        const size_t on = (size_t) (b * period + lat);
        const double pre = rms (out, on - 960, on - 48), post = rms (out, on, on + 960);
        const double ratio = pre / (post + 1.0e-9);
        worstPre = std::max (worstPre, ratio); meanPre += ratio; ++n;
    }
    meanPre /= n;
    std::printf ("   pre-echo (20 ms before / 20 ms after the hit): mean %.3f (%.1f dB), worst %.3f\n", meanPre, 20.0 * std::log10 (meanPre), worstPre);
    CHECK_MSG (meanPre < 0.2, "mean pre-echo ratio %.3f", meanPre);
}

TEST_CASE (harmoniser_preserves_formants)
{
    // source: harmonic tone with one strong formant at 1.2 kHz. Shift up 7 st and
    // compare where the spectral energy peak sits with and without preservation.
    const double sr = 48000.0, f0 = 150.0, formantHz = 1200.0;
    auto run = [&] (float formant)
    {
        Harmonizer h; h.prepare (sr); h.setActive (true); h.setFormant (formant);
        Harmonizer::Voice v; v.ratio = (float) semisToRatio (7.0); v.gainL = v.gainR = 1.0f; h.setVoice (0, v);
        std::vector<float> out;
        for (int i = 0; i < 48000; ++i)
        {
            double x = 0.0;
            for (int k = 1; k * f0 < 6000.0; ++k)
            {
                const double d = (k * f0 - formantHz) / 250.0;
                x += std::exp (-0.5 * d * d) * std::sin (kTwoPi * k * f0 * i / sr) + 0.02 * std::sin (kTwoPi * k * f0 * i / sr);
            }
            float l, r; h.process ((float) (0.2 * x), (float) (0.2 * x), l, r); out.push_back (l);
        }
        // spectral centroid of the output between 400 Hz and 4 kHz
        double num = 0.0, den = 0.0;
        for (double f = 400.0; f < 4000.0; f += 25.0)
        {
            double re = 0.0, im = 0.0;
            for (int i = 24000; i < 24000 + 8192; ++i) { const double w = 0.5 - 0.5 * std::cos (kTwoPi * (i - 24000) / 8192.0); re += w * out[(size_t) i] * std::cos (kTwoPi * f * i / sr); im += w * out[(size_t) i] * std::sin (kTwoPi * f * i / sr); }
            const double p = re * re + im * im; num += f * p; den += p;
        }
        return num / den;
    };
    const double plain = run (0.0f), kept = run (1.0f);
    CHECK_MSG (plain > formantHz * 1.35, "plain centroid %.0f", plain);              // formant moved up with the pitch
    CHECK_MSG (std::fabs (kept - formantHz) < formantHz * 0.12, "kept centroid %.0f", kept);
}

TEST_CASE (engine_harmony_voices_land_in_key)
{
    // A loop holding the note E4 in C major: a diatonic third above E is G (+3),
    // a fifth above is B (+7); with Chromatic mode +4 is a plain major third.
    Rig rig (48000.0, 120.0);
    rig.params.limiter = false;
    const double e4 = 329.6276;
    rig.sineLoop (4.0, e4, 0.4f);
    const double src = std::round (e4 * 2.0) / 2.0;       // sineLoop rounds to the loop period
    rig.params.harmony = 1.0f; rig.params.key = 0; rig.params.scale = 0; rig.params.harmMode = 0; rig.params.formant = 0.0f;
    rig.params.voiceOn[0] = true; rig.params.voiceOn[1] = false; rig.params.voiceInterval[0] = 2; rig.params.voiceLevel[0] = 1.0f; rig.params.voicePan[0] = 0.0f;
    rig.params.mix = 1.0f;
    rig.render (48000 * 3);
    // remove the fundamental by looking only above it
    const double f = dominantFrequency (rig.outL, 48000.0, 48000 * 2, 16384, src * 1.1, 3000.0);
    CHECK_MSG (std::fabs (centsBetween (f, src * semisToRatio (3.0))) < 6.0, "third: %.2f Hz, %.1f cents off", f, centsBetween (f, src * semisToRatio (3.0)));

    Rig r2 (48000.0, 120.0); r2.params = rig.params; r2.params.voiceInterval[0] = 4;
    r2.sineLoop (4.0, e4, 0.4f); r2.render (48000 * 3);
    const double f5 = dominantFrequency (r2.outL, 48000.0, 48000 * 2, 16384, src * 1.1, 3000.0);
    CHECK_MSG (std::fabs (centsBetween (f5, src * semisToRatio (7.0))) < 6.0, "fifth: %.2f Hz", f5);

    Rig r3 (48000.0, 120.0); r3.params = rig.params; r3.params.harmMode = 1; r3.params.voiceInterval[0] = 4;
    r3.sineLoop (4.0, e4, 0.4f); r3.render (48000 * 3);
    const double f3 = dominantFrequency (r3.outL, 48000.0, 48000 * 2, 16384, src * 1.1, 3000.0);
    CHECK_MSG (std::fabs (centsBetween (f3, src * semisToRatio (4.0))) < 6.0, "chromatic: %.2f Hz", f3);
}

// ================================================================ analysis and stretch
TEST_CASE (demo_loops_are_clean_and_analysed_correctly)
{
    for (int i = 0; i < kNumDemoLoops; ++i)
    {
        const DemoLoopInfo& info = demoLoopInfo (i);
        AudioData a; generateDemoLoop (i, 48000.0, a);
        CHECK (! hasNonFinite (a.ch[0]) && ! hasNonFinite (a.ch[1]));
        CHECK_MSG (peakAbs (a.ch[0]) <= 0.71f && peakAbs (a.ch[0]) > 0.3f, "%s peak %g", info.name, peakAbs (a.ch[0]));
        CHECK (std::fabs (dcOffset (a.ch[0])) < 1.0e-3);
        const double dur = (double) a.length() / 48000.0;
        CHECK (std::fabs (dur - info.beats * 60.0 / info.bpm) < 1.0e-3);
        const LoopAnalysis an = analyseLoop (a);
        std::printf ("   %-12s bpm %.2f (true %.0f) beats %.0f onsets %zu key %d/%d conf %.2f perc %.2f\n", info.name, an.bpm, info.bpm, an.beats, an.onsetTimes.size(), an.key, an.scale, an.keyConfidence, an.percussiveness);
        CHECK_MSG (std::fabs (an.bpm - info.bpm) < 0.5, "%s tempo %.2f", info.name, an.bpm);
        CHECK (an.onsetTimes.size() >= 6);
        if (! info.drums)
        {
            // the detected key must describe (nearly) the same set of notes
            int shared = 0;
            for (int k = 0; k < 7; ++k)
                for (int m = 0; m < 7; ++m)
                    if ((an.key + scaleDef (an.scale).steps[k]) % 12 == (info.key + scaleDef (info.scale).steps[m]) % 12) ++shared;
            CHECK_MSG (shared >= 6, "%s key %d/%d shares %d notes with the true key", info.name, an.key, an.scale, shared);
        }
        const SliceSet sl = slicesFromOnsets (an, dur, 0.5f);
        CHECK (sl.count >= 4 && sl.count <= kMaxSlices);
        for (int k = 1; k < sl.count; ++k) CHECK (sl.startBeat[k] > sl.startBeat[k - 1]);
    }
}

TEST_CASE (onset_detection_is_accurate_on_known_hits)
{
    const double sr = 48000.0;
    AudioData a; a.sampleRate = sr; a.resize ((int64_t) (sr * 4));
    const double times[] = { 0.0, 0.5, 0.75, 1.0, 1.5, 2.0, 2.25, 2.5, 3.0, 3.5 };
    Rng rng (1);
    for (double t : times)
        for (int i = 0; i < 4000; ++i)
        {
            const float v = 0.6f * rng.bipolar() * (float) std::exp (-i / 600.0);
            a.ch[0][(size_t) (t * sr) + (size_t) i] += v; a.ch[1][(size_t) (t * sr) + (size_t) i] += v;
        }
    const LoopAnalysis an = analyseLoop (a);
    CHECK_MSG (std::fabs (an.bpm - 120.0) < 0.5, "bpm %.2f", an.bpm);
    int matched = 0; double worst = 0.0;
    for (double t : times)
    {
        if (t == 0.0) continue;
        double best = 1.0;
        for (double o : an.onsetTimes) best = std::min (best, std::fabs (o - t));
        if (best < 0.004) ++matched;
        worst = std::max (worst, best);
    }
    CHECK_MSG (matched == 9, "matched %d of 9, worst %.2f ms, found %zu", matched, worst * 1000.0, an.onsetTimes.size());
    CHECK (an.onsetTimes.size() <= 11);
}

TEST_CASE (stretch_preserves_pitch_length_and_transients)
{
    const double sr = 48000.0;
    for (double ratio : { 0.75, 1.25, 1.5 })
    {
        // tonal: steady chord
        std::vector<float> in[2], out[2];
        const int64_t n = 96000, outLen = (int64_t) (n * ratio);
        for (int c = 0; c < 2; ++c) { in[c].resize ((size_t) n); for (int64_t i = 0; i < n; ++i) in[c][(size_t) i] = 0.3f * (float) (std::sin (kTwoPi * 220.0 * i / sr) + 0.5 * std::sin (kTwoPi * 554.5 * i / sr)); }
        stretchTonal (in, out, outLen, {}, sr);
        CHECK ((int64_t) out[0].size() == outLen);
        const double f = dominantFrequency (out[0], sr, (size_t) (outLen / 3), 16384, 100.0, 400.0);
        CHECK_MSG (std::fabs (centsBetween (f, 220.0)) < 3.0, "tonal ratio %.2f freq %.3f", ratio, f);
        double lo = 1e9, hi = 0.0;
        for (size_t b = 4096; b + 2048 < (size_t) outLen - 4096; b += 2048) { const double e = rms (out[0], b, b + 2048); lo = std::min (lo, e); hi = std::max (hi, e); }
        CHECK_MSG (hi / lo < 1.15, "tonal ratio %.2f ripple %.3f", ratio, hi / lo);
        const double inRms = rms (in[0]), outRms = rms (out[0]);
        CHECK_MSG (std::fabs (outRms / inRms - 1.0) < 0.08, "tonal ratio %.2f level %.3f", ratio, outRms / inRms);

        // beats: decaying noise hits every 12000 samples; they must land on ratio * position and stay sharp
        std::vector<double> onsets; Rng rng (3);
        for (int c = 0; c < 2; ++c) std::fill (in[c].begin(), in[c].end(), 0.0f);
        for (int64_t t = 0; t < n; t += 12000)
        {
            if (t > 0) onsets.push_back ((double) t);
            for (int i = 0; i < 6000; ++i) { const float v = 0.6f * rng.bipolar() * (float) std::exp (-i / 900.0); in[0][(size_t) (t + i)] = v; in[1][(size_t) (t + i)] = v; }
        }
        for (int mode = 0; mode < 2; ++mode)
        {
            if (mode == 0) stretchBeats (in, out, outLen, onsets, sr); else stretchTonal (in, out, outLen, onsets, sr);
            double worst = 0.0, minSharp = 1e9;
            for (int k = 1; k < 8; ++k)
            {
                const int64_t ideal = (int64_t) (k * 12000 * ratio);
                // attack position = first sample above 30% of the local peak
                float pk = 0.0f;
                for (int64_t i = ideal - 2000; i < ideal + 2000; ++i) pk = std::max (pk, std::fabs (out[0][(size_t) i]));
                int64_t at = ideal - 2000;
                for (int64_t i = ideal - 2000; i < ideal + 2000; ++i) if (std::fabs (out[0][(size_t) i]) > 0.3f * pk) { at = i; break; }
                worst = std::max (worst, std::fabs ((double) (at - ideal)));
                // sharpness: energy just after the attack vs just before
                const double before = rms (out[0], (size_t) (ideal - 700), (size_t) (ideal - 200)), after = rms (out[0], (size_t) ideal, (size_t) (ideal + 500));
                minSharp = std::min (minSharp, after / (before + 1.0e-6));
            }
            CHECK_MSG (worst < (mode == 0 ? 96.0 : 300.0), "%s ratio %.2f attack off by %.0f samples", mode == 0 ? "beats" : "tonal", ratio, worst);
            CHECK_MSG (minSharp > (mode == 0 ? 8.0 : 3.0), "%s ratio %.2f sharpness %.1f", mode == 0 ? "beats" : "tonal", ratio, minSharp);
            CHECK (! hasNonFinite (out[0]));
        }
    }
}

TEST_CASE (render_loop_hits_exact_length_at_any_tempo_and_rate)
{
    AudioData a; generateDemoLoop (1, 44100.0, a);
    LoopAnalysis an = analyseLoop (a);
    for (double sr : { 44100.0, 96000.0 })
        for (double bpm : { 90.0, 120.0, 174.0 })
            for (auto mode : { StretchMode::Tonal, StretchMode::Beats, StretchMode::Tape })
            {
                RenderedLoop r; renderLoop (a, an, sr, bpm, mode, r);
                const int64_t want = (int64_t) std::llround (an.beats * 60.0 / bpm * sr);
                CHECK (r.length == want);
                std::vector<float> v (r.channel (0), r.channel (0) + r.length);
                CHECK (! hasNonFinite (v));
                CHECK_MSG (peakAbs (v) < 1.2f && rms (v) > 0.02, "sr %.0f bpm %.0f mode %d peak %g rms %g", sr, bpm, (int) mode, peakAbs (v), rms (v));
                // seamless: guard region mirrors the loop
                CHECK (r.channel (0)[r.length] == r.channel (0)[0]);
                CHECK (r.channel (0)[-1] == r.channel (0)[r.length - 1]);
            }
}

int main (int argc, char** argv) { return testutil::runAll (argc > 1 ? argv[1] : nullptr); }
