#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "model/Json.h"

using namespace atm;

namespace {

} // namespace

juce::AudioProcessor::BusesProperties AutomataProcessor::makeBuses()
{
    auto b = BusesProperties()
                 .withOutput ("Main", juce::AudioChannelSet::stereo(), true)
                 .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false);
    for (int t = 0; t < kTracks; ++t)
        b = b.withOutput ("Track " + juce::String (t + 1), juce::AudioChannelSet::stereo(), false);
    return b;
}

namespace {

// A host parameter that is simply one of our normalised parameters.
class NormParam : public juce::AudioParameterFloat
{
public:
    NormParam (const juce::String& id, const juce::String& name, const ParamDesc& d)
        : AudioParameterFloat (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.f, 1.f), d.defNorm(),
                               juce::AudioParameterFloatAttributes()
                                   .withStringFromValueFunction ([&d] (float v, int) { char buf[32]; formatParam (d, v, buf, 32); return juce::String (buf); })
                                   .withValueFromStringFunction ([&d] (const juce::String& s) { float n = 0.f; return parseParam (d, s.toRawUTF8(), n) ? n : s.getFloatValue(); }))
    {}
};

constexpr char kMagic[8] = { 'A', 'T', 'M', 'A', 'S', 'T', '0', '1' };
constexpr int kMaxEmbedSeconds = 60;

void writeU32 (juce::MemoryOutputStream& o, uint32_t v) { o.writeInt ((int) v); }

} // namespace

// ============================================================================
AutomataProcessor::AutomataProcessor() : AudioProcessor (makeBuses())
{
    formats.registerBasicFormats();
    eng = std::make_unique<AudioEngine>();
    eng->prepare (48000.0, 512);
    sess = std::make_unique<Session> (*eng);
    sess->samples().fileLoader = [] (const std::string& path) { return decodeFile (juce::File (juce::String::fromUTF8 (path.c_str()))); };
    sess->onProjectReplaced = [this] { syncHostParamsFromProject(); };

    auto add = [this] (juce::RangedAudioParameter* p, int kind, int a, int b)
    {
        addParameter (p);
        HostParam h; h.param = p; h.kind = kind; h.a = a; h.b = b;
        hostParams.push_back (h);
    };
    static constexpr int globals[] = { GP::Density, GP::Complexity, GP::Mutation, GP::Chaos, GP::Glitch, GP::Space,
                                       GP::Macro1, GP::Macro2, GP::Macro3, GP::Macro4, GP::Master, GP::Swing };
    for (int gp : globals)
    {
        const ParamDesc& d = globalParamDesc (gp, nullptr);
        add (new NormParam (d.key, d.name, d), HostParam::Global, gp, 0);
    }
    add (new juce::AudioParameterBool (juce::ParameterID { "fill", 1 }, "Fill", false), HostParam::Fill, 0, 0);
    add (new juce::AudioParameterBool (juce::ParameterID { "break", 1 }, "Break", false), HostParam::Break, 0, 0);
    add (new juce::AudioParameterBool (juce::ParameterID { "evolve", 1 }, "Evolve", false), HostParam::Evolve, 0, 0);
    for (int t = 0; t < kTracks; ++t)
    {
        const juce::String n = juce::String (t + 1);
        add (new NormParam ("t" + n + "level", "Track " + n + " Level", commonTrackParamDesc (TP::Level)), HostParam::Track, t, TP::Level);
        add (new NormParam ("t" + n + "pan", "Track " + n + " Pan", commonTrackParamDesc (TP::Pan)), HostParam::Track, t, TP::Pan);
        add (new juce::AudioParameterBool (juce::ParameterID { "t" + n + "mute", 1 }, "Track " + n + " Mute", false), HostParam::Mute, t, 0);
    }
    midiIn.resize (1024); midiOut.resize (2048);
    syncHostParamsFromProject();
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr) startTimerHz (30);
}

AutomataProcessor::~AutomataProcessor()
{
    stopTimer();
    loaderPool.removeAllJobs (true, 4000);
    sess.reset();
    eng.reset();
}

bool AutomataProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo()) return false;
    }
    for (const auto& set : layouts.inputBuses)
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo() && set != juce::AudioChannelSet::mono()) return false;
    return true;
}

void AutomataProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool rateChanged = std::abs (sampleRate - currentSampleRate) > 0.5;
    currentSampleRate = sampleRate;
    // apply anything queued while audio was stopped, then (re)build for the new rate
    eng->drainCommandsOffline();
    eng->prepare (sampleRate, juce::jmax (16, samplesPerBlock));
    scratch.setSize (2, juce::jmax (8192, samplesPerBlock), false, true, true);
    for (auto& h : hostParams) h.lastAudio = -1.f;
    juce::ignoreUnused (rateChanged);
}

// ============================================================================ host params
juce::RangedAudioParameter* AutomataProcessor::findHostParam (int kind, int a, int b) const
{
    for (auto& h : hostParams) if (h.kind == kind && h.a == a && h.b == b) return h.param;
    return nullptr;
}

void AutomataProcessor::syncHostParamsFromProject()
{
    const Project& p = sess->project();
    suppressHostSync = true;
    for (auto& h : hostParams)
    {
        float v = 0.f;
        switch (h.kind)
        {
            case HostParam::Global: v = p.kit.gparams[h.a]; break;
            case HostParam::Track:  v = p.kit.tracks[h.a].params[h.b]; break;
            case HostParam::Mute:   v = p.kit.tracks[h.a].mute ? 1.f : 0.f; break;
            case HostParam::Evolve: v = p.evolveOn ? 1.f : 0.f; break;
            default: v = 0.f; break;
        }
        if (std::abs (h.param->getValue() - v) > 1.0e-6f) h.param->setValueNotifyingHost (v);
        h.lastMsg = v;
    }
    suppressHostSync = false;
}

void AutomataProcessor::setGlobalParam (int gp, float norm)
{
    if (auto* p = findHostParam (HostParam::Global, gp, 0)) p->setValueNotifyingHost (clamp01 (norm));
    sess->setGlobalParam (gp, norm);
}

void AutomataProcessor::setTrackParam (int track, int tp, float norm)
{
    if (auto* p = findHostParam (HostParam::Track, track, tp)) p->setValueNotifyingHost (clamp01 (norm));
    sess->setTrackParam (track, tp, norm);
}

void AutomataProcessor::setTrackMute (int track, bool mute)
{
    if (auto* p = findHostParam (HostParam::Mute, track, 0)) p->setValueNotifyingHost (mute ? 1.f : 0.f);
    sess->setTrackFlags (track, mute, sess->project().kit.tracks[track].solo != 0);
}

void AutomataProcessor::setEvolve (bool on)
{
    if (auto* p = findHostParam (HostParam::Evolve, 0, 0)) p->setValueNotifyingHost (on ? 1.f : 0.f);
    sess->setEvolve (on);
}

void AutomataProcessor::beginGesture (int track, int param)
{
    if (auto* p = track < 0 ? findHostParam (HostParam::Global, param, 0) : findHostParam (HostParam::Track, track, param)) p->beginChangeGesture();
}

void AutomataProcessor::endGesture (int track, int param)
{
    if (auto* p = track < 0 ? findHostParam (HostParam::Global, param, 0) : findHostParam (HostParam::Track, track, param)) p->endChangeGesture();
}

bool AutomataProcessor::undo() { const bool ok = sess->undo(); if (ok) syncHostParamsFromProject(); return ok; }
bool AutomataProcessor::redo() { const bool ok = sess->redo(); if (ok) syncHostParamsFromProject(); return ok; }

void AutomataProcessor::housekeeping()
{
    sess->tick();
    if (suppressHostSync) return;
    // mirror host automation into the canonical project (no undo, no re-posting)
    std::lock_guard<std::recursive_mutex> g (sess->mutex());
    for (auto& h : hostParams)
    {
        const float v = h.param->getValue();
        if (v == h.lastMsg) continue;
        h.lastMsg = v;
        switch (h.kind)
        {
            case HostParam::Global: sess->noteHostGlobal (h.a, v); break;
            case HostParam::Track:  sess->noteHostTrack (h.a, h.b, v); break;
            case HostParam::Mute:   sess->noteHostMute (h.a, v >= 0.5f); break;
            default: break;
        }
    }
}

// ============================================================================ audio
void AutomataProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    // not advertised (supportsDoublePrecisionProcessing is false); keep hosts that call it anyway silent and safe
    buffer.clear(); midi.clear();
}

void AutomataProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();

    // ---- host parameters
    for (auto& h : hostParams)
    {
        const float v = h.param->getValue();
        if (v == h.lastAudio) continue;
        h.lastAudio = v;
        switch (h.kind)
        {
            case HostParam::Global: eng->setGlobalParamRt (h.a, v); break;
            case HostParam::Track:  eng->setTrackParamRt (h.a, h.b, v); break;
            case HostParam::Mute:   eng->setTrackMuteRt (h.a, v >= 0.5f); break;
            case HostParam::Fill:   eng->setFillRt (v >= 0.5f); break;
            case HostParam::Break:  eng->setBreakRt (v >= 0.5f); break;
            case HostParam::Evolve: eng->setEvolveRt (v >= 0.5f); break;
        }
    }

    ProcessIO io;
    io.numSamples = n;

    // ---- sidechain input: copy it out before the output buses (which may share channels) are cleared
    const float* scL = nullptr; const float* scR = nullptr;
    if (auto* inBus = getBus (true, 0); inBus != nullptr && inBus->isEnabled() && inBus->getNumberOfChannels() > 0 && n <= scratch.getNumSamples())
    {
        auto in = getBusBuffer (buffer, true, 0);
        if (in.getNumChannels() > 0)
        {
            scratch.copyFrom (0, 0, in, 0, 0, n);
            scratch.copyFrom (1, 0, in, in.getNumChannels() > 1 ? 1 : 0, 0, n);
            scL = scratch.getReadPointer (0); scR = scratch.getReadPointer (1);
        }
    }
    io.sidechain[0] = scL; io.sidechain[1] = scR;

    // ---- outputs
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    auto mainBus = getBusBuffer (buffer, false, 0);
    if (mainBus.getNumChannels() < 1 || n == 0) { midi.clear(); return; }
    io.main[0] = mainBus.getWritePointer (0);
    io.main[1] = mainBus.getNumChannels() > 1 ? mainBus.getWritePointer (1) : nullptr;
    for (int t = 0; t < kTracks; ++t)
    {
        if (t + 1 >= getBusCount (false)) break;
        auto* bus = getBus (false, t + 1);
        if (bus == nullptr || ! bus->isEnabled()) continue;
        auto bb = getBusBuffer (buffer, false, t + 1);
        if (bb.getNumChannels() >= 2) { io.trackOut[t][0] = bb.getWritePointer (0); io.trackOut[t][1] = bb.getWritePointer (1); }
    }

    // ---- transport
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            io.transport.valid = true;
            io.transport.playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm()) io.transport.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) io.transport.ppq = *ppq; else io.transport.playing = false;
            if (auto ts = pos->getTimeSignature()) { io.transport.tsNum = ts->numerator; io.transport.tsDen = ts->denominator; }
            if (auto bar = pos->getPpqPositionOfLastBarStart()) io.transport.barStartPpq = *bar;
            else
            {
                const double bpb = (double) io.transport.tsNum * 4.0 / (double) juce::jmax (1, io.transport.tsDen);
                io.transport.barStartPpq = std::floor (io.transport.ppq / bpb) * bpb;
            }
        }
    }

    // ---- MIDI in
    int numIn = 0;
    for (const auto meta : midi)
    {
        if (numIn >= (int) midiIn.size()) break;
        const auto msg = meta.getMessage();
        if (msg.getRawDataSize() < 1 || msg.getRawDataSize() > 3) continue;
        MidiMsg& m = midiIn[(size_t) numIn++];
        m.sample = juce::jlimit (0, n - 1, meta.samplePosition);
        const auto* raw = msg.getRawData();
        m.data[0] = raw[0]; m.data[1] = msg.getRawDataSize() > 1 ? raw[1] : 0; m.data[2] = msg.getRawDataSize() > 2 ? raw[2] : 0;
    }
    io.midiIn = midiIn.data(); io.numMidiIn = numIn;
    io.midiOut = midiOut.data(); io.midiOutCapacity = (int) midiOut.size();

    eng->process (io);

    midi.clear();
    for (int i = 0; i < io.numMidiOut; ++i)
        midi.addEvent (midiOut[(size_t) i].data, 3, midiOut[(size_t) i].sample);

    const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    const double load = elapsed / ((double) n / currentSampleRate);
    const float prev = cpu.load (std::memory_order_relaxed);
    cpu.store (prev + 0.1f * ((float) load - prev), std::memory_order_relaxed);
}

// ============================================================================ samples
SampleData* AutomataProcessor::decodeFile (const juce::File& file)
{
    static juce::AudioFormatManager mgr;
    static std::once_flag once;
    std::call_once (once, [] { mgr.registerBasicFormats(); });
    std::unique_ptr<juce::AudioFormatReader> reader (mgr.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0) return nullptr;
    const int maxLen = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 300.0));
    const int chans = juce::jlimit (1, 2, (int) reader->numChannels);
    juce::AudioBuffer<float> buf (chans, maxLen);
    reader->read (&buf, 0, maxLen, 0, true, chans > 1);
    const float* ptrs[2] = { buf.getReadPointer (0), buf.getReadPointer (chans > 1 ? 1 : 0) };
    return makeSampleData (ptrs, chans, maxLen, reader->sampleRate, file.getFileNameWithoutExtension().toRawUTF8());
}

void AutomataProcessor::loadSampleAsync (int track, const juce::File& file)
{
    // the pool is joined in the destructor, so `this` outlives the job; the async callback is guarded by a weak reference
    loaderPool.addJob ([this, track, file]
    {
        SampleData* data = decodeFile (file);
        if (data == nullptr) return;
        juce::MessageManager::callAsync ([weakThis = juce::WeakReference<AutomataProcessor> (this), this, track, file, data]
        {
            if (weakThis == nullptr) { delete data; return; }
            std::lock_guard<std::recursive_mutex> g (sess->mutex());
            const int id = sess->samples().addOwned (data, file.getFileNameWithoutExtension().toStdString(), file.getFullPathName().toStdString());
            TrackSound ts = sess->project().kit.tracks[track];
            ts.sampleId = id;
            if (ts.engine != Engine::Sampler && ts.engine != Engine::Granular)
            {
                ts.engine = Engine::Sampler;
                setEngineDefaults (ts, Engine::Sampler);
            }
            std::snprintf (ts.name, sizeof (ts.name), "%s", file.getFileNameWithoutExtension().toRawUTF8());
            sess->setTrackSound (track, ts);
        });
    });
}

// ============================================================================ state
void AutomataProcessor::saveState (juce::MemoryBlock& dest, bool embedSamples)
{
    std::lock_guard<std::recursive_mutex> g (sess->mutex());
    housekeeping();
    const std::string json = sess->toJson().dump();

    juce::MemoryOutputStream out (dest, false);
    out.write (kMagic, sizeof (kMagic));
    {
        juce::MemoryOutputStream gz;
        { juce::GZIPCompressorOutputStream z (gz, 6); z.write (json.data(), json.size()); z.flush(); }
        writeU32 (out, (uint32_t) json.size());
        writeU32 (out, (uint32_t) gz.getDataSize());
        out.write (gz.getData(), gz.getDataSize());
    }
    // embedded file samples (FLAC, 24 bit)
    std::vector<const SamplePool::Entry*> embed;
    if (embedSamples)
        for (const TrackSound& ts : sess->project().kit.tracks)
            if (const auto* e = sess->samples().find (ts.sampleId))
                if (e->kind == SamplePool::File && e->data != nullptr && e->data->length > 0
                    && e->data->length <= (int) (e->data->sampleRate * kMaxEmbedSeconds)
                    && std::find (embed.begin(), embed.end(), e) == embed.end())
                    embed.push_back (e);
    writeU32 (out, (uint32_t) embed.size());
    for (const auto* e : embed)
    {
        juce::MemoryBlock blob;
        {
            std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::MemoryOutputStream> (blob, false);
            juce::FlacAudioFormat flac;
            auto writer = flac.createWriterFor (os, juce::AudioFormatWriterOptions()
                                                        .withSampleRate (e->data->sampleRate)
                                                        .withNumChannels (e->data->numChannels)
                                                        .withBitsPerSample (24)
                                                        .withQualityOptionIndex (5));
            if (writer != nullptr)
            {
                const float* ch[2] = { e->data->data (0, 0), e->data->data (0, 1) };
                writer->writeFromFloatArrays (ch, e->data->numChannels, e->data->length);
                writer->flush();
            }
        }
        out.writeInt (e->id);
        writeU32 (out, (uint32_t) blob.getSize());
        out.write (blob.getData(), blob.getSize());
    }
    out.flush();
}

bool AutomataProcessor::loadState (const void* data, size_t size)
{
    if (data == nullptr || size < sizeof (kMagic) + 12 || std::memcmp (data, kMagic, sizeof (kMagic)) != 0) return false;
    juce::MemoryInputStream in (data, size, false);
    in.skipNextBytes ((juce::int64) sizeof (kMagic));
    const uint32_t jsonSize = (uint32_t) in.readInt();
    const uint32_t gzSize = (uint32_t) in.readInt();
    if (gzSize > in.getNumBytesRemaining() || jsonSize > 256u * 1024u * 1024u) return false;
    std::string json;
    {
        juce::MemoryBlock gz;
        in.readIntoMemoryBlock (gz, (ssize_t) gzSize);
        juce::MemoryInputStream gzIn (gz, false);
        juce::GZIPDecompressorInputStream z (gzIn);
        json.resize (jsonSize);
        const auto got = z.read (json.data(), (int) jsonSize);
        if (got != (int) jsonSize) return false;
    }
    bool ok = false;
    const Json j = Json::parse (json, &ok);
    if (! ok) return false;

    std::lock_guard<std::recursive_mutex> g (sess->mutex());
    sess->samples().clearEmbedded();
    if (in.getNumBytesRemaining() >= 4)
    {
        const uint32_t count = (uint32_t) in.readInt();
        for (uint32_t i = 0; i < count && in.getNumBytesRemaining() >= 8; ++i)
        {
            const int key = in.readInt();
            const uint32_t blobSize = (uint32_t) in.readInt();
            if (blobSize > in.getNumBytesRemaining()) break;
            juce::MemoryBlock blob;
            in.readIntoMemoryBlock (blob, (ssize_t) blobSize);
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (new juce::MemoryInputStream (blob, false), true));
            if (reader == nullptr || reader->lengthInSamples <= 0) continue;
            const int len = (int) reader->lengthInSamples;
            const int chans = juce::jlimit (1, 2, (int) reader->numChannels);
            juce::AudioBuffer<float> buf (chans, len);
            reader->read (&buf, 0, len, 0, true, chans > 1);
            const float* ptrs[2] = { buf.getReadPointer (0), buf.getReadPointer (chans > 1 ? 1 : 0) };
            sess->samples().embedded[key] = makeSampleData (ptrs, chans, len, reader->sampleRate, "");
        }
    }
    const bool loaded = sess->fromJson (j);
    sess->samples().clearEmbedded();
    return loaded;
}

void AutomataProcessor::getStateInformation (juce::MemoryBlock& dest) { saveState (dest, true); }
void AutomataProcessor::setStateInformation (const void* data, int size) { loadState (data, (size_t) juce::jmax (0, size)); }

bool AutomataProcessor::saveProjectFile (const juce::File& f)
{
    juce::MemoryBlock mb;
    saveState (mb, true);
    return f.replaceWithData (mb.getData(), mb.getSize());
}

bool AutomataProcessor::loadProjectFile (const juce::File& f)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) return false;
    return loadState (mb.getData(), mb.getSize());
}

juce::AudioProcessorEditor* AutomataProcessor::createEditor() { return new AutomataEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AutomataProcessor(); }
