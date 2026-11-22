// Core smoke test: a kick/hat/snare pattern through the JUCE-free engine.
#include "engine/AudioEngine.h"
#include "util/Wav.h"
#include "util/Denormals.h"
#include <cstdio>
#include <vector>
#include <chrono>
using namespace atm;

int main (int argc, char** argv)
{
    ScopedFlushDenormals fz;
    const double sr = 48000.0; const int block = argc > 2 ? std::atoi (argv[2]) : 256;
    const double bpm = 140.0;
    AudioEngine eng;
    eng.prepare (sr, block);
    auto* proj = new Project(); initProject (*proj);
    TrackPattern& k = proj->patterns[0].tracks[0];
    for (int i = 0; i < 16; i += 4) { k.steps[i].on = 1; k.steps[i].vel = 120; }
    TrackSound& sn = proj->kit.tracks[1]; setTrackParamText (sn, "model", "snare");
    TrackPattern& s = proj->patterns[0].tracks[1];
    s.steps[4].on = 1; s.steps[12].on = 1; s.steps[12].ratCount = 4; s.steps[12].ratCurve = RatCurve::Accel; s.steps[12].ratVel = -0.6f;
    TrackSound& hh = proj->kit.tracks[2]; setTrackParamText (hh, "model", "hat"); setTrackParamText (hh, "decay", "0.06");
    TrackPattern& h = proj->patterns[0].tracks[2];
    for (int i = 0; i < 16; ++i) { h.steps[i].on = 1; h.steps[i].vel = (uint8_t) (i % 4 == 2 ? 110 : 70); }
    h.steps[7].ratCount = 3; h.steps[15].micro = 0.3f;
    Cmd c; c.type = Cmd::SetProject; c.p = proj; eng.post (c);

    const int total = (int) (sr * 60.0 / bpm * 16.0);
    std::vector<std::vector<float>> out (2, std::vector<float> ((size_t) total));
    ProcessIO io; MidiMsg mo[256];
    auto t0 = std::chrono::steady_clock::now();
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        io.numSamples = n; io.main[0] = out[0].data() + pos; io.main[1] = out[1].data() + pos;
        io.midiOut = mo; io.midiOutCapacity = 256;
        io.transport.valid = true; io.transport.playing = true; io.transport.bpm = bpm;
        io.transport.ppq = (double) pos * bpm / 60.0 / sr;
        io.transport.barStartPpq = std::floor (io.transport.ppq / 4.0) * 4.0;
        eng.process (io);
    }
    const double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    float peak = 0; double rms = 0; bool bad = false;
    for (int i = 0; i < total; ++i) { const float v = out[0][(size_t) i]; if (! std::isfinite (v)) bad = true; peak = std::max (peak, std::fabs (v)); rms += v * v; }
    std::printf ("peak %.3f rms %.4f nan=%d  realtime x%.1f (%.2f%% of one core)\n", peak, std::sqrt (rms / total), bad, (total / sr) / el, 100.0 * el / (total / sr));
    if (argc > 1) writeWav (argv[1], out, (int) sr, 24);
    return bad ? 1 : 0;
}
