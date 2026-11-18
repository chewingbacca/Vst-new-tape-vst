// verdant_render — offline renderer / verification harness for the Verdant DSP engine.
//
//   verdant_render --signal phrase --preset "Greenhouse Dawn" --out renders
//   verdant_render --suite --out renders        (full verification matrix, non-zero exit on failure)
//
// See README.md ("Testing") for the checks performed.
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "TestSignals.h"
#include "dsp/Engine.h"
#include "dsp/Presets.h"

using namespace verdant;
using vtest::Stereo;

namespace
{

constexpr double kCeiling = 0.9665; // limiter ceiling (-0.3 dBFS) + rounding

struct Job
{
    std::string name, signal = "phrase", solo = "none";
    int preset = 0;
    double sr = 48000.0, seconds = 14.0, bpm = 120.0;
    int block = 256;
    bool varBlock = false, automate = false, playing = true, writeWav = true;
    std::vector<std::pair<int, float>> sets;
    struct Event { double time; int pid; float value; };
    std::vector<Event> events;                 // parameter changes at a given time (seconds)
    bool expectSilence = false, allowSustain = false, expectHold = false, cyclePresets = false;
};

struct Result
{
    Job job;
    vtest::Stats stats;
    double cpuPercent = 0.0, growthDb = 0.0, endDb = 0.0, limiterDb = 0.0, relLevelDb = 0.0, maxStep = 0.0;
    std::vector<std::string> failures;
};

bool applySolo (ParamValues& p, const std::string& solo)
{
    if (solo == "none") return true;
    const std::map<std::string, PID> mods = { { "tape", pTapeOn }, { "grain", pGrainOn }, { "arp", pArpOn },
                                              { "chorus", pChorusOn }, { "reverb", pRevOn } };
    if (solo != "dry" && mods.find (solo) == mods.end()) return false;
    for (const auto& m : mods)
        p[m.second] = (m.first == solo) ? 1.0f : 0.0f;
    p[pMix] = 1.0f;
    p[pOutDuck] = 0.0f;
    // neutral macros so the module is measured on its own parameters
    p[pBloom] = p[pGrowth] = p[pMoisture] = p[pSunlight] = p[pRoots] = 0.5f;
    if (solo == "grain") p[pGrainMix] = 1.0f;
    if (solo == "arp")   { p[pArpMix] = 1.0f; p[pArpWash] = 0.0f; }
    if (solo == "chorus") p[pChorusMix] = 0.5f;
    if (solo == "reverb") p[pRevMix] = 1.0f;
    return true;
}

// RMS in dB of [t0, t1) seconds.
double windowDb (const Stereo& s, double sr, double t0, double t1)
{
    const size_t a = std::min (s.size(), size_t (std::max (0.0, t0) * sr));
    const size_t b = std::min (s.size(), size_t (std::max (0.0, t1) * sr));
    if (b <= a) return -240.0;
    double sq = 0.0;
    for (size_t i = a; i < b; ++i)
        sq += double (s.l[i]) * s.l[i] + double (s.r[i]) * s.r[i];
    return vtest::toDb (std::sqrt (sq / double (2 * (b - a))));
}

Result runJob (const Job& job, const std::string& outDir)
{
    Result res;
    res.job = job;

    const auto& presets = factoryPresets();
    ParamValues base = presets[size_t (std::clamp (job.preset, 0, int (presets.size()) - 1))].resolve();
    applySolo (base, job.solo);
    for (const auto& s : job.sets)
        base[s.first] = s.second;

    Stereo sig = vtest::makeSignal (job.signal, job.sr, job.seconds);
    const double inputEnd = (job.signal == "phrase" || job.signal == "sweep") ? 4.5 : 3.0;
    const double dryDb = windowDb (sig, job.sr, 0.05, inputEnd);
    size_t nextEvent = 0;
    float minLim = 1.0f;

    Engine engine;
    engine.prepare (job.sr, 8192);

    Rng rng;
    rng.seed (4242);
    Transport tr;
    tr.bpm = job.bpm;
    tr.playing = job.playing;

    ParamValues cur = base;
    const size_t total = sig.size();
    size_t pos = 0;
    const auto t0 = std::chrono::steady_clock::now();

    while (pos < total)
    {
        int n = job.block;
        if (job.varBlock)
            n = 1 + rng.irange (rng.chance (0.1f) ? 4096 : 300);
        n = int (std::min (size_t (n), total - pos));

        if (job.automate)
        {
            // Brutal automation: jump several random parameters every block.
            for (int k = 0; k < 6; ++k)
            {
                const int pid = rng.irange (kNumParams);
                // bypass passes the input straight through, so it is only exercised on sane input
                if (pid == pBypass && (job.signal == "hostile" || ! rng.chance (0.05f))) continue;
                cur[pid] = fromNormalised (kParams[pid], rng.uni());
            }
        }
        while (nextEvent < job.events.size() && double (pos) >= job.events[nextEvent].time * job.sr)
        {
            cur[job.events[nextEvent].pid] = job.events[nextEvent].value;
            ++nextEvent;
        }
        if (job.cyclePresets)
        {
            // jump to the next factory preset every 1.5 s
            const int idx = int (double (pos) / job.sr / 1.5) % int (presets.size());
            const float byp = cur[pBypass];
            cur = presets[size_t (idx)].resolve();
            cur[pBypass] = byp;
        }

        engine.setParams (cur);
        tr.ppq = double (pos) / job.sr * job.bpm / 60.0;
        engine.process (sig.l.data() + pos, sig.r.data() + pos, n, tr);
        pos += size_t (n);
        minLim = std::min (minLim, engine.takeMinLimiterGain());
    }

    const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    res.cpuPercent = 100.0 * elapsed / job.seconds;
    res.stats = vtest::analyse (sig, job.sr);

    res.limiterDb = vtest::toDb (double (minLim));
    res.relLevelDb = windowDb (sig, job.sr, 0.05, inputEnd) - dryDb;
    for (size_t i = 1; i < sig.size(); ++i)
        res.maxStep = std::max (res.maxStep, double (std::abs (sig.l[i] - sig.l[i - 1])));

    // Tail behaviour: compare level just after the input ends with the end of the render.
    const double early = windowDb (sig, job.sr, inputEnd, inputEnd + 1.0);
    const double late  = windowDb (sig, job.sr, job.seconds - 1.0, job.seconds);
    res.growthDb = late - early;
    res.endDb = late;

    // ---- checks
    auto fail = [&res] (const std::string& m) { res.failures.push_back (m); };
    if (res.stats.bad > 0) fail ("non-finite samples");
    if (res.stats.peak > kCeiling) fail ("peak above limiter ceiling");
    if (job.expectSilence)
    {
        if (res.stats.peak > 1.0e-5) fail ("output not silent for silent input");
    }
    else if (! job.automate && job.signal != "hostile")
    {
        if (std::abs (res.stats.dc) > 0.003) fail ("DC offset");
        if (vtest::toDb (res.stats.rms) < -70.0) fail ("output unexpectedly silent");
        // (the "sine" test signal never stops, so there is no tail to judge)
        if (job.signal != "sine" && res.growthDb > 3.0 && late > -50.0) fail ("tail grows after input stops (runaway)");
        if (! job.allowSustain && job.seconds >= 12.0 && late > -20.0 && job.signal != "sine")
            fail ("tail still loud at end of render");
        if (job.expectHold && late < windowDb (sig, job.sr, 6.0, 7.0) - 6.0)
            fail ("frozen tail did not hold");
    }

    if (job.writeWav && ! outDir.empty())
        vtest::writeWav (outDir + "/" + job.name + ".wav", sig, job.sr);

    return res;
}

std::string jsonLine (const Result& r)
{
    std::ostringstream o;
    o.setf (std::ios::fixed);
    o.precision (3);
    o << "{\"name\":\"" << r.job.name << "\",\"signal\":\"" << r.job.signal << "\",\"preset\":\""
      << factoryPresets()[size_t (r.job.preset)].name << "\",\"solo\":\"" << r.job.solo << "\",\"sr\":" << r.job.sr
      << ",\"block\":" << r.job.block << ",\"peak_db\":" << vtest::toDb (r.stats.peak)
      << ",\"rms_db\":" << vtest::toDb (r.stats.rms) << ",\"dc\":";
    o.precision (6);
    o << r.stats.dc;
    o.precision (3);
    o << ",\"end_db\":" << r.endDb << ",\"growth_db\":" << r.growthDb << ",\"corr\":" << r.stats.correlation
      << ",\"rel_db\":" << r.relLevelDb << ",\"limiter_db\":" << r.limiterDb
      << ",\"max_step\":" << r.maxStep << ",\"bad\":" << r.stats.bad << ",\"cpu_pct\":" << r.cpuPercent << ",\"ok\":" << (r.failures.empty() ? "true" : "false")
      << ",\"failures\":[";
    for (size_t i = 0; i < r.failures.size(); ++i)
        o << (i ? "," : "") << "\"" << r.failures[i] << "\"";
    o << "]}";
    return o.str();
}

std::string slug (std::string s)
{
    for (auto& c : s)
        c = std::isalnum (static_cast<unsigned char> (c)) ? char (std::tolower (c)) : '_';
    return s;
}

bool presetSustains (const ParamValues& p)
{
    return p[pRevFreeze] >= 0.5f || p[pRevDecay] > 25.0f || p[pGrainFeedback] > 0.6f || p[pArpHold] > 0.9f;
}

std::vector<Job> buildSuite (bool quick)
{
    std::vector<Job> jobs;
    const auto& presets = factoryPresets();

    // 1. every preset x every musical signal
    const char* sigs[] = { "impulse", "sweep", "noise", "phrase", "pluck", "voice" };
    for (int p = 0; p < int (presets.size()); ++p)
    {
        const ParamValues pv = presets[size_t (p)].resolve();
        for (const char* s : sigs)
        {
            if (quick && std::string (s) != "phrase" && std::string (s) != "impulse") continue;
            Job j;
            j.preset = p; j.signal = s;
            j.seconds = presetSustains (pv) ? 30.0 : 20.0;
            j.allowSustain = presetSustains (pv);
            j.name = "preset_" + slug (presets[size_t (p)].name) + "__" + s;
            jobs.push_back (j);
        }
        // silence in -> silence out
        Job q; q.preset = p; q.signal = "silence"; q.seconds = 4.0; q.expectSilence = true; q.writeWav = false;
        q.name = "silence_" + slug (presets[size_t (p)].name);
        jobs.push_back (q);
        // hostile input must still be bounded
        Job h; h.preset = p; h.signal = "hostile"; h.seconds = 8.0; h.writeWav = false;
        h.name = "hostile_" + slug (presets[size_t (p)].name);
        jobs.push_back (h);
    }

    // 2. each module on its own (default preset values)
    for (const char* m : { "dry", "tape", "grain", "arp", "chorus", "reverb" })
        for (const char* s : sigs)
        {
            Job j; j.solo = m; j.signal = s; j.seconds = 16.0;
            j.name = std::string ("solo_") + m + "__" + s;
            jobs.push_back (j);
        }

    // 3. sample rates and block sizes
    for (double sr : { 44100.0, 88200.0, 96000.0, 192000.0 })
        for (int p = 0; p < int (presets.size()); p += (quick ? 7 : 3))
        {
            Job j; j.preset = p; j.sr = sr; j.signal = "phrase"; j.seconds = 12.0; j.writeWav = false;
            j.allowSustain = true;
            j.name = "sr" + std::to_string (int (sr)) + "_" + slug (presets[size_t (p)].name);
            jobs.push_back (j);
        }
    for (int block : { 1, 7, 32, 33, 64, 480, 4096 })
    {
        Job j; j.block = block; j.signal = "phrase"; j.seconds = 10.0; j.writeWav = false; j.allowSustain = true;
        j.name = "block" + std::to_string (block);
        jobs.push_back (j);
    }

    // 4. random block sizes + brutal parameter automation, at several rates
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const char* s : { "phrase", "hostile" })
        {
            Job j; j.sr = sr; j.signal = s; j.seconds = quick ? 10.0 : 30.0; j.varBlock = true; j.automate = true;
            j.writeWav = (sr == 48000.0 && std::string (s) == "phrase");
            j.name = "automation_" + std::to_string (int (sr)) + "_" + s;
            jobs.push_back (j);
        }

    // 5. extreme corners: everything maxed, infinite decay + max shimmer + max feedback
    {
        Job j; j.signal = "phrase"; j.seconds = 40.0; j.allowSustain = true; j.name = "extreme_everything_max";
        for (int i = 0; i < kNumParams; ++i)
            if (kParams[i].type == PType::Float && i != pOutGain)
                j.sets.push_back ({ i, kParams[i].max });
        j.sets.push_back ({ pOutGain, 12.0f });
        jobs.push_back (j);

        Job k; k.signal = "noise"; k.seconds = 60.0; k.allowSustain = true; k.name = "extreme_infinite_shimmer";
        k.sets = { { pRevDecay, 61.0f }, { pRevShimmer, 1.0f }, { pRevMix, 1.0f }, { pGrainFeedback, 1.0f },
                   { pBloom, 1.0f }, { pMoisture, 1.0f }, { pSunlight, 1.0f }, { pMix, 1.0f } };
        jobs.push_back (k);

        // freeze engaged mid-phrase must hold its level for a minute
        Job f; f.signal = "phrase"; f.seconds = 60.0; f.allowSustain = true; f.expectHold = true; f.name = "extreme_freeze";
        f.sets = { { pRevMix, 1.0f }, { pMix, 1.0f } };
        f.events = { { 3.0, pRevFreeze, 1.0f } };
        jobs.push_back (f);

        // walking through every preset while audio is running
        for (const char* sgl : { "sine", "phrase" })
        {
            Job c; c.signal = sgl; c.seconds = 1.5 * double (factoryPresets().size()) * 2.0; c.allowSustain = true;
            c.cyclePresets = true; c.varBlock = true;
            c.name = std::string ("preset_cycle_") + sgl;
            jobs.push_back (c);
        }
    }

    return jobs;
}

void usage()
{
    std::cout << "verdant_render [--suite [--quick]] [--out DIR] [--signal NAME] [--preset NAME|N] [--solo MODULE]\n"
                 "               [--set id=value]... [--at SECONDS:id=value]... [--sr HZ] [--block N] [--seconds S] [--bpm B] [--name NAME]\n"
                 "               [--varblock] [--automate] [--stopped] [--list-presets] [--list-params]\n"
                 "signals: impulse sweep noise phrase pluck voice hostile sine silence\n"
                 "solo:    dry tape grain arp chorus reverb\n";
}

} // namespace

int main (int argc, char** argv)
{
    Job job;
    std::string outDir = "render_out";
    bool suite = false, quick = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> std::string { return i + 1 < argc ? argv[++i] : ""; };

        if (a == "--suite") suite = true;
        else if (a == "--quick") quick = true;
        else if (a == "--out") outDir = next();
        else if (a == "--signal") job.signal = next();
        else if (a == "--solo") job.solo = next();
        else if (a == "--sr") job.sr = std::atof (next().c_str());
        else if (a == "--block") job.block = std::max (1, std::atoi (next().c_str()));
        else if (a == "--seconds") job.seconds = std::atof (next().c_str());
        else if (a == "--bpm") job.bpm = std::atof (next().c_str());
        else if (a == "--name") job.name = next();
        else if (a == "--varblock") job.varBlock = true;
        else if (a == "--automate") job.automate = true;
        else if (a == "--stopped") job.playing = false;
        else if (a == "--sustain") job.allowSustain = true;
        else if (a == "--preset")
        {
            const std::string v = next();
            const auto& ps = factoryPresets();
            bool found = false;
            for (int p = 0; p < int (ps.size()); ++p)
                if (v == ps[size_t (p)].name || slug (v) == slug (ps[size_t (p)].name)) { job.preset = p; found = true; }
            if (! found)
            {
                char* end = nullptr;
                const long n = std::strtol (v.c_str(), &end, 10);
                if (end != v.c_str() && *end == 0 && n >= 0 && n < long (ps.size())) job.preset = int (n);
                else { std::cerr << "unknown preset: " << v << "\n"; return 2; }
            }
        }
        else if (a == "--at")
        {
            // --at 3.0:revFreeze=1
            const std::string v = next();
            const auto colon = v.find (':'), eq = v.find ('=');
            const int pid = (colon == std::string::npos || eq == std::string::npos) ? -1
                          : findParam (v.substr (colon + 1, eq - colon - 1).c_str());
            if (pid < 0) { std::cerr << "bad --at " << v << "\n"; return 2; }
            job.events.push_back ({ std::atof (v.substr (0, colon).c_str()), pid, float (std::atof (v.substr (eq + 1).c_str())) });
        }
        else if (a == "--set")
        {
            const std::string v = next();
            const auto eq = v.find ('=');
            const int pid = eq == std::string::npos ? -1 : findParam (v.substr (0, eq).c_str());
            if (pid < 0) { std::cerr << "bad --set " << v << "\n"; return 2; }
            job.sets.push_back ({ pid, float (std::atof (v.substr (eq + 1).c_str())) });
        }
        else if (a == "--list-presets")
        {
            for (const auto& p : factoryPresets())
                std::cout << p.name << "\t" << p.category << "\t" << p.description << "\n";
            return 0;
        }
        else if (a == "--list-params")
        {
            for (int p = 0; p < kNumParams; ++p)
            {
                char buf[48];
                formatValue (kParams[p], kParams[p].def, buf, sizeof (buf));
                std::cout << kParams[p].id << "\t" << kParams[p].group << "\t" << kParams[p].name << "\t"
                          << kParams[p].min << "\t" << kParams[p].max << "\t" << buf << "\t" << kParams[p].help << "\n";
            }
            return 0;
        }
        else { usage(); return a == "--help" ? 0 : 2; }
    }

    std::filesystem::create_directories (outDir);

    if (! suite)
    {
        if (job.name.empty())
            job.name = slug (factoryPresets()[size_t (job.preset)].name) + "__" + job.solo + "__" + job.signal;
        const Result r = runJob (job, outDir);
        std::cout << jsonLine (r) << "\n";
        return r.failures.empty() ? 0 : 1;
    }

    const auto jobs = buildSuite (quick);
    std::ofstream report (outDir + "/report.jsonl");
    int failed = 0;
    double worstCpu = 0.0;
    std::string worstCpuName;
    for (const auto& j : jobs)
    {
        const Result r = runJob (j, outDir);
        report << jsonLine (r) << "\n";
        if (! r.failures.empty())
        {
            ++failed;
            std::cout << "FAIL " << j.name << ": ";
            for (const auto& f : r.failures) std::cout << f << "; ";
            std::cout << jsonLine (r) << "\n";
        }
        if (j.sr == 48000.0 && r.cpuPercent > worstCpu) { worstCpu = r.cpuPercent; worstCpuName = j.name; }
    }
    std::cout << jobs.size() << " renders, " << failed << " failed. Worst CPU at 48 kHz: " << worstCpu
              << " % of one core (" << worstCpuName << ")\n";
    return failed == 0 ? 0 : 1;
}
