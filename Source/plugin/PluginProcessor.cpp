#include "PluginProcessor.h"
#include "ui/PluginEditor.h"
#include "dsp/Sequencer.h"

using namespace juce;
using namespace splinter;

SplinterProcessor::SplinterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Params", createSplinterLayout())
{
    bridge.attach (apvts);
    bypassParam = dynamic_cast<AudioParameterBool*> (apvts.getParameter (pid::bypass));
    midiScratch.reserve (512);

    loops.onLoopChanged = [this] (bool keepSlices) { handleLoopChanged (keepSlices); };

    loadFactoryPreset (0);
    loops.loadDemo (0);
}

SplinterProcessor::~SplinterProcessor() { loops.onLoopChanged = nullptr; }

void SplinterProcessor::handleLoopChanged (bool keepSlices)
{
    const LoopInfo info = loops.getInfo();
    if (info.generation == handledGeneration) return;
    handledGeneration = info.generation;
    const PatternBank b = getBank();
    if (! keepSlices || std::fabs (b.slices.loopBeats - (float) info.beats) > 0.01f)
    {
        reslice();
        if (! keepSlices) applyDetectedKey();
    }
}

// ---------------------------------------------------------------- audio
void SplinterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (engine.latencySamples());
    scratch.setSize (2, jmax (16, samplesPerBlock * 2), false, true, true);
    dirty.store (allBits);
    bridge.fill (engineParams);
    loops.setTarget (sampleRate, internalBpm.load(), bridge.stretchMode());
}

bool SplinterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono()) return false;
    return in == out;
}

void SplinterProcessor::syncBankToEngine()
{
    if (dirty.load (std::memory_order_acquire) == 0) return;
    const SpinLock::ScopedTryLockType tl (bankLock);
    if (! tl.isLocked()) return;                       // UI is mid-edit: pick it up next block
    const uint64_t d = dirty.exchange (0, std::memory_order_acq_rel);
    for (int i = 0; i < kNumSlots; ++i)
    {
        if (d & timeBit (i)) engine.setTimeCurve (i, bank.time[i]);
        if (d & volBit (i)) engine.setVolCurve (i, bank.vol[i]);
        if (d & seqBit (i)) engine.setSeqPattern (i, bank.seq[i]);
    }
    if (d & slicesBit) engine.setSlices (bank.slices);
}

void SplinterProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);
    if (n <= 0 || numOut <= 0) { midi.clear(); return; }

    // ---- transport: host if it gives us tempo + position, otherwise our own clock
    TransportInfo tr;
    bool hostValid = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            const auto bpm = pos->getBpm(); const auto ppq = pos->getPpqPosition();
            if (bpm.hasValue() && ppq.hasValue() && *bpm > 1.0)
            {
                tr.bpm = *bpm; tr.ppq = *ppq; tr.playing = pos->getIsPlaying(); hostValid = true;
            }
        }
    if (hostValid) hostTransportSeen.store (true, std::memory_order_relaxed);
    else
    {
        tr.bpm = jlimit (30.0, 300.0, internalBpm.load (std::memory_order_relaxed));
        tr.playing = internalPlaying.load (std::memory_order_relaxed);
        tr.ppq = internalPpq;
        if (tr.playing) internalPpq += (double) n * tr.bpm / (60.0 * currentSampleRate);
        hostTransportSeen.store (false, std::memory_order_relaxed);
    }

    bridge.fill (engineParams);
    if (forceBypass) engineParams.bypass = true;
    loops.setTarget (currentSampleRate, tr.bpm, bridge.stretchMode());
    engine.setLoop (loops.acquire());
    syncBankToEngine();

    {
        const uint32_t want = uiHeldLanes.load (std::memory_order_relaxed);
        if (want != appliedHeldLanes)
        {
            for (int l = 0; l < kNumLanes; ++l)
                if (((want ^ appliedHeldLanes) >> l) & 1u) engine.holdLane (l, ((want >> l) & 1u) != 0);
            appliedHeldLanes = want;
        }
    }

    // ---- MIDI (plus notes from the on-screen keyboard)
    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    midiScratch.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (midiScratch.size() >= midiScratch.capacity()) break;
        MidiEvent e; e.offset = jlimit (0, n - 1, meta.samplePosition);
        if (m.isNoteOn()) { e.on = true; e.note = m.getNoteNumber(); e.velocity = m.getFloatVelocity(); midiScratch.push_back (e); }
        else if (m.isNoteOff()) { e.on = false; e.note = m.getNoteNumber(); midiScratch.push_back (e); }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { e.allOff = true; midiScratch.push_back (e); }
    }
    midi.clear();

    // ---- audio
    if (scratch.getNumSamples() < n) scratch.setSize (2, n, false, false, true);   // host exceeded its promised block size
    float* inL = scratch.getWritePointer (0); float* inR = scratch.getWritePointer (1);
    if (numIn >= 2) { FloatVectorOperations::copy (inL, buffer.getReadPointer (0), n); FloatVectorOperations::copy (inR, buffer.getReadPointer (1), n); }
    else if (numIn == 1) { FloatVectorOperations::copy (inL, buffer.getReadPointer (0), n); FloatVectorOperations::copy (inR, buffer.getReadPointer (0), n); }
    else { FloatVectorOperations::clear (inL, n); FloatVectorOperations::clear (inR, n); }

    if (numOut >= 2)
        engine.process (inL, inR, buffer.getWritePointer (0), buffer.getWritePointer (1), n, tr, engineParams, midiScratch.data(), (int) midiScratch.size());
    else
    {
        engine.process (inL, inR, inL, inR, n, tr, engineParams, midiScratch.data(), (int) midiScratch.size());
        float* o = buffer.getWritePointer (0);
        for (int i = 0; i < n; ++i) o[i] = 0.5f * (inL[i] + inR[i]);
    }
}

void SplinterProcessor::processBlockBypassed (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    // keep running so the latency-matched dry path and the capture buffer stay valid
    forceBypass = true;
    processBlock (buffer, midi);
    forceBypass = false;
}

// ---------------------------------------------------------------- bank
PatternBank SplinterProcessor::getBank() const
{
    const SpinLock::ScopedLockType sl (bankLock);
    return bank;
}

void SplinterProcessor::setSliceMode (int mode, float sensitivity)
{
    editBank ([&] (PatternBank& b) { b.sliceMode = mode; b.sliceSensitivity = sensitivity; }, 0);
    reslice();
}

void SplinterProcessor::reslice()
{
    const LoopInfo info = loops.getInfo();
    const PatternBank b = getBank();
    const float beats = (float) jmax (1.0, info.kind == LoopInfo::none ? 8.0 : info.beats);
    SliceSet s = b.slices;
    if (b.sliceMode == 0) s = loops.detectSlices (b.sliceSensitivity);
    else if (b.sliceMode >= 1 && b.sliceMode <= 4) { static const int div[5] = { 0, 1, 2, 4, 8 }; s.makeGrid (beats, div[b.sliceMode]); }
    else if (s.loopBeats > 0.0f && std::fabs (s.loopBeats - beats) > 0.01f)
    {
        const float k = beats / s.loopBeats;            // manual markers follow a length change
        for (int i = 0; i < s.count; ++i) s.startBeat[i] *= k;
    }
    s.loopBeats = beats;
    if (s.count <= 0) s.makeGrid (beats, 4);
    editBank ([&] (PatternBank& pb) { pb.slices = s; }, slicesBit);
}

void SplinterProcessor::applyDetectedKey()
{
    const LoopInfo info = loops.getInfo();
    if (info.kind == LoopInfo::none || info.keyConfidence < 0.3f) return;
    setParam (pid::key, (float) info.key);
    setParam (pid::scale, (float) info.scale);
}

void SplinterProcessor::captureLiveInput()
{
    // read a consistent (beat, ring index) pair published by the audio thread
    int64_t written = -1; double beat = 0.0;
    for (int tries = 0; tries < 200; ++tries)
    {
        const int64_t w1 = engine.visual.ringWritten.load (std::memory_order_acquire);
        beat = engine.visual.beat.load (std::memory_order_relaxed);
        const int64_t w2 = engine.visual.ringWritten.load (std::memory_order_acquire);
        if (w1 >= 0 && w1 == w2) { written = w1; break; }
        Thread::yield();
    }
    if (written <= 0) return;
    const double bpm = engine.visual.bpm.load();
    const double spb = currentSampleRate * 60.0 / bpm;
    const int bars = jlimit (1, 8, (int) getParam (pid::liveBars));
    const double endBeat = std::floor (beat / 4.0) * 4.0;
    const int64_t endIdx = written - (int64_t) std::llround ((beat - endBeat) * spb);
    const int64_t len = (int64_t) std::llround (bars * 4.0 * spb);
    const auto& ring = engine.getLiveRing();
    if (len < 256 || endIdx - len < 0 || written - (endIdx - len) > ring.capacity() - 1024) return;   // not enough recorded yet
    splinter::AudioData a; a.sampleRate = currentSampleRate; a.resize (len);
    float peak = 0.0f;
    for (int64_t i = 0; i < len; ++i)
        for (int c = 0; c < 2; ++c) { const float v = ring.sampleAbs (c, endIdx - len + i); a.ch[c][(size_t) i] = v; peak = jmax (peak, std::fabs (v)); }
    if (peak < 1.0e-5f) return;                          // nothing but silence came in
    // blend the bar *before* the capture into its tail, so the end of the loop
    // flows into its own start without a seam
    const int fade = (int) jmin<int64_t> (len / 8, (int64_t) (0.012 * currentSampleRate));
    if (endIdx - len - fade >= 0)
        for (int i = 0; i < fade; ++i)
        {
            float g = (float) (i + 1) / (float) (fade + 1); g = g * g * (3.0f - 2.0f * g);
            for (int c = 0; c < 2; ++c)
            {
                const float pre = ring.sampleAbs (c, endIdx - len - fade + i);
                float& tail = a.ch[c][(size_t) (len - fade + i)];
                tail += g * (pre - tail);
            }
        }
    loops.loadAudio (std::move (a), "Capture " + Time::getCurrentTime().toString (false, true, true, true), bpm, LoopInfo::capture);
    setParam (pid::sourceMode, 0.0f);
}

// ---------------------------------------------------------------- parameters
void SplinterProcessor::setParam (const String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        const float norm = p->convertTo0to1 (realValue);
        if (std::fabs (norm - p->getValue()) > 1.0e-7f)
        {
            p->beginChangeGesture(); p->setValueNotifyingHost (norm); p->endChangeGesture();
        }
    }
}

float SplinterProcessor::getParam (const String& id) const
{
    if (auto* v = apvts.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

// ---------------------------------------------------------------- presets
void SplinterProcessor::loadPresetData (const PresetData& d)
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
        {
            if (presets::isSessionParam (rp->getParameterID())) continue;
            float norm = rp->getDefaultValue();
            for (auto& kv : d.params) if (kv.first == rp->getParameterID()) { norm = rp->convertTo0to1 (kv.second); break; }
            if (std::fabs (norm - rp->getValue()) > 1.0e-7f) rp->setValueNotifyingHost (norm);
        }
    editBank ([&] (PatternBank& b)
    {
        for (int i = 0; i < kNumSlots; ++i) { b.time[i] = d.bank.time[i]; b.vol[i] = d.bank.vol[i]; b.seq[i] = d.bank.seq[i]; }
    }, allBits & ~slicesBit);
    presetName = d.name;
}

void SplinterProcessor::loadFactoryPreset (int index)
{
    PresetData d;
    presets::buildFactory (jlimit (0, presets::numFactory() - 1, index), d);
    loadPresetData (d);
    factoryIndex = index;
}

bool SplinterProcessor::saveUserPreset (const String& name)
{
    const File dir = presets::userDirectory();
    if (! dir.createDirectory()) return false;
    presetName = name; factoryIndex = -1;
    const ValueTree t = buildState (true);
    if (auto xml = t.createXml()) return xml->writeTo (dir.getChildFile (File::createLegalFileName (name) + ".splinter"));
    return false;
}

bool SplinterProcessor::loadUserPreset (const File& f)
{
    if (auto xml = XmlDocument::parse (f))
    {
        const ValueTree t = ValueTree::fromXml (*xml);
        if (! t.hasType ("Splinter")) return false;
        restoreState (t, true);
        presetName = f.getFileNameWithoutExtension(); factoryIndex = -1;
        return true;
    }
    return false;
}

void SplinterProcessor::generatePattern (int slot, uint32_t seed)
{
    slot = jlimit (0, kNumSlots - 1, slot);
    editBank ([&] (PatternBank& b)
    {
        SeqPattern& p = b.seq[slot];
        static const float density[kNumLanes] = { 0.7f, 0.8f, 0.6f, 0.9f, 0.5f, 0.5f, 0.35f, 0.3f, 0.5f, 0.4f, 0.8f, 0.7f, 0.0f };
        const int spb = jmax (1, p.stepsPerBeat);
        for (int lane = 0; lane < kNumLanes; ++lane)
        {
            if (p.locked[lane] || density[lane] <= 0.0f) continue;
            for (int k = 0; k < kMaxSteps; ++k) p.steps[lane][k] = Step();
            const int glen = seqdetail::genLength (lane, spb);
            for (int a = 0; a < p.numSteps; a += glen)
            {
                const double beat = (double) a / spb;
                const int pos16 = (int) std::floor (floorMod (beat, 4.0) * 4.0 + 1.0e-6) & 15;
                const int64_t bar = (int64_t) std::floor (beat / 4.0) + (p.numSteps <= 16 ? 3 : 2);   // treat the pattern end as a phrase end
                const uint32_t h = hash3 (seed, (uint32_t) lane * 977U + 3U, (uint32_t) a);
                if (hashToUnit (h) >= density[lane] * seqdetail::genWeight (lane, pos16, bar)) continue;
                const int var = seqdetail::randomVar (lane, hash32 (h + 5U));
                const int prob = 55 + (int) (hashToUnit (hash32 (h + 9U)) * 46.0f);
                for (int k = a; k < jmin (p.numSteps, a + glen); ++k) { p.steps[lane][k].on = 1; p.steps[lane][k].var = (uint8_t) var; p.steps[lane][k].prob = (uint8_t) prob; }
            }
        }
    }, seqBit (slot));
}

// ---------------------------------------------------------------- state
ValueTree SplinterProcessor::buildState (bool forPreset) const
{
    ValueTree root ("Splinter");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("preset", presetName, nullptr);
    root.setProperty ("factoryIndex", factoryIndex, nullptr);
    ValueTree params = const_cast<AudioProcessorValueTreeState&> (apvts).copyState();
    root.appendChild (params, nullptr);
    root.appendChild (getBank().toTree (! forPreset), nullptr);
    if (! forPreset)
    {
        root.setProperty ("uiW", editorWidth.load(), nullptr);
        root.setProperty ("uiH", editorHeight.load(), nullptr);
        root.setProperty ("uiPage", editorPage.load(), nullptr);
        root.setProperty ("intBpm", internalBpm.load(), nullptr);
        root.setProperty ("intPlay", internalPlaying.load(), nullptr);
        loops.writeState (root);
    }
    return root;
}

void SplinterProcessor::restoreState (const ValueTree& root, bool isPreset)
{
    const ValueTree params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
    {
        if (isPreset)
        {
            // presets leave session parameters (source, key, bypass ...) alone
            for (const auto& child : params)
            {
                const String id = child["id"].toString();
                if (presets::isSessionParam (id)) continue;
                if (auto* p = apvts.getParameter (id))
                    p->setValueNotifyingHost (p->convertTo0to1 ((float) child["value"]));
            }
        }
        else apvts.replaceState (params.createCopy());
    }
    const ValueTree pats = root.getChildWithName ("Patterns");
    editBank ([&] (PatternBank& b) { b.fromTree (pats, ! isPreset); }, isPreset ? (allBits & ~slicesBit) : allBits);
    presetName = root.getProperty ("preset", presetName).toString();
    factoryIndex = root.getProperty ("factoryIndex", -1);
    if (! isPreset)
    {
        editorWidth.store (jlimit (700, 2400, (int) root.getProperty ("uiW", 1040)));
        editorHeight.store (jlimit (460, 1600, (int) root.getProperty ("uiH", 680)));
        editorPage.store ((int) root.getProperty ("uiPage", 0));
        internalBpm.store (jlimit (30.0, 300.0, (double) root.getProperty ("intBpm", 120.0)));
        internalPlaying.store ((bool) root.getProperty ("intPlay", true));
        loops.readState (root);
    }
}

void SplinterProcessor::getStateInformation (MemoryBlock& dest)
{
    MemoryOutputStream out (dest, false);
    buildState (false).writeToStream (out);
}

void SplinterProcessor::setStateInformation (const void* data, int size)
{
    const ValueTree t = ValueTree::readFromData (data, (size_t) size);
    if (t.isValid() && t.hasType ("Splinter")) restoreState (t, false);
}

AudioProcessorEditor* SplinterProcessor::createEditor() { return new SplinterEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SplinterProcessor(); }
