// Splinter — offline render harness.
//
// Simulates a host (tempo, transport, MIDI, block sizes, sample rates), renders
// synthesised loops through every factory preset to WAV and measures the
// result: NaN/Inf, clipping, DC offset, clicks, timing drift against the grid,
// harmony pitch accuracy, block-size independence, state round trip and CPU.
#include "TestUtil.h"
#include "plugin/PluginProcessor.h"
#include <chrono>

using namespace testutil;
using splinter::kTwoPi;

namespace
{
struct FakePlayHead final : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0; bool playing = true; int64_t samples = 0; bool valid = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        if (! valid) return {};
        PositionInfo p;
        p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing); p.setTimeInSamples (samples);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

struct Midi { int64_t sample; bool on; int note; float vel; };

struct Session
{
    std::unique_ptr<SplinterProcessor> proc;
    FakePlayHead head;
    double sr = 48000.0;
    int maxBlock = 512;
    std::vector<float> L, R;
    std::vector<Midi> midi;
    int64_t pos = 0;
    double cpuSeconds = 0.0;

    explicit Session (double sampleRate = 48000.0, double bpm = 120.0, int block = 512)
    {
        sr = sampleRate; maxBlock = block; head.bpm = bpm;
        proc.reset (dynamic_cast<SplinterProcessor*> (createPluginFilter()));
        proc->setPlayHead (&head);
        proc->setPlayConfigDetails (2, 2, sr, maxBlock);
        proc->prepareToPlay (sr, maxBlock);
        settle();
    }
    ~Session() { proc->releaseResources(); }

    // wait for the background worker, then run what the message thread would do
    void settle()
    {
        proc->loops.setTarget (sr, head.bpm, (int) proc->getParam (pid::stretchMode));
        proc->loops.waitUntilReady (60000);
        proc->handleLoopChanged (settleKeepsSlices);
        proc->loops.waitUntilReady (60000);
    }
    bool settleKeepsSlices = false;
    void demo (int index) { proc->loops.loadDemo (index); settle(); }
    void audio (splinter::AudioData&& a, double bpmHint) { proc->loops.loadAudio (std::move (a), "test", bpmHint, LoopInfo::capture); settle(); }
    void preset (int index) { proc->loadFactoryPreset (index); }
    void set (const juce::String& id, float v) { proc->setParam (id, v); }

    int latency() const { return proc->getLatencySamples(); }
    double spb() const { return sr * 60.0 / head.bpm; }
    int64_t sampleAtBeat (double beat) const { return (int64_t) std::llround (beat * spb()) + latency(); }

    // blockSize 0 = random sizes up to maxBlock
    void render (int64_t numSamples, int blockSize = -1, const std::function<void (int64_t, float&, float&)>& input = nullptr)
    {
        if (blockSize < 0) blockSize = maxBlock;
        splinter::Rng rng (1234);
        juce::AudioBuffer<float> buf (2, maxBlock);
        juce::MidiBuffer mb;
        int64_t done = 0;
        while (done < numSamples)
        {
            int n = blockSize == 0 ? 1 + rng.range (maxBlock) : blockSize;
            n = (int) std::min<int64_t> (n, numSamples - done);
            buf.setSize (2, n, false, false, true);
            buf.clear();
            if (input) for (int i = 0; i < n; ++i) input (pos + i, buf.getWritePointer (0)[i], buf.getWritePointer (1)[i]);
            mb.clear();
            for (auto& m : midi)
                if (m.sample >= pos && m.sample < pos + n)
                    mb.addEvent (m.on ? juce::MidiMessage::noteOn (1, m.note, m.vel) : juce::MidiMessage::noteOff (1, m.note), (int) (m.sample - pos));
            head.samples = pos;
            const auto t0 = std::chrono::steady_clock::now();
            proc->processBlock (buf, mb);
            cpuSeconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            L.insert (L.end(), buf.getReadPointer (0), buf.getReadPointer (0) + n);
            R.insert (R.end(), buf.getReadPointer (1), buf.getReadPointer (1) + n);
            if (head.playing) head.ppq += (double) n * head.bpm / (60.0 * sr);
            pos += n; done += n;
        }
    }
    void renderBeats (double beats, int blockSize = -1) { render ((int64_t) std::llround (beats * spb()), blockSize); }
    double cpuPercent() const { return 100.0 * cpuSeconds / ((double) L.size() / sr); }
};

// A smooth, loopable pad: any click the plugin adds stands out against it.
splinter::AudioData smoothPad (double sr, double bpm, double beats)
{
    splinter::AudioData a; a.sampleRate = sr;
    const double dur = beats * 60.0 / bpm;
    a.resize ((int64_t) std::llround (dur * sr));
    const double freqs[5] = { 110.0, 164.81, 220.0, 277.18, 329.63 };
    for (int64_t i = 0; i < a.length(); ++i)
    {
        double v = 0.0;
        for (int k = 0; k < 5; ++k) { const double f = std::round (freqs[k] * dur) / dur; v += std::sin (kTwoPi * f * (double) i / sr + k) / (1.0 + k * 0.5); }
        a.ch[0][(size_t) i] = a.ch[1][(size_t) i] = (float) (0.16 * v);
    }
    return a;
}

splinter::AudioData sineLoop (double sr, double bpm, double beats, double hz, float amp = 0.4f)
{
    splinter::AudioData a; a.sampleRate = sr;
    const double dur = beats * 60.0 / bpm;
    a.resize ((int64_t) std::llround (dur * sr));
    const double f = std::round (hz * dur) / dur;
    for (int64_t i = 0; i < a.length(); ++i) a.ch[0][(size_t) i] = a.ch[1][(size_t) i] = amp * (float) std::sin (kTwoPi * f * (double) i / sr);
    return a;
}

splinter::AudioData clickLoop (double sr, double bpm, double beats)
{
    splinter::AudioData a; a.sampleRate = sr;
    a.resize ((int64_t) std::llround (beats * 60.0 / bpm * sr));
    splinter::Rng rng (5);
    for (int b = 0; b < (int) beats; ++b)
    {
        const int64_t s0 = (int64_t) std::llround (b * 60.0 / bpm * sr);
        for (int i = 0; i < (int) (0.03 * sr); ++i)
        {
            const float v = 0.7f * rng.bipolar() * (float) std::exp (-(double) i / (0.004 * sr));
            if (s0 + i < a.length()) { a.ch[0][(size_t) (s0 + i)] = v; a.ch[1][(size_t) (s0 + i)] = v; }
        }
    }
    return a;
}

juce::String outDir;
std::vector<juce::String> report;
void save (const juce::String& name, const Session& s)
{
    if (outDir.isEmpty()) return;
    writeWav ((outDir + "/" + name + ".wav").toStdString(), s.L, s.R, s.sr);
}
void note (const juce::String& line) { report.push_back (line); std::printf ("   %s\n", line.toRawUTF8()); }

bool presetIsRough (int index)
{
    // presets that intentionally add harsh / dense content, where the click metric does not apply as-is
    PresetData d; presets::buildFactory (index, d);
    for (auto& s : d.bank.seq)
        for (int lane : { (int) splinter::laneCrush, (int) splinter::laneRing, (int) splinter::laneComb, (int) splinter::laneFreeze })
            if (! s.laneEmpty (lane)) return true;
    return false;
}
} // namespace

// ================================================================ tests
TEST_CASE (default_state_makes_sound_immediately)
{
    Session s;
    s.renderBeats (16.0);
    CHECK (! hasNonFinite (s.L) && ! hasNonFinite (s.R));
    CHECK_MSG (rms (s.L, 20000) > 0.02, "rms %g", rms (s.L, 20000));
    CHECK (peakAbs (s.L) <= 1.0f && peakAbs (s.R) <= 1.0f);
    CHECK (s.latency() > 0);
    save ("default", s);
}

TEST_CASE (every_preset_on_every_demo_loop_is_clean)
{
    const double bpms[splinter::kNumDemoLoops] = { 96.0, 128.0, 140.0, 90.0, 118.0, 124.0 };
    double worstCpu = 0.0, sumCpu = 0.0; int count = 0; juce::String worstName;
    for (int loop = 0; loop < splinter::kNumDemoLoops; ++loop)
    {
        Session s (48000.0, bpms[loop], 512);
        s.demo (loop);
        for (int p = 0; p < presets::numFactory(); ++p)
        {
            // each preset is exercised on two loops, so all loops and presets are covered
            if ((p + loop) % 3 != 0 && loop != p % splinter::kNumDemoLoops) continue;
            s.preset (p);
            s.L.clear(); s.R.clear(); s.cpuSeconds = 0.0;
            s.head.ppq = 0.0;
            s.renderBeats (32.0);
            const juce::String name = presets::factoryName (p) + " / " + splinter::demoLoopInfo (loop).name;
            const bool finite = ! hasNonFinite (s.L) && ! hasNonFinite (s.R);
            const float peak = std::max (peakAbs (s.L), peakAbs (s.R));
            const double level = rms (s.L, 24000), dc = std::max (std::fabs (dcOffset (s.L)), std::fabs (dcOffset (s.R)));
            CHECK_MSG (finite, "%s: non-finite output", name.toRawUTF8());
            CHECK_MSG (peak <= 0.967f, "%s: peak %.4f", name.toRawUTF8(), peak);
            CHECK_MSG (level > 0.01 && level < 0.6, "%s: rms %.4f", name.toRawUTF8(), level);
            CHECK_MSG (dc < 0.01, "%s: dc %.5f", name.toRawUTF8(), dc);
            const double cpu = s.cpuPercent();
            sumCpu += cpu; ++count;
            if (cpu > worstCpu) { worstCpu = cpu; worstName = name; }
            if (loop == p % splinter::kNumDemoLoops)
                save (juce::String::formatted ("preset_%02d_", p + 1) + presets::factoryName (p).replaceCharacters (" '&", "___") + "__" + juce::String (splinter::demoLoopInfo (loop).name).replaceCharacter (' ', '_'), s);
        }
    }
    note (juce::String::formatted ("CPU at 48 kHz / 512: mean %.1f %%, worst %.1f %% (%s), over %d renders", sumCpu / count, worstCpu, worstName.toRawUTF8(), count));
    CHECK_MSG (worstCpu < 25.0, "worst CPU %.1f %% (%s)", worstCpu, worstName.toRawUTF8());
    CHECK_MSG (sumCpu / count < 15.0, "mean CPU %.1f %%", sumCpu / count);
}

TEST_CASE (presets_do_not_click_on_smooth_material)
{
    // Time-domain glitching (chop, stutter, reverse, tape, pitch, curves, gates,
    // harmony, filter) must be click free. Lanes that are harsh by design are
    // switched off for this audit.
    Session s (48000.0, 120.0, 512);
    s.audio (smoothPad (48000.0, 120.0, 8.0), 120.0);
    double worst = 0.0; juce::String worstName;
    for (int p = 0; p < presets::numFactory(); ++p)
    {
        s.preset (p);
        s.set (pid::crush, 0.0f);
        s.proc->editBank ([] (PatternBank& b)
        {
            for (auto& q : b.seq)
                for (int lane : { (int) splinter::laneCrush, (int) splinter::laneRing, (int) splinter::laneComb,
                                  (int) splinter::laneDelay, (int) splinter::laneReverb })
                {
                    for (auto& st : q.steps[lane]) st.on = 0;
                    q.locked[lane] = 1;                    // keeps Chaos from injecting them either
                }
        }, SplinterProcessor::allBits);
        s.L.clear(); s.R.clear(); s.head.ppq = 0.0;
        s.renderBeats (32.0, 0);
        const ClickResult c = detectClicks (s.L, s.sr, 12000);
        const bool rough = presetIsRough (p);
        if (c.worstRatio > worst) { worst = c.worstRatio; worstName = presets::factoryName (p); }
        CHECK_MSG (c.worstRatio < 9.0, "%s: click ratio %.1f at sample %zu (beat %.3f)%s", presets::factoryName (p).toRawUTF8(), c.worstRatio, c.position,
                   (double) ((int64_t) c.position - s.latency()) / s.spb(), rough ? " [rough preset]" : "");
        if (c.worstRatio >= 9.0) save ("click_" + presets::factoryName (p).replaceCharacters (" '&", "___"), s);
    }
    note (juce::String::formatted ("click audit: worst ratio %.2f (%s); threshold 9, a hard cut scores > 20", worst, worstName.toRawUTF8()));
}

TEST_CASE (timing_is_locked_to_the_grid)
{
    // Tape mode: an exact resample, so hits must land within a sample or two.
    // Beats / Tonal: transients are protected, a few ms tolerance.
    struct Case { int mode; double hostBpm; double tolSamples; const char* name; };
    const Case cases[] = { { 3, 120.0, 1.5, "tape 120" }, { 3, 133.0, 3.0, "tape 133" }, { 2, 104.0, 140.0, "beats 104" }, { 2, 150.0, 140.0, "beats 150" }, { 1, 133.0, 400.0, "tonal 133" } };
    for (auto& cs : cases)
    {
        Session s (48000.0, cs.hostBpm, 256);
        s.preset (presets::numFactory() - 1);            // Clean Slate
        s.set (pid::stretchMode, (float) cs.mode);
        s.audio (clickLoop (48000.0, 120.0, 8.0), 120.0);
        s.renderBeats (40.0, 0);
        double worst = 0.0; int found = 0;
        for (int b = 2; b < 38; ++b)
        {
            const int64_t ideal = s.sampleAtBeat ((double) b);
            float pk = 0.0f;
            for (int64_t i = ideal - 1500; i < ideal + 1500; ++i) pk = std::max (pk, std::fabs (s.L[(size_t) i]));
            int64_t at = -1;
            for (int64_t i = ideal - 1500; i < ideal + 1500; ++i) if (std::fabs (s.L[(size_t) i]) > 0.25f * pk) { at = i; break; }
            if (pk > 0.1f && at >= 0) { ++found; worst = std::max (worst, (double) std::llabs (at - ideal)); }
        }
        note (juce::String::formatted ("timing %-10s: %d/36 beats found, worst offset %.1f samples (%.2f ms), no cumulative drift over 40 beats", cs.name, found, worst, worst / 48.0));
        CHECK_MSG (found == 36, "%s: found %d", cs.name, found);
        CHECK_MSG (worst <= cs.tolSamples, "%s: worst %.1f samples", cs.name, worst);
        if (cs.mode == 2 && cs.hostBpm == 104.0) save ("timing_beats_104", s);
    }
}

TEST_CASE (harmony_voices_are_in_tune)
{
    // A4-ish source in A minor; thirds (C, +3), fifths (E, +7) and an octave down.
    struct V { int mode; int interval; double semis; const char* name; };
    const V vs[] = { { 0, 2, 3.0, "scale third" }, { 0, 4, 7.0, "scale fifth" }, { 0, -7, -12.0, "scale octave down" }, { 1, 4, 4.0, "chromatic +4" }, { 1, 12, 12.0, "chromatic +12" } };
    for (double sr : { 44100.0, 96000.0 })
        for (auto& v : vs)
        {
            Session s (sr, 120.0, 512);
            s.preset (presets::numFactory() - 1);
            const double srcHz = std::round (440.0 * 4.0) / 4.0;
            s.audio (sineLoop (sr, 120.0, 8.0, 440.0), 120.0);
            s.set (pid::key, 9.0f); s.set (pid::scale, 1.0f); s.set (pid::harmMode, (float) v.mode); s.set (pid::harmony, 1.0f); s.set (pid::formant, 0.0f);
            s.set ("v1On", 1.0f); s.set ("v2On", 0.0f); s.set ("v1Interval", (float) v.interval); s.set ("v1Level", 1.0f); s.set ("v1Pan", 0.0f);
            s.renderBeats (16.0);
            const double want = srcHz * std::exp2 (v.semis / 12.0);
            const double lo = v.semis > 0 ? srcHz * 1.08 : 60.0, hi = v.semis > 0 ? 4000.0 : srcHz * 0.92;
            const double f = dominantFrequency (s.L, sr, (size_t) (sr * 4), 32768, lo, hi);
            const double cents = centsBetween (f, want);
            note (juce::String::formatted ("harmony %-18s @ %.0f Hz: %.2f Hz (target %.2f), %+.2f cents", v.name, sr, f, want, cents));
            CHECK_MSG (std::fabs (cents) < 5.0, "%s at %.0f: %.2f cents", v.name, sr, cents);
        }
}

TEST_CASE (output_is_identical_for_any_block_size)
{
    // Pass 0: everything except the harmoniser and the bit crusher. Must match to
    // float precision whatever the host's buffer size.
    // Pass 1: harmoniser on. Its per-frame decisions (which bins are peaks, is
    // this frame a transient) can flip on rounding noise in the host position,
    // so a frame may differ slightly; anything systematic would show in the rms.
    for (int pass = 0; pass < 2; ++pass)
    {
        std::vector<float> ref;
        for (int block : { 512, 1, 17, 64, 2048, 0 })
        {
            Session s (48000.0, 126.0, 2048);
            s.demo (5); s.preset (14);                        // Wire Cutter: lots going on
            s.proc->editBank ([] (PatternBank& b) { for (auto& q : b.seq) { for (auto& st : q.steps[splinter::laneCrush]) st.on = 0; q.locked[splinter::laneCrush] = 1; } }, SplinterProcessor::allBits);
            s.set (pid::harmony, pass == 0 ? 0.0f : 0.5f); s.set (pid::harmMode, 1.0f); s.set (pid::stretchMode, 3.0f); s.settle();
            s.renderBeats (block == 1 ? 6.0 : 12.0, block);
            if (ref.empty()) { ref = s.L; continue; }
            std::vector<float> diff (std::min (ref.size(), s.L.size()));
            double maxDiff = 0.0;
            for (size_t i = 0; i < diff.size(); ++i) { diff[i] = ref[i] - s.L[i]; maxDiff = std::max (maxDiff, (double) std::fabs (diff[i])); }
            note (juce::String::formatted ("block size %4d (%s): differs from the 512 render by max %.2e, rms %.2e", block, pass == 0 ? "harmony off" : "harmony on ", maxDiff, rms (diff)));
            if (pass == 0) CHECK_MSG (maxDiff < 2.0e-5, "block %d differs by %g", block, maxDiff);
            else CHECK_MSG (maxDiff < 5.0e-2 && rms (diff) < 5.0e-4, "block %d differs by max %g rms %g", block, maxDiff, rms (diff));
        }
    }
}

TEST_CASE (stable_at_all_sample_rates)
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        Session s (sr, 128.0, sr > 100000.0 ? 1024 : 256);
        s.demo (1); s.preset (22);                        // Choir Stack: harmony + reverb
        s.renderBeats (8.0, 0);
        s.preset (15); s.renderBeats (8.0, 0);            // Circuit Bent
        s.preset (7); s.renderBeats (8.0, 0);             // Half Moon
        const float peak = std::max (peakAbs (s.L), peakAbs (s.R));
        const double level = rms (s.L, (size_t) sr);
        note (juce::String::formatted ("sample rate %6.0f: latency %d samples (%.1f ms), peak %.3f, rms %.3f, cpu %.1f %%", sr, s.latency(), 1000.0 * s.latency() / sr, peak, level, s.cpuPercent()));
        CHECK (! hasNonFinite (s.L) && ! hasNonFinite (s.R));
        CHECK_MSG (peak <= 0.967f, "sr %.0f peak %g", sr, peak);
        CHECK_MSG (level > 0.01, "sr %.0f silent", sr);
    }
}

TEST_CASE (latency_is_reported_correctly)
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        // live mode with everything neutral must equal the input delayed by the reported latency
        Session s (sr, 120.0, 512);
        s.preset (presets::numFactory() - 1);
        s.set (pid::sourceMode, 1.0f); s.set (pid::limiter, 0.0f);
        splinter::Rng rng (3); std::vector<float> in;
        const int64_t n = (int64_t) sr * 2;
        for (int64_t i = 0; i < n; ++i) in.push_back (0.3f * rng.bipolar());
        s.render (n, 0, [&] (int64_t i, float& l, float& r) { l = r = in[(size_t) i]; });
        const int lat = s.latency();
        double err = 0.0;
        for (int64_t i = lat + 4000; i < n; ++i) err = std::max (err, (double) std::fabs (s.L[(size_t) i] - in[(size_t) (i - lat)]));
        note (juce::String::formatted ("latency @ %.0f Hz: %d samples reported, null-test residual %.1e", sr, lat, err));
        CHECK_MSG (err < 1.0e-4, "sr %.0f err %g", sr, err);
    }
}

TEST_CASE (bypass_parameter_changes_and_transport_are_click_free)
{
    Session s (48000.0, 120.0, 256);
    s.preset (presets::numFactory() - 1);
    s.audio (smoothPad (48000.0, 120.0, 8.0), 120.0);
    s.set (pid::harmony, 0.4f);
    auto pad = smoothPad (48000.0, 120.0, 8.0);
    auto input = [&] (int64_t i, float& l, float& r) { l = r = pad.ch[0][(size_t) (i % pad.length())]; };
    splinter::Rng rng (17);
    std::vector<int64_t> marks;
    for (int k = 0; k < 60; ++k)
    {
        s.render (4800 + rng.range (4800), 0, input);
        marks.push_back (s.pos);
        switch (k % 10)
        {
            case 0: s.set (pid::bypass, 1.0f); break;
            case 1: s.set (pid::bypass, 0.0f); break;
            case 2: s.set (pid::mix, rng.unit()); break;
            case 3: s.set (pid::timeSlot, (float) (1 + rng.range (12))); s.set (pid::volSlot, (float) (1 + rng.range (12))); break;
            case 4: s.set (pid::warp, rng.unit()); s.set (pid::width, 2.0f * rng.unit()); break;
            case 5: s.head.playing = false; break;
            case 6: s.head.playing = true; s.head.ppq = 4.0 * rng.range (8); break;
            case 7: s.set (pid::lpFreq, 400.0f + 15000.0f * rng.unit()); s.set (pid::hpFreq, 20.0f + 400.0f * rng.unit()); s.set (pid::outGain, -6.0f + 8.0f * rng.unit()); break;
            case 8: s.set (pid::seqSlot, (float) (1 + rng.range (4))); s.set (pid::harmony, rng.unit()); break;
            default: s.set (pid::timeSlot, 1.0f); s.set (pid::volSlot, 1.0f); s.set (pid::mix, 1.0f); s.set (pid::sourceMode, (float) (k / 10 % 2)); break;
        }
    }
    CHECK (! hasNonFinite (s.L));
    CHECK (peakAbs (s.L) <= 0.967f);
    const ClickResult c = detectClicks (s.L, s.sr, 12000);
    int step = 0; for (size_t i = 0; i < marks.size(); ++i) if (marks[i] + s.latency() <= (int64_t) c.position + 64) step = (int) i;
    note (juce::String::formatted ("bypass / parameter / transport torture: worst click score %.2f at sample %zu (%d samples after change #%d, kind %d)", c.worstRatio, c.position, (int) ((int64_t) c.position - marks[(size_t) step] - s.latency()), step, step % 10));
    CHECK_MSG (c.worstRatio < 9.0, "click ratio %.1f at %zu", c.worstRatio, c.position);
    save ("torture", s);
}

TEST_CASE (tempo_changes_stay_in_sync)
{
    Session s (48000.0, 120.0, 512);
    s.preset (presets::numFactory() - 1);
    s.set (pid::stretchMode, 3.0f);
    s.audio (clickLoop (48000.0, 120.0, 8.0), 120.0);
    // ramp the tempo; record where each beat should fall in output samples
    std::vector<int64_t> beatSamples;
    for (int k = 0; k < 3000; ++k)
    {
        s.head.bpm = k < 1000 ? 120.0 : (k < 2000 ? 120.0 + (k - 1000) * 0.03 : 150.0);
        const double before = s.head.ppq, after = before + 512.0 * s.head.bpm / (60.0 * 48000.0);
        const double kBeat = std::ceil (before - 1.0e-9);
        if (kBeat < after - 1.0e-9) beatSamples.push_back (s.pos + (int64_t) std::llround (std::max (0.0, kBeat - before) * 60.0 * 48000.0 / s.head.bpm));
        s.render (512);
        if (k % 20 == 0) juce::Thread::sleep (2);          // let the background re-render catch up now and then
    }
    int found = 0; double worst = 0.0;
    for (size_t b = 4; b + 2 < beatSamples.size(); ++b)
    {
        const int64_t ideal = beatSamples[b] + s.latency();
        if (ideal + 2000 >= (int64_t) s.L.size()) break;
        float pk = 0.0f; int64_t at = -1;
        for (int64_t i = ideal - 1200; i < ideal + 1200; ++i) pk = std::max (pk, std::fabs (s.L[(size_t) i]));
        for (int64_t i = ideal - 1200; i < ideal + 1200; ++i) if (std::fabs (s.L[(size_t) i]) > 0.25f * pk) { at = i; break; }
        if (pk > 0.08f && at >= 0) { ++found; worst = std::max (worst, (double) std::llabs (at - ideal)); }
    }
    note (juce::String::formatted ("tempo ramp 120 -> 150 BPM: %d beats checked, worst offset %.1f samples (%.2f ms)", found, worst, worst / 48.0));
    CHECK (found > 60);
    CHECK_MSG (worst < 60.0, "worst %.1f", worst);
    CHECK (! hasNonFinite (s.L));
}

TEST_CASE (state_round_trip_restores_everything)
{
    juce::MemoryBlock state;
    std::vector<float> a, b;
    {
        Session s (48000.0, 110.0, 512);
        s.audio (smoothPad (44100.0, 95.0, 8.0), 95.0);   // a "user" loop that only exists inside the state
        s.preset (24);                                     // Chord Stabs
        s.set (pid::stretchMode, 3.0f); s.settle();
        s.proc->editBank ([] (PatternBank& bk)
        {
            bk.sliceMode = 5; bk.slices.count = 3; bk.slices.startBeat[0] = 0.0f; bk.slices.startBeat[1] = 2.5f; bk.slices.startBeat[2] = 5.25f;
            bk.time[4].clear(); bk.time[4].add (0.0f, 0.1f); bk.time[4].add (1.0f, 0.3f, 0.5f); bk.time[4].setName ("Mine");
            bk.seq[5].steps[splinter::laneReverse][3].on = 1; bk.seq[5].steps[splinter::laneReverse][3].prob = 42; bk.seq[5].locked[2] = 1;
        }, SplinterProcessor::allBits);
        s.set (pid::seed, 321.0f); s.set (pid::chaos, 0.3f);
        s.proc->getStateInformation (state);
        s.renderBeats (16.0); a = s.L;
    }
    note (juce::String::formatted ("state size with an embedded 5 s stereo loop: %.1f kB", state.getSize() / 1024.0));
    {
        Session s (48000.0, 110.0, 512);
        s.settleKeepsSlices = true;
        s.proc->setStateInformation (state.getData(), (int) state.getSize());
        s.settle();
        const PatternBank bk = s.proc->getBank();
        CHECK (bk.slices.count == 3 && std::fabs (bk.slices.startBeat[1] - 2.5f) < 1.0e-3f && std::fabs (bk.slices.startBeat[2] - 5.25f) < 1.0e-3f);
        CHECK (bk.sliceMode == 5);
        CHECK (juce::String (bk.time[4].name) == "Mine" && bk.time[4].numNodes == 2 && std::fabs (bk.time[4].nodes[1].tension - 0.5f) < 1.0e-3f);
        CHECK (bk.seq[5].steps[splinter::laneReverse][3].on == 1 && bk.seq[5].steps[splinter::laneReverse][3].prob == 42 && bk.seq[5].locked[2] == 1);
        CHECK ((int) s.proc->getParam (pid::seed) == 321);
        CHECK (s.proc->getPresetName() == presets::factoryName (24));
        const LoopInfo info = s.proc->loops.getInfo();
        CHECK (info.kind == LoopInfo::capture && std::fabs (info.beats - 8.0) < 1.0e-6);
        s.renderBeats (16.0); b = s.L;
    }
    double maxDiff = 0.0;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i) maxDiff = std::max (maxDiff, (double) std::fabs (a[i] - b[i]));
    std::vector<float> diff (std::min (a.size(), b.size()));
    for (size_t i = 0; i < diff.size(); ++i) diff[i] = a[i] - b[i];
    note (juce::String::formatted ("state round trip: re-render differs by max %.2e, rms %.2e (the loop is stored as 24-bit FLAC)", maxDiff, rms (diff)));
    CHECK_MSG (maxDiff < 5.0e-2 && rms (diff) < 5.0e-4, "max diff %g rms %g", maxDiff, rms (diff));
}

TEST_CASE (midi_slices_and_live_capture)
{
    Session s (48000.0, 120.0, 256);
    s.preset (presets::numFactory() - 1);
    s.audio (clickLoop (48000.0, 120.0, 8.0), 120.0);
    s.set (pid::loopPlay, 0.0f);
    s.proc->setSliceMode (1, 0.5f);                        // 1/4 grid: slice n starts on beat n
    const int64_t t0 = 30000;
    s.midi.push_back ({ t0, true, 36 + 3, 1.0f }); s.midi.push_back ({ t0 + 12000, false, 36 + 3, 0.0f });
    s.render (60000, 0);
    CHECK (peakAbs (s.L, 0, (size_t) t0) < 1.0e-4f);
    // the slice starts with a hit: it must sound right at the note-on (+ latency)
    int64_t at = -1;
    for (int64_t i = t0; i < t0 + 8000; ++i) if (std::fabs (s.L[(size_t) i]) > 0.1f) { at = i; break; }
    note (juce::String::formatted ("MIDI slice trigger: note-on at %lld, audio at %lld, latency %d", (long long) t0, (long long) at, s.latency()));
    CHECK_MSG (at >= 0 && std::llabs (at - (t0 + s.latency())) < 96, "slice audio at %lld", (long long) at);
    CHECK (peakAbs (s.L, (size_t) (t0 + 12000 + s.latency() + 2000), 60000) < 1.0e-4f);

    // live: feed two bars of a pad, capture them, switch to loop mode and hear them
    Session live (48000.0, 120.0, 256);
    live.preset (presets::numFactory() - 1);
    live.set (pid::sourceMode, 1.0f); live.set (pid::liveBars, 2.0f);
    auto pad = smoothPad (48000.0, 120.0, 8.0);
    auto input = [&] (int64_t i, float& l, float& r) { l = r = pad.ch[0][(size_t) (i % pad.length())]; };
    live.renderBeats (10.0);
    live.L.clear(); live.R.clear();
    live.render ((int64_t) (live.spb() * 10.0), -1, input);
    CHECK (rms (live.L, 48000) > 0.05);                    // live input passes through
    live.proc->captureLiveInput(); live.settle();
    const LoopInfo info = live.proc->loops.getInfo();
    CHECK_MSG (info.kind == LoopInfo::capture && std::fabs (info.beats - 8.0) < 1.0e-6, "capture kind %d beats %.2f", (int) info.kind, info.beats);
    CHECK ((int) live.proc->getParam (pid::sourceMode) == 0);
    live.L.clear(); live.R.clear();
    live.renderBeats (8.0);                                // no input now: the captured loop plays
    CHECK_MSG (rms (live.L, 48000) > 0.05, "captured loop rms %g", rms (live.L, 48000));
    const ClickResult c = detectClicks (live.L, 48000.0, 12000);
    CHECK_MSG (c.worstRatio < 9.0, "captured loop click ratio %.1f", c.worstRatio);
}

TEST_CASE (chaos_is_repeatable_per_seed)
{
    std::vector<float> a, b, c;
    for (int pass = 0; pass < 3; ++pass)
    {
        Session s (48000.0, 124.0, 512);
        s.demo (5); s.preset (0);
        s.set (pid::chaos, 0.7f); s.set (pid::seed, pass == 2 ? 99.0f : 42.0f); s.set (pid::stretchMode, 3.0f); s.settle();
        s.renderBeats (16.0);
        (pass == 0 ? a : (pass == 1 ? b : c)) = s.L;
    }
    double same = 0.0, other = 0.0;
    for (size_t i = 0; i < a.size(); ++i) { same = std::max (same, (double) std::fabs (a[i] - b[i])); other = std::max (other, (double) std::fabs (a[i] - c[i])); }
    note (juce::String::formatted ("chaos: same seed differs by %.1e, different seed by %.2f", same, other));
    CHECK (same < 1.0e-6);
    CHECK (other > 0.05);
}



int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const char* filter = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--out" && i + 1 < argc) { outDir = argv[++i]; juce::File (outDir).createDirectory(); }
        else filter = argv[i];
    }
    const int rc = testutil::runAll (filter);
    if (outDir.isNotEmpty())
    {
        juce::String text = "Splinter render report\n======================\n\n";
        for (auto& l : report) text << l << "\n";
        text << "\nresult: " << (rc == 0 ? "PASS" : "FAIL") << "\n";
        juce::File (outDir).getChildFile ("report.txt").replaceWithText (text);
    }
    return rc;
}
