// A tiny offline "host" around the core engine for tests.
#pragma once
#include "engine/AudioEngine.h"
#include "engine/OfflineRender.h"
#include <memory>
#include <vector>

struct TestHost
{
    atm::AudioEngine eng;
    double sr = 48000.0, bpm = 120.0;
    int64_t pos = 0;                 // song position in samples
    bool playing = true;
    std::vector<float> L, R;
    std::vector<atm::MidiMsg> midiOut;      // sample positions are absolute (relative to the start of the render)
    std::vector<atm::MidiMsg> midiIn;       // absolute sample positions, sorted

    explicit TestHost (double sampleRate = 48000.0, double tempo = 120.0) : sr (sampleRate), bpm (tempo) { eng.prepare (sr, 4096); }

    void setProject (const atm::Project& p)
    {
        atm::Cmd c; c.type = atm::Cmd::SetProject; c.p = new atm::Project (p);
        eng.post (c); eng.drainCommandsOffline();
        collectGarbage();
    }
    void post (const atm::Cmd& c) { eng.post (c); }
    void collectGarbage()
    {
        atm::Garbage g;
        while (eng.popGarbage (g))
        {
            switch (g.kind)
            {
                case atm::Garbage::KProject: delete static_cast<atm::Project*> (g.p); break;
                case atm::Garbage::KTrackPattern: delete static_cast<atm::TrackPattern*> (g.p); break;
                case atm::Garbage::KTrackSound: delete static_cast<atm::TrackSound*> (g.p); break;
                case atm::Garbage::KEffect: delete static_cast<atm::Effect*> (g.p); break;
                case atm::Garbage::KTuning: delete static_cast<atm::Tuning*> (g.p); break;
                case atm::Garbage::KSong: delete static_cast<atm::Song*> (g.p); break;
                default: break;
            }
        }
    }

    // Render `frames` samples appended to L/R, in blocks of `block` (or varying sizes if block <= 0).
    void render (int frames, int block = 256)
    {
        const size_t start = L.size();
        L.resize (start + (size_t) frames); R.resize (start + (size_t) frames);
        atm::MidiMsg mo[512];
        std::vector<atm::MidiMsg> mi;
        uint32_t rnd = 12345;
        size_t miIdx = 0;
        while (miIdx < midiIn.size() && midiIn[miIdx].sample < (int) start) ++miIdx;
        for (int done = 0; done < frames;)
        {
            int n = block;
            if (block <= 0) { rnd = rnd * 1664525u + 1013904223u; n = 1 + (int) ((rnd >> 16) % 700); }
            n = std::min (n, frames - done);
            atm::ProcessIO io;
            io.numSamples = n;
            io.main[0] = L.data() + start + (size_t) done; io.main[1] = R.data() + start + (size_t) done;
            io.midiOut = mo; io.midiOutCapacity = 512;
            mi.clear();
            const int blockStart = (int) start + done;
            while (miIdx < midiIn.size() && midiIn[miIdx].sample < blockStart + n)
            { atm::MidiMsg m = midiIn[miIdx++]; m.sample -= blockStart; mi.push_back (m); }
            io.midiIn = mi.data(); io.numMidiIn = (int) mi.size();
            io.transport.valid = true; io.transport.playing = playing; io.transport.bpm = bpm;
            io.transport.ppq = (double) pos * bpm / 60.0 / sr;
            io.transport.barStartPpq = std::floor (io.transport.ppq / 4.0) * 4.0;
            eng.process (io);
            for (int i = 0; i < io.numMidiOut; ++i) { atm::MidiMsg m = mo[i]; m.sample += blockStart; midiOut.push_back (m); }
            if (playing) pos += n;
            done += n;
        }
        collectGarbage();
    }

    double beatToSample (double beat) const { return beat * 60.0 / bpm * sr; }
};

// A project with one short, clicky kick on track 0 and nothing else: onsets are easy to detect.
inline std::unique_ptr<atm::Project> makeClickProject()
{
    auto pp = std::make_unique<atm::Project>();
    atm::Project& p = *pp;
    atm::initProject (p);
    atm::TrackSound& ts = p.kit.tracks[0];
    atm::setTrackParamText (ts, "model", "kick");
    atm::setTrackParamPhys (ts, "decay", 0.01f);
    atm::setTrackParamPhys (ts, "sweep", 0.f);
    atm::setTrackParamPhys (ts, "tune", 24.f);
    atm::setTrackParamPhys (ts, "snap", 0.f);
    atm::setTrackParamPhys (ts, "noise", 0.f);
    atm::setTrackParamPhys (ts, "ampd", 0.006f);
    atm::setTrackParamPhys (ts, "amps", 0.f);
    atm::setTrackParamPhys (ts, "ampr", 0.002f);
    atm::setTrackParamPhys (ts, "ampvel", 0.f);
    p.kit.gparams[atm::GP::LimitOn] = 0.f;
    return pp;
}

// Indices where the signal rises above `thresh` after at least `quiet` samples below it.
inline std::vector<int> findOnsets (const std::vector<float>& x, float thresh = 1.0e-5f, int quiet = 48)
{
    std::vector<int> on;
    int below = quiet;
    for (int i = 0; i < (int) x.size(); ++i)
    {
        if (std::fabs (x[(size_t) i]) > thresh) { if (below >= quiet) on.push_back (i); below = 0; }
        else ++below;
    }
    return on;
}
