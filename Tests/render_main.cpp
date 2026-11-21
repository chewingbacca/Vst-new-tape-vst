// Offline render harness for Swoon. Renders test signals through the DSP
// engine to WAV and prints metrics as JSON lines for tools/analyze.py.
//
//   swoon_render render --out out.wav [--signal strum|impulse|sweep|sine:440|noise|bursts:220|silence]
//                       [--sr 48000] [--block 256] [--seconds 10] [--tail 4] [--level -12]
//                       [--preset "Name"|index] [--solo fuzz|glide|...] [--bare] [--set id=value]...
//                       [--write-input in.wav]
//   swoon_render list-presets
//   swoon_render list-params
//   swoon_render bench [--preset ...] [--sr 48000] [--block 256]
#include "TestSignals.h"

#include "dsp/Engine.h"
#include "dsp/Presets.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <xmmintrin.h>

using namespace swoon;
using testsig::Stereo;

namespace
{
struct Options
{
    std::string command, out, signal = "strum", preset, solo, writeInput;
    double sr = 48000.0, seconds = 10.0, tail = 4.0;
    int block = 256;
    float levelDb = -12.0f;
    bool bare = false;
    std::vector<std::pair<std::string, float>> sets;
};

const std::map<std::string, P>& moduleSwitches()
{
    static const std::map<std::string, P> m {
        { "fuzz", P::fuzzOn }, { "glide", P::glideOn }, { "reverse", P::revOn }, { "focus", P::focOn },
        { "mod", P::modOn }, { "wash", P::washOn }, { "amp", P::ampOn }, { "tape", P::tapeOn },
    };
    return m;
}

bool findPreset (const std::string& key, ParamValues& pv, std::string& nameOut)
{
    const auto& presets = factoryPresets();
    char* end = nullptr;
    const long idx = std::strtol (key.c_str(), &end, 10);
    if (end != key.c_str() && *end == 0 && idx >= 0 && idx < static_cast<long> (presets.size()))
    {
        pv = presetValues (presets[static_cast<size_t> (idx)]);
        nameOut = presets[static_cast<size_t> (idx)].name;
        return true;
    }
    for (const auto& p : presets)
        if (key == p.name)
        {
            pv = presetValues (p);
            nameOut = p.name;
            return true;
        }
    return false;
}

bool buildParams (const Options& o, ParamValues& pv, std::string& presetName)
{
    presetName = "(defaults)";
    if (! o.preset.empty() && ! findPreset (o.preset, pv, presetName))
    {
        std::fprintf (stderr, "unknown preset: %s\n", o.preset.c_str());
        return false;
    }

    if (o.bare || ! o.solo.empty())
    {
        for (const auto& [name, param] : moduleSwitches())
            pv[param] = (name == o.solo) ? 1.0f : 0.0f;
        // neutral macros, no global colouring
        for (P m : { P::wall, P::wash, P::glide, P::blur, P::warmth })
            pv[m] = 0.5f;
        pv[P::mix] = 1.0f;
    }

    for (const auto& [id, value] : o.sets)
    {
        const P p = paramFromId (id);
        if (p == P::Count)
        {
            std::fprintf (stderr, "unknown parameter: %s\n", id.c_str());
            return false;
        }
        pv[p] = value;
    }
    return true;
}

Stereo makeSignal (const Options& o)
{
    const float level = testsig::dbToLin (o.levelDb);
    const std::string kind = o.signal.substr (0, o.signal.find (':'));
    const double arg = o.signal.find (':') == std::string::npos ? 0.0 : std::atof (o.signal.substr (o.signal.find (':') + 1).c_str());

    Stereo s;
    if (kind == "file")
    {
        // --signal file:/path/to/input.wav  (must match --sr)
        if (! testsig::readWav (o.signal.substr (5), s))
            std::fprintf (stderr, "could not read %s\n", o.signal.substr (5).c_str());
        s.sampleRate = o.sr;
    }
    else if (kind == "strum")        s = testsig::guitarPhrase (o.sr, o.seconds);
    else if (kind == "impulse") s = testsig::impulse (o.sr, o.seconds, level);
    else if (kind == "sweep")   s = testsig::sweep (o.sr, o.seconds, 20.0, 20000.0, level);
    else if (kind == "sine")    s = testsig::sine (o.sr, o.seconds, arg > 0.0 ? arg : 440.0, level);
    else if (kind == "noise")   s = testsig::noise (o.sr, o.seconds, level);
    else if (kind == "decay")   s = testsig::decayingNote (o.sr, o.seconds, arg > 0.0 ? arg : 196.0, level);
    else if (kind == "bursts")  s = testsig::bursts (o.sr, o.seconds, arg > 0.0 ? arg : 220.0, level);
    else                        s = testsig::silence (o.sr, o.seconds);

    if (kind == "strum" && o.levelDb != -12.0f)
    {
        // --level sets the strum's peak (default peak is -6 dBFS)
        const float g = testsig::dbToLin (o.levelDb + 6.0f);
        for (size_t i = 0; i < s.size(); ++i) { s.l[i] *= g; s.r[i] *= g; }
    }

    s.resize (s.size() + static_cast<size_t> (o.tail * o.sr));
    return s;
}

void processAll (Engine& engine, Stereo& s, const ParamValues& pv, int block)
{
    for (size_t pos = 0; pos < s.size(); pos += static_cast<size_t> (block))
    {
        const int n = static_cast<int> (std::min (static_cast<size_t> (block), s.size() - pos));
        engine.process (s.l.data() + pos, s.r.data() + pos, n, pv);
    }
}

int cmdRender (const Options& o)
{
    ParamValues pv;
    std::string presetName;
    if (! buildParams (o, pv, presetName))
        return 1;

    Stereo s = makeSignal (o);
    if (! o.writeInput.empty())
        testsig::writeWav (o.writeInput, s);
    const auto inM = testsig::measure (s);

    Engine engine;
    engine.prepare (o.sr, o.block);
    const auto t0 = std::chrono::steady_clock::now();
    processAll (engine, s, pv, o.block);
    const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

    if (! o.out.empty() && ! testsig::writeWav (o.out, s))
    {
        std::fprintf (stderr, "could not write %s\n", o.out.c_str());
        return 1;
    }

    const auto m = testsig::measure (s);
    const double duration = static_cast<double> (s.size()) / o.sr;
    std::printf ("{\"out\":\"%s\",\"preset\":\"%s\",\"signal\":\"%s\",\"sr\":%.0f,\"block\":%d,\"latency\":%d,"
                 "\"inPeakDb\":%.2f,\"inRmsDb\":%.2f,\"peakDb\":%.2f,\"rmsDb\":%.2f,\"dc\":%.6f,\"tailRmsDb\":%.1f,"
                 "\"maxStep\":%.4f,\"corr\":%.3f,\"bad\":%ld,\"overs\":%ld,\"cpuPct\":%.2f}\n",
                 o.out.c_str(), presetName.c_str(), o.signal.c_str(), o.sr, o.block, engine.getLatencySamples(),
                 testsig::toDb (inM.peak), testsig::toDb (inM.rms), testsig::toDb (m.peak), testsig::toDb (m.rms), m.dc,
                 testsig::toDb (m.tailRms), m.maxStep, m.corr, m.badSamples, m.overs, 100.0 * elapsed / duration);
    return m.badSamples == 0 ? 0 : 2;
}

int cmdBench (const Options& o)
{
    ParamValues pv;
    std::string presetName;
    if (! buildParams (o, pv, presetName))
        return 1;

    Options sig = o;
    sig.signal = "strum";
    sig.seconds = 10.0;
    sig.tail = 0.0;
    const Stereo src = makeSignal (sig);

    Engine engine;
    engine.prepare (o.sr, o.block);
    double best = 1.0e9;
    for (int pass = 0; pass < 4; ++pass)
    {
        Stereo s = src;
        const auto t0 = std::chrono::steady_clock::now();
        processAll (engine, s, pv, o.block);
        best = std::min (best, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
    }
    std::printf ("{\"bench\":\"%s\",\"sr\":%.0f,\"block\":%d,\"cpuPct\":%.2f}\n", presetName.c_str(), o.sr, o.block,
                 100.0 * best / 10.0);
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    // match the plugin's ScopedNoDenormals
    _mm_setcsr (_mm_getcsr() | 0x8040);

    if (argc < 2)
    {
        std::puts ("usage: swoon_render render|bench|list-presets|list-params [options]  (see Tests/render_main.cpp)");
        return 1;
    }

    Options o;
    o.command = argv[1];

    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto next = [&] () -> std::string { return i + 1 < argc ? argv[++i] : ""; };

        if (a == "--out") o.out = next();
        else if (a == "--signal") o.signal = next();
        else if (a == "--sr") o.sr = std::atof (next().c_str());
        else if (a == "--block") o.block = std::max (1, std::atoi (next().c_str()));
        else if (a == "--seconds") o.seconds = std::atof (next().c_str());
        else if (a == "--tail") o.tail = std::atof (next().c_str());
        else if (a == "--level") o.levelDb = static_cast<float> (std::atof (next().c_str()));
        else if (a == "--preset") o.preset = next();
        else if (a == "--solo") o.solo = next();
        else if (a == "--bare") o.bare = true;
        else if (a == "--write-input") o.writeInput = next();
        else if (a == "--set")
        {
            const std::string kv = next();
            const auto eq = kv.find ('=');
            if (eq == std::string::npos)
            {
                std::fprintf (stderr, "--set expects id=value\n");
                return 1;
            }
            o.sets.emplace_back (kv.substr (0, eq), static_cast<float> (std::atof (kv.substr (eq + 1).c_str())));
        }
        else
        {
            std::fprintf (stderr, "unknown option: %s\n", a.c_str());
            return 1;
        }
    }

    if (o.command == "render")
        return cmdRender (o);
    if (o.command == "bench")
        return cmdBench (o);
    if (o.command == "list-presets")
    {
        int i = 0;
        for (const auto& p : factoryPresets())
            std::printf ("%d\t%s\t%s\n", i++, p.category, p.name);
        return 0;
    }
    if (o.command == "list-params")
    {
        for (const auto& d : paramDefs())
            std::printf ("%s\t%s\t%g\t%g\t%g\t%s\n", d.id, d.name, d.min, d.max, d.def, d.choices);
        return 0;
    }

    std::fprintf (stderr, "unknown command: %s\n", o.command.c_str());
    return 1;
}
