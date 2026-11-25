#include "Export.h"
#include "engine/OfflineRender.h"

namespace exporter {

using namespace atm;

static juce::File exportDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Automata");
    d.createDirectory();
    return d;
}

static double currentBpm (AutomataProcessor& proc)
{
    const float host = proc.engine().shared.bpm.load (std::memory_order_relaxed);
    return host > 1.f ? (double) host : (double) globalParamDesc (GP::Tempo, nullptr).toPhys (proc.session().project().kit.gparams[GP::Tempo]);
}

static juce::String safeName (const juce::String& s) { return juce::File::createLegalFileName (s.isEmpty() ? "Automata" : s).replaceCharacter (' ', '_'); }

juce::File writePatternMidi (AutomataProcessor& proc, int pattern)
{
    std::lock_guard<std::recursive_mutex> g (proc.session().mutex());
    const Project& p = proc.session().project();
    const double bars = (double) juce::jmax (1, (int) p.patterns[pattern].lengthBars) * (p.patterns[pattern].lengthBars < 4 ? 4.0 / p.patterns[pattern].lengthBars : 1.0);
    std::vector<ExportNote> notes;
    collectPatternNotes (p, pattern, bars, notes);
    const int ppq = 960;
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    juce::MidiMessageSequence tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / currentBpm (proc))), 0.0);
    tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    mf.addTrack (tempo);
    for (int t = 0; t < kTracks; ++t)
    {
        juce::MidiMessageSequence seq;
        const TrackSound& ts = p.kit.tracks[t];
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, juce::String (t + 1) + " " + ts.name), 0.0);
        const int ch = juce::jlimit (1, 16, (int) ts.midiCh);
        bool any = false;
        for (const ExportNote& n : notes)
        {
            if (n.track != t) continue;
            const int nn = juce::jlimit (0, 127, (int) std::lround (n.note));
            const double on = n.beat * ppq, off = juce::jmax (on + 1.0, (n.beat + n.lengthBeats) * ppq - 1.0);
            seq.addEvent (juce::MidiMessage::noteOn (ch, nn, (juce::uint8) juce::jlimit (1, 127, (int) std::lround (n.velocity * 127.f))), on);
            seq.addEvent (juce::MidiMessage::noteOff (ch, nn), off);
            any = true;
        }
        if (! any) continue;
        seq.sort(); seq.updateMatchedPairs();
        seq.addEvent (juce::MidiMessage::endOfTrack(), bars * 4.0 * ppq);
        mf.addTrack (seq);
    }
    const juce::File f = exportDir().getChildFile (safeName (p.name) + "_" + p.patterns[pattern].name + ".mid");
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (! os.openedOk() || ! mf.writeTo (os)) return {};
    return f;
}

juce::File renderPatternAudio (AutomataProcessor& proc, int pattern, int soloTrack)
{
    std::vector<float> l, r;
    juce::String name;
    double sr = 48000.0;
    {
        std::lock_guard<std::recursive_mutex> g (proc.session().mutex());
        auto p = std::make_unique<Project> (proc.session().project());
        p->currentPattern = (uint8_t) pattern;
        p->song.songMode = 0;
        const SampleData* samples[kTracks];
        for (int t = 0; t < kTracks; ++t) samples[t] = proc.session().samples().data (p->kit.tracks[t].sampleId);
        OfflineOptions o;
        sr = proc.getSampleRate() > 8000.0 ? proc.getSampleRate() : 48000.0;
        o.sampleRate = sr; o.bpm = currentBpm (proc);
        const int lb = juce::jmax (1, (int) p->patterns[pattern].lengthBars);
        o.bars = lb < 4 ? 4.0 : (double) lb;
        o.tailSeconds = 4.0; o.soloTrack = soloTrack;
        renderOffline (*p, samples, o, l, r);
        name = safeName (p->name) + "_" + juce::String (p->patterns[pattern].name) + (soloTrack >= 0 ? "_T" + juce::String (soloTrack + 1) : juce::String())
             + "_" + juce::String ((int) std::lround (o.bpm)) + "bpm.wav";
    }
    const juce::File f = exportDir().getChildFile (name);
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    if (! static_cast<juce::FileOutputStream*> (os.get())->openedOk()) return {};
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
    if (writer == nullptr) return {};
    const float* ch[2] = { l.data(), r.data() };
    writer->writeFromFloatArrays (ch, 2, (int) l.size());
    writer.reset();
    return f;
}

} // namespace exporter
