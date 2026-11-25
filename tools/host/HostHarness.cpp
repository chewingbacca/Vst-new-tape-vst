#include "HostHarness.h"
#include "model/Json.h"
#include <cstdio>

namespace harness {

using namespace atm;

void demoProject (Project& p)
{
    initProject (p);
    TrackPattern& k = p.patterns[0].tracks[0];
    for (int i : { 0, 6, 10 }) { k.steps[i].on = 1; k.steps[i].vel = 120; }
    setTrackParamText (p.kit.tracks[1], "model", "snare");
    TrackPattern& s = p.patterns[0].tracks[1];
    s.steps[4].on = 1; s.steps[12].on = 1; s.steps[15].on = 1; s.steps[15].ratCount = 3; s.steps[15].ratVel = -0.5f;
    setTrackParamText (p.kit.tracks[2], "model", "hat"); setTrackParamText (p.kit.tracks[2], "decay", "0.05");
    TrackPattern& h = p.patterns[0].tracks[2];
    for (int i = 0; i < 16; ++i) { h.steps[i].on = 1; h.steps[i].vel = (uint8_t) (i % 4 == 2 ? 112 : 64); }
    // a melodic line with slides, conditions, probability and a lock lane so every cell feature shows up
    initTrackSound (p.kit.tracks[3], Engine::Synth); p.kit.tracks[3].role = Role::Bass; p.kit.tracks[3].scaleOn = 1; p.kit.tracks[3].baseNote = 36;
    std::snprintf (p.kit.tracks[3].name, sizeof (p.kit.tracks[3].name), "Acid Line");
    setTrackParamText (p.kit.tracks[3], "flttype", "Ladder"); setTrackParamText (p.kit.tracks[3], "cutoff", "600");
    setTrackParamText (p.kit.tracks[3], "res", "0.7"); setTrackParamText (p.kit.tracks[3], "fltenv", "0.5");
    TrackPattern& a = p.patterns[0].tracks[3];
    a.length = 24;
    static const int pitches[24] = { 0, 0, 7, 0, 3, 0, 0, 12, 0, 5, 0, 0, 7, 3, 0, 0, 10, 0, 0, 7, 0, 3, 0, 12 };
    for (int i = 0; i < 24; ++i)
    {
        Step& st = a.steps[i];
        st.on = (i % 3) != 1; st.pitch = (int8_t) pitches[i]; st.vel = (uint8_t) (i % 4 == 0 ? 120 : 84);
        if (i == 7 || i == 16) st.slide = 1;
        if (i == 9) st.prob = 50;
        if (i == 13) st.cond = condMakeAB (1, 2);
        if (i == 20) { st.ratCount = 4; st.ratCurve = RatCurve::Bounce; st.ratPitch = 12.f; }
        if (i == 5) st.micro = 0.3f;
        st.len = i % 5 == 0 ? 2.f : 0.6f;
    }
    for (int i : { 2, 7, 12, 19 }) a.setLock (i, TP::FltCut, 0.5f + 0.02f * (float) i);
    a.setLock (12, TP::FltRes, 0.9f);
    TrackPattern& hh = p.patterns[0].tracks[2];
    hh.steps[7].ratCount = 3; hh.steps[11].ratCount = 6; hh.steps[11].ratCurve = RatCurve::Accel; hh.steps[11].ratVel = -0.7f;
    hh.steps[13].prob = 60; hh.steps[5].micro = -0.2f;
    std::snprintf (p.name, sizeof (p.name), "Demo");
}

int runCli (int argc, char** argv)
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    auto value = [&args] (const char* flag, const juce::String& def = {}) -> juce::String
    {
        const int i = args.indexOf (flag);
        return (i >= 0 && i + 1 < args.size()) ? args[i + 1] : def;
    };
    auto has = [&args] (const char* flag) { return args.contains (flag); };

    RenderOptions o;
    o.sr = value ("--sr", "48000").getDoubleValue();
    o.bpm = value ("--bpm", "140").getDoubleValue();
    o.block = value ("--block", "512").getIntValue();
    o.bars = value ("--bars", "8").getDoubleValue();
    o.tailSeconds = value ("--tail", "2").getDoubleValue();
    o.varBlock = has ("--varblock");
    o.trackBuses = has ("--tracks");
    o.useInternalTransport = has ("--internal");
    if (has ("--loop"))
    {
        const auto l = juce::StringArray::fromTokens (value ("--loop"), ":", "");
        if (l.size() == 2) { o.loopStartBar = l[0].getDoubleValue(); o.loopEndBar = l[1].getDoubleValue(); }
    }

    auto proc = std::make_unique<AutomataProcessor>();
    proc->setRateAndBufferSizeDetails (o.sr, o.block);
    proc->prepareToPlay (o.sr, o.block);

    if (has ("--project"))
    {
        if (! proc->loadProjectFile (juce::File::getCurrentWorkingDirectory().getChildFile (value ("--project"))))
        { std::fprintf (stderr, "cannot load project %s\n", value ("--project").toRawUTF8()); return 2; }
    }
    else if (has ("--json"))
    {
        const auto text = juce::File::getCurrentWorkingDirectory().getChildFile (value ("--json")).loadFileAsString().toStdString();
        bool ok = false; const Json j = Json::parse (text, &ok);
        if (! ok || ! proc->session().fromJson (j)) { std::fprintf (stderr, "cannot load json %s\n", value ("--json").toRawUTF8()); return 2; }
    }
    else
    {
        auto p = std::make_unique<Project>();
        demoProject (*p);
        proc->session().replaceProject (*p);
    }
    for (int i = 0; i < args.size() - 1; ++i)
        if (args[i] == "--set")
        {
            const auto kv = juce::StringArray::fromTokens (args[i + 1], "=", "");
            const int gp = kv.size() == 2 ? globalParamFromKey (kv[0].toRawUTF8()) : -1;
            if (gp < 0) { std::fprintf (stderr, "unknown global param %s\n", args[i + 1].toRawUTF8()); return 2; }
            proc->setGlobalParam (gp, kv[1].getFloatValue());
        }

    if (has ("--state-roundtrip"))
    {
        juce::MemoryBlock mb;
        proc->getStateInformation (mb);
        auto second = std::make_unique<AutomataProcessor>();
        second->setRateAndBufferSizeDetails (o.sr, o.block);
        second->prepareToPlay (o.sr, o.block);
        second->setStateInformation (mb.getData(), (int) mb.getSize());
        proc = std::move (second);
    }
    if (has ("--save")) proc->saveProjectFile (juce::File::getCurrentWorkingDirectory().getChildFile (value ("--save")));

    const RenderResult r = render (*proc, o);

    if (has ("--out")) writeWav (value ("--out").toStdString(), r.main, (int) o.sr, 24);
    if (has ("--tracks"))
        for (int t = 0; t < kTracks; ++t)
            writeWav ((value ("--tracks") + "_t" + juce::String (t + 1) + ".wav").toStdString(), r.tracks[(size_t) t], (int) o.sr, 24);
    if (has ("--midi"))
    {
        juce::String text;
        for (auto& e : r.midiOut) text << e.sample << " " << juce::String::toHexString (e.data, 3) << "\n";
        juce::File::getCurrentWorkingDirectory().getChildFile (value ("--midi")).replaceWithText (text);
    }
    const Stats l = measure (r.main[0]), rr = measure (r.main[1]);
    std::printf ("{\"peak\":%.5f,\"rmsL\":%.5f,\"rmsR\":%.5f,\"dc\":%.6f,\"maxStep\":%.5f,\"nan\":%s,\"seconds\":%.3f,\"cpu\":%.4f,\"midiOut\":%d}\n",
                 std::max (l.peak, rr.peak), l.rms, rr.rms, l.dc, std::max (l.maxStep, rr.maxStep), (l.nan || rr.nan) ? "true" : "false",
                 r.seconds, r.cpuSeconds / r.seconds, (int) r.midiOut.size());
    return (l.nan || rr.nan) ? 1 : 0;
}

} // namespace harness
