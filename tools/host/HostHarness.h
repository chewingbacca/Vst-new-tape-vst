// A simulated host around the real AudioProcessor, shared by the offline tools.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "util/Wav.h"
#include <string>
#include <vector>

namespace harness {

struct HostPlayHead : public juce::AudioPlayHead
{
    PositionInfo info;
    juce::Optional<PositionInfo> getPosition() const override { return info; }
};

struct MidiEv { double beat; juce::MidiMessage msg; };

struct RenderOptions
{
    double sr = 48000.0, bpm = 140.0;
    int    block = 512;
    bool   varBlock = false;
    double bars = 8.0;
    double tailSeconds = 2.0;
    bool   trackBuses = false;
    double loopStartBar = -1.0, loopEndBar = -1.0;   // host loop (in bars), -1 = none
    int    loopPasses = 2;
    int    tsNum = 4, tsDen = 4;
    double startBar = 0.0;
    std::vector<MidiEv> midi;                         // events by beat position
    bool   useInternalTransport = false;              // host stopped, plugin's own play button
};

struct MidiOutEv { juce::int64 sample; juce::uint8 data[3]; };

struct RenderResult
{
    std::vector<std::vector<float>> main;                       // 2 channels
    std::vector<std::vector<std::vector<float>>> tracks;       // [track][2][n]
    std::vector<MidiOutEv> midiOut;
    double seconds = 0.0;         // audio duration
    double cpuSeconds = 0.0;      // wall time spent inside processBlock
};

inline RenderResult render (AutomataProcessor& proc, const RenderOptions& o)
{
    RenderResult res;
    HostPlayHead ph;
    proc.setPlayHead (&ph);

    auto layout = proc.getBusesLayout();
    for (int i = 1; i < layout.outputBuses.size(); ++i)
        layout.outputBuses.getReference (i) = o.trackBuses ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    for (auto& in : layout.inputBuses) in = juce::AudioChannelSet::disabled();
    proc.setBusesLayout (layout);
    proc.setRateAndBufferSizeDetails (o.sr, o.block);
    proc.prepareToPlay (o.sr, o.block);
    proc.housekeeping();

    const int totalCh = juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
    const double beatsPerBar = (double) o.tsNum * 4.0 / (double) o.tsDen;
    const double spb = o.sr * 60.0 / o.bpm;                       // samples per beat
    const juce::int64 playSamples = (juce::int64) std::llround (o.bars * beatsPerBar * spb);
    const juce::int64 tailSamples = (juce::int64) (o.tailSeconds * o.sr);
    const juce::int64 total = playSamples + tailSamples;

    res.main.assign (2, std::vector<float> ((size_t) total, 0.f));
    if (o.trackBuses) res.tracks.assign (atm::kTracks, std::vector<std::vector<float>> (2, std::vector<float> ((size_t) total, 0.f)));

    juce::AudioBuffer<float> buf (juce::jmax (2, totalCh), juce::jmax (o.block, 4096));
    juce::MidiBuffer midi;
    double beat = o.startBar * beatsPerBar;                       // host position in beats
    int loopsLeft = o.loopPasses - 1;
    const double loopStart = o.loopStartBar * beatsPerBar, loopEnd = o.loopEndBar * beatsPerBar;
    juce::uint32 rnd = 0x1234567u;
    size_t midiIdx = 0;
    std::vector<MidiEv> evs = o.midi;
    std::sort (evs.begin(), evs.end(), [] (const MidiEv& a, const MidiEv& b) { return a.beat < b.beat; });

    if (o.useInternalTransport) proc.session().setPlay (true);

    for (juce::int64 done = 0; done < total;)
    {
        const bool playing = done < playSamples;
        int n = o.block;
        if (o.varBlock) { rnd = rnd * 1664525u + 1013904223u; n = 1 + (int) ((rnd >> 12) % (juce::uint32) o.block); }
        n = (int) juce::jmin<juce::int64> (n, total - done);
        if (playing) n = (int) juce::jmin<juce::int64> (n, playSamples - done);
        // split the block at the loop end like a host would
        if (playing && o.loopEndBar > 0 && loopsLeft > 0)
        {
            const double toEnd = (loopEnd - beat) * spb;
            if (toEnd <= 0.5) { beat = loopStart; --loopsLeft; }
            else if (toEnd < (double) n) n = juce::jmax (1, (int) std::floor (toEnd + 0.5));
        }

        if (playing && o.useInternalTransport && done + n >= playSamples) {}
        ph.info = juce::AudioPlayHead::PositionInfo();
        ph.info.setBpm (o.bpm);
        ph.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { o.tsNum, o.tsDen });
        ph.info.setIsPlaying (playing && ! o.useInternalTransport);
        ph.info.setPpqPosition (beat);
        ph.info.setPpqPositionOfLastBarStart (std::floor (beat / beatsPerBar) * beatsPerBar);
        ph.info.setTimeInSamples ((juce::int64) std::llround (beat * spb));
        ph.info.setTimeInSeconds (beat * 60.0 / o.bpm);

        midi.clear();
        if (playing)
        {
            const double endBeat = beat + (double) n / spb;
            while (midiIdx < evs.size() && evs[midiIdx].beat < endBeat)
            {
                const int s = juce::jlimit (0, n - 1, (int) std::ceil ((evs[midiIdx].beat - beat) * spb - 1.0e-6));
                midi.addEvent (evs[midiIdx].msg, s);
                ++midiIdx;
            }
        }
        if (! playing && o.useInternalTransport && done == playSamples) proc.session().setPlay (false);

        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), buf.getNumChannels(), n);
        view.clear();
        const auto t0 = juce::Time::getHighResolutionTicks();
        proc.processBlock (view, midi);
        res.cpuSeconds += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);

        for (int c = 0; c < 2; ++c)
            std::memcpy (res.main[(size_t) c].data() + done, view.getReadPointer (juce::jmin (c, view.getNumChannels() - 1)), sizeof (float) * (size_t) n);
        if (o.trackBuses)
            for (int t = 0; t < atm::kTracks; ++t)
            {
                const int ch = proc.getChannelIndexInProcessBlockBuffer (false, t + 1, 0);
                if (ch < 0 || ch + 1 >= view.getNumChannels()) continue;
                for (int c = 0; c < 2; ++c)
                    std::memcpy (res.tracks[(size_t) t][(size_t) c].data() + done, view.getReadPointer (ch + c), sizeof (float) * (size_t) n);
            }
        for (const auto meta : midi)
        {
            MidiOutEv e; e.sample = done + meta.samplePosition;
            const auto m = meta.getMessage();
            e.data[0] = m.getRawData()[0]; e.data[1] = m.getRawDataSize() > 1 ? m.getRawData()[1] : 0; e.data[2] = m.getRawDataSize() > 2 ? m.getRawData()[2] : 0;
            res.midiOut.push_back (e);
        }
        if (playing) beat += (double) n / spb;
        done += n;
        if ((done & 0xffff) < n) proc.housekeeping();
    }
    proc.housekeeping();
    proc.setPlayHead (nullptr);
    res.seconds = (double) total / o.sr;
    return res;
}

struct Stats { double peak = 0, rms = 0, dc = 0, maxStep = 0; bool nan = false; };
inline Stats measure (const std::vector<float>& x)
{
    Stats s; double sum = 0, sq = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const float v = x[i];
        if (! std::isfinite (v)) { s.nan = true; continue; }
        s.peak = std::max (s.peak, (double) std::fabs (v)); sum += v; sq += (double) v * v;
        if (i) s.maxStep = std::max (s.maxStep, (double) std::fabs (v - x[i - 1]));
    }
    if (! x.empty()) { s.dc = sum / (double) x.size(); s.rms = std::sqrt (sq / (double) x.size()); }
    return s;
}

int runCli (int argc, char** argv);
// Handles --factory <name|index> and --idea <style>:<seed>. Returns the preset's tempo (0 if neither flag), -1 on error.
double loadPresetArgs (AutomataProcessor& proc, const juce::StringArray& args);
// A small project that exercises most step features (used by the tools when nothing is loaded).
void demoProject (atm::Project& p);

} // namespace harness
