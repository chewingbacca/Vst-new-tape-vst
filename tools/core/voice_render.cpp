// Render one engine through a full voice (drive, filter, amp) to a WAV and print measurements.
//
//   voice_render <engine> <out.wav> [sr=48000] [note=60] [vel=1] [dur=2] [gate=0.5]
//                [retrig=<seconds>] [notes=60,63,67] [slide=1] [sample=<file.wav>|test] [block=16]
//                <param>=<value> ...
//
// Params use the keys and physical units of the descriptor tables (core/model/ParamTables.cpp),
// e.g.  voice_render drum /tmp/k.wav model=kick decay=0.4 sweep=0.6 cutoff=8000 flttype=LP24
#include "ToolUtil.h"
#include "voices/Voice.h"
#include "model/Model.h"
#include "util/Denormals.h"
using namespace atm;

int main (int argc, char** argv)
{
    ScopedFlushDenormals fz;
    const tool::Args a = tool::parseArgs (argc, argv);
    if (a.positional.size() < 2) { std::printf ("usage: voice_render <engine> <out.wav> [options] key=value...\n"); return 2; }
    const int engine = engineFromKey (a.positional[0].c_str());
    if (engine < 0) { std::printf ("unknown engine '%s'\n", a.positional[0].c_str()); return 2; }
    const double sr = a.num ("sr", 48000.0), dur = a.num ("dur", 2.0), gate = a.num ("gate", 0.5), retrig = a.num ("retrig", 0.0);
    const float vel = (float) a.num ("vel", 1.0);
    std::vector<float> notes;
    {
        std::string ns = a.get ("notes", a.get ("note", "60"));
        size_t p = 0;
        while (p < ns.size()) { size_t e = ns.find (',', p); if (e == std::string::npos) e = ns.size(); notes.push_back ((float) std::atof (ns.substr (p, e - p).c_str())); p = e + 1; }
    }
    TrackSound ts; initTrackSound (ts, engine);
    static const char* const reserved[] = { "sr", "note", "notes", "vel", "dur", "gate", "retrig", "slide", "sample", "block" };
    for (auto& kv : a.kv)
    {
        bool skip = false; for (auto r : reserved) if (kv.first == r) skip = true;
        if (skip) continue;
        if (! setTrackParamText (ts, kv.first.c_str(), kv.second.c_str())) { std::printf ("unknown or bad param: %s=%s\n", kv.first.c_str(), kv.second.c_str()); return 2; }
    }
    SampleData* sample = nullptr;
    if (a.has ("sample")) sample = a.get ("sample") == "test" ? tool::makeTestSample() : tool::loadSample (a.get ("sample"));
    else if (engine == Engine::Sampler || engine == Engine::Granular) sample = tool::makeTestSample();

    Voice voices[2];
    for (auto& v : voices) { v.prepare (sr); v.setSample (sample); }
    const int total = (int) (sr * dur), block = (int) a.num ("block", 16);
    std::vector<std::vector<float>> out (2, std::vector<float> ((size_t) total, 0.f));
    const bool slide = a.num ("slide", 0) > 0;
    int nextTrig = 0, noteIdx = 0, cur = 0; float lastNote = -1000.f;
    const int gateSamples = std::max (1, (int) (gate * sr));
    tool::Timer timer;
    for (int pos = 0; pos < total;)
    {
        if (pos >= nextTrig && nextTrig >= 0)
        {
            VoiceStart vs; vs.note = notes[(size_t) noteIdx % notes.size()]; vs.velocity = vel; vs.seed = 1234u + (uint32_t) noteIdx;
            vs.gateSamples = gateSamples; vs.slide = slide && noteIdx > 0; vs.glideFrom = lastNote;
            if (vs.slide && voices[cur].active()) voices[cur].slideTo (vs, ts.params);
            else { voices[cur].kill(); cur ^= 1; voices[cur].start (vs, engine, ts.params); }
            lastNote = vs.note; ++noteIdx;
            nextTrig = retrig > 0.0 ? pos + std::max (1, (int) (retrig * sr)) : -1;
        }
        int n = std::min (block, total - pos);
        if (nextTrig > pos) n = std::min (n, nextTrig - pos);
        n = std::min (n, kMaxSlice);
        for (auto& v : voices) if (v.active()) { v.control (ts.params, n); v.render (out[0].data() + pos, out[1].data() + pos, n); }
        pos += n;
    }
    const double el = timer.seconds();
    const auto sl = tool::analyse (out[0], sr), srr = tool::analyse (out[1], sr);
    tool::printStats ("L", sl); tool::printStats ("R", srr);
    std::printf ("cpu    %.2f%% of real time for one voice\n", 100.0 * el / dur);
    writeWav (a.positional[1], out, (int) sr, 24);
    delete sample;
    return (sl.nan || srr.nan) ? 1 : 0;
}
