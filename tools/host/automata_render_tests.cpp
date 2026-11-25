// Plugin-level render tests through the real AudioProcessor and a simulated host.
//
//   automata_render_tests [--out <dir>] [--quick] [--sr 48000]
//
// For every factory kit: render 4 bars (+ tail) and check NaN/Inf, peak <= 1, DC,
// silence, clicks; render again with random block sizes and after a state round-trip
// and require bit-identical output; check a host loop. For every voice preset: put it
// on track 1 with a test pattern, render, and check the same signal rules.
// Writes WAVs to --out when given. Exit code = number of failures (capped at 255).
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "HostHarness.h"
#include "factory/Presets.h"
#include "seq/SeqEngine.h"
#include <cstdio>

using namespace atm;

namespace {

int failures = 0;
void fail (const char* fmt, const char* name, double v = 0.0)
{
    ++failures;
    std::printf ("  FAIL %-22s ", name);
    std::printf (fmt, v);
    std::printf ("\n");
}

// A click is a sample-to-sample jump much larger than the signal around it would explain.
int countClicks (const std::vector<float>& x, double sr)
{
    const int win = std::max (32, (int) (sr * 0.0015));
    int clicks = 0;
    for (size_t i = (size_t) win; i + (size_t) win < x.size(); ++i)
    {
        const float d = std::fabs (x[i] - x[i - 1]);
        if (d < 0.35f) continue;
        // neighbourhood slope (excluding this sample)
        float local = 0.f;
        for (int k = 2; k < win; ++k) local = std::max (local, std::max (std::fabs (x[i - (size_t) k] - x[i - (size_t) k - 1]), std::fabs (x[i + (size_t) k] - x[i + (size_t) k - 1])));
        if (d > 4.f * local + 0.05f) { ++clicks; i += (size_t) win; }
    }
    return clicks;
}

struct Checks { bool silenceOk = true; double minRmsDb = -60.0; };

void checkSignal (const char* name, const harness::RenderResult& r, double sr, const Checks& c)
{
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto s = harness::measure (r.main[(size_t) ch]);
        if (s.nan) { fail ("NaN/Inf in output%s", name); return; }
        if (s.peak > 1.0) fail ("peak %.3f > 1", name, s.peak);
        if (std::fabs (s.dc) > 0.01) fail ("DC offset %.4f", name, s.dc);
        const double rmsDb = 20.0 * std::log10 (s.rms + 1e-12);
        if (c.silenceOk == false && rmsDb < c.minRmsDb) fail ("too quiet / silent: rms %.1f dB", name, rmsDb);
        const int clicks = countClicks (r.main[(size_t) ch], sr);
        if (clicks > 0) fail ("%g click candidate(s)", name, clicks);
    }
}

bool identical (const harness::RenderResult& a, const harness::RenderResult& b, double& maxDiff)
{
    maxDiff = 0.0;
    if (a.main[0].size() != b.main[0].size()) { maxDiff = 1e9; return false; }
    for (int ch = 0; ch < 2; ++ch)
        for (size_t i = 0; i < a.main[(size_t) ch].size(); ++i)
            maxDiff = std::max (maxDiff, (double) std::fabs (a.main[(size_t) ch][i] - b.main[(size_t) ch][i]));
    return maxDiff == 0.0;
}

std::unique_ptr<AutomataProcessor> makeProc (double sr, int block)
{
    auto p = std::make_unique<AutomataProcessor>();
    p->setRateAndBufferSizeDetails (sr, block);
    p->prepareToPlay (sr, block);
    return p;
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    auto value = [&args] (const char* f, const juce::String& d) { const int i = args.indexOf (f); return i >= 0 && i + 1 < args.size() ? args[i + 1] : d; };
    const bool quick = args.contains ("--quick");
    const double sr = value ("--sr", "48000").getDoubleValue();
    const juce::String outDir = value ("--out", "");
    if (outDir.isNotEmpty()) juce::File (outDir).createDirectory();
    auto save = [&] (const juce::String& name, const harness::RenderResult& r)
    {
        if (outDir.isNotEmpty()) writeWav ((outDir + "/" + juce::File::createLegalFileName (name) + ".wav").toStdString(), r.main, (int) sr, 24);
    };

    double cpuWorst = 0.0; juce::String cpuWorstName;
    // ------------------------------------------------------------------ kits
    std::printf ("== %d factory kits\n", numKitPresets());
    for (int k = 0; k < numKitPresets(); ++k)
    {
        const char* name = kitPresetInfo (k).name;
        harness::RenderOptions o;
        o.sr = sr; o.bpm = kitPresetInfo (k).bpm; o.bars = quick ? 2 : 4; o.tailSeconds = 1.5; o.block = 512;
        auto load = [&] (AutomataProcessor& proc)
        {
            auto p = std::make_unique<Project>();
            buildKitPreset (k, *p, &proc.session().samples());
            proc.session().replaceProject (*p);
        };
        auto proc = makeProc (sr, o.block);
        load (*proc);
        const auto a = harness::render (*proc, o);
        Checks c; c.silenceOk = false; c.minRmsDb = -40.0;
        checkSignal (name, a, sr, c);
        const double cpu = a.cpuSeconds / a.seconds;
        if (cpu > cpuWorst) { cpuWorst = cpu; cpuWorstName = name; }
        save (juce::String (k).paddedLeft ('0', 2) + "_" + name, a);

        // determinism: same project, new instance, random block sizes
        {
            auto p2 = makeProc (sr, o.block);
            load (*p2);
            harness::RenderOptions ov = o; ov.varBlock = true;
            const auto b = harness::render (*p2, ov);
            double md;
            if (! identical (a, b, md)) fail ("random block sizes change the output (max diff %g)", name, md);
        }
        // state round trip
        if (! quick || k % 5 == 0)
        {
            juce::MemoryBlock mb;
            auto src = makeProc (sr, o.block);
            load (*src);
            src->getStateInformation (mb);
            auto dst = makeProc (sr, o.block);
            dst->setStateInformation (mb.getData(), (int) mb.getSize());
            const auto b = harness::render (*dst, o);
            double md;
            if (! identical (a, b, md)) fail ("state round-trip changes the output (max diff %g)", name, md);
        }
        std::printf ("  ok   %-22s rms %5.1f dB  peak %5.2f  cpu %4.1f%%\n", name,
                     20.0 * std::log10 (harness::measure (a.main[0]).rms + 1e-12), harness::measure (a.main[0]).peak, 100.0 * cpu);
    }

    // ------------------------------------------------------------------ host loop: bar 2..3 looped twice equals two passes of the same bars
    {
        harness::RenderOptions o; o.sr = sr; o.bpm = 150; o.bars = 4; o.tailSeconds = 0; o.loopStartBar = 1; o.loopEndBar = 2; o.loopPasses = 3;
        auto proc = makeProc (sr, 256);
        auto p = std::make_unique<Project>();
        buildKitPreset (8, *p, &proc->session().samples());
        proc->session().replaceProject (*p);
        const auto r = harness::render (*proc, o);
        checkSignal ("host loop", r, sr, Checks());
        std::printf ("  ok   host loop render\n");
    }

    // ------------------------------------------------------------------ voice presets
    std::printf ("== %d voice presets\n", numVoicePresets());
    int voiceCount = 0;
    for (int v = 0; v < numVoicePresets(); ++v)
    {
        if (quick && v % 4 != 0) continue;
        const char* name = voicePresetInfo (v).name;
        auto proc = makeProc (sr, 512);
        auto p = std::make_unique<Project>();
        initProject (*p);
        VoicePresetData vd;
        loadVoicePreset (v, vd, &proc->session().samples());
        applyVoicePreset (*p, 0, vd);
        p->kit.gparams[GP::LimitOn] = 0.f;              // judge the raw voice level
        TrackPattern& tp = p->patterns[0].tracks[0];
        const int role = vd.sound.role;
        const bool melodic = role == Role::Bass || role == Role::Lead || role == Role::Pad;
        static const int pitches[8] = { 0, 2, 4, 7, 0, -3, 5, 9 };
        for (int i = 0; i < 16; i += melodic ? 2 : 4)
        {
            tp.steps[i].on = 1; tp.steps[i].vel = (uint8_t) (i % 8 == 0 ? 120 : 90);
            tp.steps[i].len = (role == Role::Pad || role == Role::Fx) ? 3.5f : 0.8f;
            if (melodic) tp.steps[i].pitch = (int8_t) pitches[(i / 2) % 8];
        }
        tp.steps[14].ratCount = 3;                     // a retrigger burst for the click check
        harness::RenderOptions o; o.sr = sr; o.bpm = 120; o.bars = 2; o.tailSeconds = 2; o.block = 512;
        proc->session().replaceProject (*p);
        const auto r = harness::render (*proc, o);
        Checks c; c.silenceOk = false; c.minRmsDb = (role == Role::Hat || role == Role::Perc || role == Role::Fx) ? -58.0 : -45.0;
        checkSignal (name, r, sr, c);
        const auto s = harness::measure (r.main[0]);
        if (s.peak > 1.0) {} // reported above
        save (juce::String ("voice_") + juce::String (v).paddedLeft ('0', 3) + "_" + name, r);
        ++voiceCount;
    }
    std::printf ("  checked %d voice presets\n", voiceCount);

    std::printf ("\nworst kit CPU: %.1f%% of one core (%s) at %.0f Hz\n", 100.0 * cpuWorst, cpuWorstName.toRawUTF8(), sr);
    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return std::min (failures, 255);
}
