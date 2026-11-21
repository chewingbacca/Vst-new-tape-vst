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
    rig.transport.playing = true; rig.transport.ppq = 2.0; rig.render (30000);
    CHECK (rms (rig.outL, 80000, 90000) > 0.2);
    const ClickResult c = detectClicks (rig.outL, 48000.0, 4000);
    CHECK_MSG (c.worstRatio < 6.0, "click ratio %.2f at %zu", c.worstRatio, c.position);
}

int main (int argc, char** argv) { return testutil::runAll (argc > 1 ? argv[1] : nullptr); }
