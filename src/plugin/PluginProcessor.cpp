#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace verdant;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout VerdantProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < kNumParams; ++i)
    {
        const ParamDef& d = kParams[i];
        const juce::ParameterID pid { d.id, 1 };

        switch (d.type)
        {
            case PType::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, d.name, d.def >= 0.5f));
                break;

            case PType::Choice:
            {
                juce::StringArray choices;
                for (int c = 0; c < d.numChoices; ++c)
                    choices.add (d.choices[c]);
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, d.name, choices, int (d.def)));
                break;
            }

            case PType::Int:
                layout.add (std::make_unique<juce::AudioParameterInt> (
                    pid, d.name, int (d.min), int (d.max), int (d.def),
                    juce::AudioParameterIntAttributes().withStringFromValueFunction (
                        [i] (int v, int)
                        {
                            char buf[48];
                            formatValue (kParams[i], float (v), buf, sizeof (buf));
                            return juce::String (buf);
                        })));
                break;

            case PType::Float:
            {
                juce::NormalisableRange<float> range (d.min, d.max);
                if (d.centre > d.min && d.centre < d.max)
                    range.setSkewForCentre (d.centre);

                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    pid, d.name, range, d.def,
                    juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([i] (float v, int)
                        {
                            char buf[48];
                            formatValue (kParams[i], v, buf, sizeof (buf));
                            return juce::String (buf);
                        })
                        .withValueFromStringFunction ([i] (const juce::String& s)
                        {
                            const ParamDef& pd = kParams[i];
                            if (pd.pid == pRevDecay && s.trim().startsWithIgnoreCase ("inf"))
                                return pd.max;
                            float v = s.retainCharacters ("0123456789.-+").getFloatValue();
                            if (pd.unit == PUnit::Percent || pd.unit == PUnit::Bipolar)
                                v *= 0.01f;
                            else if (pd.unit == PUnit::Hz && s.containsIgnoreCase ("k"))
                                v *= 1000.0f;
                            return juce::jlimit (pd.min, pd.max, v);
                        })));
                break;
            }
        }
    }

    return layout;
}

//==============================================================================
VerdantProcessor::VerdantProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VerdantState", createLayout())
{
    for (int i = 0; i < kNumParams; ++i)
    {
        raw[size_t (i)]    = apvts.getRawParameterValue (kParams[i].id);
        params[size_t (i)] = apvts.getParameter (kParams[i].id);
        jassert (raw[size_t (i)] != nullptr && params[size_t (i)] != nullptr);
    }

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (kParams[pBypass].id));

    setCurrentPresetName (factoryPresets().front().name);
    applyValues (factoryPresets().front().resolve());

    // Announce a plausible latency straight away; prepareToPlay() sets the exact value.
    setLatencySamples (Engine::latencyForSampleRate (48000.0));
}

//==============================================================================
bool VerdantProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();

    // stereo -> stereo, mono -> stereo and (for mono tracks in hosts that insist) mono -> mono
    if (out == stereo) return in == mono || in == stereo;
    if (out == mono)   return in == mono;
    return false;
}

void VerdantProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (engine.latencySamples());

    // The engine always works in stereo; this scratch buffer serves mono layouts.
    scratch.setSize (2, juce::jmax (64, samplesPerBlock), false, false, true);
}

void VerdantProcessor::reset()
{
    engine.reset();
}

void VerdantProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numCh = buffer.getNumChannels();
    if (n == 0 || numCh == 0)
        return;

    ParamValues pv;
    for (int i = 0; i < kNumParams; ++i)
        pv[i] = raw[size_t (i)]->load (std::memory_order_relaxed);
    engine.setParams (pv);

    Transport transport;
    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            if (const auto bpm = pos->getBpm(); bpm.hasValue() && *bpm > 1.0)
                transport.bpm = *bpm;
            if (const auto ppq = pos->getPpqPosition(); ppq.hasValue())
            {
                transport.ppq = *ppq;
                transport.playing = pos->getIsPlaying();
            }
        }
    }

    if (numCh >= 2 && numOut >= 2)
    {
        // Mono input feeds both sides.
        if (numIn < 2)
            buffer.copyFrom (1, 0, buffer, 0, 0, n);

        for (int ch = 2; ch < numCh; ++ch)
            buffer.clear (ch, 0, n);

        engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, transport);
        return;
    }

    // Mono in / mono out: run the stereo engine on a scratch buffer and fold down.
    // Processed in pieces so a host sending more than the announced block size is still safe.
    const int cap = scratch.getNumSamples();
    if (cap == 0)
        return;     // not prepared
    auto* mono = buffer.getWritePointer (0);
    const double beatsPerSample = transport.bpm / (60.0 * getSampleRate());
    for (int done = 0; done < n;)
    {
        const int m = juce::jmin (cap, n - done);
        auto* l = scratch.getWritePointer (0);
        auto* r = scratch.getWritePointer (1);
        juce::FloatVectorOperations::copy (l, mono + done, m);
        juce::FloatVectorOperations::copy (r, mono + done, m);
        engine.process (l, r, m, transport);
        for (int i = 0; i < m; ++i)
            mono[done + i] = 0.5f * (l[i] + r[i]);
        done += m;
        transport.ppq += double (m) * beatsPerSample;
    }
}

//==============================================================================
juce::AudioProcessorEditor* VerdantProcessor::createEditor()
{
    return new VerdantEditor (*this);
}

//==============================================================================
double VerdantProcessor::getTailLengthSeconds() const
{
    // Used by hosts for offline bounces and "stop when silent": follow the reverb decay.
    const bool revOn = raw[pRevOn]->load() >= 0.5f;
    const float decay = raw[pRevDecay]->load();
    if (revOn && (decay > kRevDecayInf || raw[pRevFreeze]->load() >= 0.5f))
        return 60.0;

    double tail = 6.0;      // grains and arp linger
    if (revOn)
        tail += 1.3 * double (decay) * double (macroScale (raw[pMoisture]->load(), 0.4f, 2.5f));
    return juce::jlimit (3.0, 60.0, tail);
}

//==============================================================================
int VerdantProcessor::getNumPrograms()
{
    return int (factoryPresets().size());
}

const juce::String VerdantProcessor::getProgramName (int index)
{
    const auto& fp = factoryPresets();
    if (index >= 0 && index < int (fp.size()))
        return fp[size_t (index)].name;
    return {};
}

void VerdantProcessor::setCurrentProgram (int index)
{
    loadFactoryPreset (index);
}

void VerdantProcessor::applyValues (const ParamValues& values)
{
    for (int i = 0; i < kNumParams; ++i)
    {
        if (i == pBypass)
            continue; // a preset never changes the bypass state

        auto* p = params[size_t (i)];
        const float norm = p->convertTo0to1 (values[i]);
        if (std::abs (p->getValue() - norm) > 1.0e-7f)
            p->setValueNotifyingHost (norm);
    }
}

void VerdantProcessor::loadFactoryPreset (int index)
{
    const auto& fp = factoryPresets();
    if (index < 0 || index >= int (fp.size()))
        return;

    currentProgram = index;
    setCurrentPresetName (fp[size_t (index)].name);
    applyValues (fp[size_t (index)].resolve());
}

juce::File VerdantProcessor::getUserPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Verdant")
        .getChildFile ("Presets");
}

juce::Array<VerdantProcessor::PresetInfo> VerdantProcessor::getPresetList() const
{
    juce::Array<PresetInfo> list;

    for (const auto& fp : factoryPresets())
        list.add ({ fp.name, fp.category, fp.description, true, {} });

    const auto dir = getUserPresetDirectory();
    if (dir.isDirectory())
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, "*.verdant");
        files.sort();
        for (const auto& f : files)
            list.add ({ f.getFileNameWithoutExtension(), "User", "User preset", false, f });
    }

    return list;
}

void VerdantProcessor::loadPreset (const PresetInfo& info)
{
    if (info.factory)
    {
        const auto& fp = factoryPresets();
        for (int i = 0; i < int (fp.size()); ++i)
            if (info.name == fp[size_t (i)].name)
            {
                loadFactoryPreset (i);
                return;
            }
        return;
    }

    if (auto xml = juce::XmlDocument::parse (info.file))
    {
        if (xml->hasTagName ("VerdantPreset"))
        {
            ParamValues values;
            for (auto* e : xml->getChildWithTagNameIterator ("Param"))
            {
                const int idx = findParam (e->getStringAttribute ("id").toRawUTF8());
                if (idx >= 0)
                    values[idx] = juce::jlimit (kParams[idx].min, kParams[idx].max,
                                                float (e->getDoubleAttribute ("value", double (kParams[idx].def))));
            }
            setCurrentPresetName (info.name);
            applyValues (values);
        }
    }
}

bool VerdantProcessor::saveUserPreset (const juce::String& name)
{
    const auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty())
        return false;

    const auto dir = getUserPresetDirectory();
    if (! dir.isDirectory() && ! dir.createDirectory())
        return false;

    juce::XmlElement xml ("VerdantPreset");
    xml.setAttribute ("name", safe);
    xml.setAttribute ("version", 1);
    for (int i = 0; i < kNumParams; ++i)
    {
        if (i == pBypass)
            continue;
        auto* e = xml.createNewChildElement ("Param");
        e->setAttribute ("id", kParams[i].id);
        e->setAttribute ("value", double (raw[size_t (i)]->load()));
    }

    if (! xml.writeTo (dir.getChildFile (safe + ".verdant")))
        return false;

    setCurrentPresetName (safe);
    return true;
}

bool VerdantProcessor::deleteUserPreset (const PresetInfo& info)
{
    return ! info.factory && info.file.existsAsFile() && info.file.deleteFile();
}

//==============================================================================
void VerdantProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", getCurrentPresetName(), nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("editorWidth", editorWidth, nullptr);
    state.setProperty ("editorHeight", editorHeight, nullptr);
    state.setProperty ("editorPage", editorPage, nullptr);
    state.setProperty ("stateVersion", 1, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VerdantProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            if (! state.isValid())
                return;

            setCurrentPresetName (state.getProperty ("presetName", getCurrentPresetName()).toString());
            currentProgram = juce::jlimit (0, juce::jmax (0, getNumPrograms() - 1), int (state.getProperty ("program", 0)));
            editorWidth  = int (state.getProperty ("editorWidth", 0));
            editorHeight = int (state.getProperty ("editorHeight", 0));
            editorPage   = int (state.getProperty ("editorPage", 0));
            apvts.replaceState (state);

            // replaceState() leaves a parameter alone when its *snapped* value already matches
            // the state. A host may have left a Bool at an un-snapped value such as 0.8
            // (AudioParameterBool stores what it is given), so re-assert every parameter;
            // otherwise the host-visible value is not restored (pluginval: "not restored on
            // setStateInformation").
            for (int i = 0; i < kNumParams; ++i)
            {
                auto* p = params[size_t (i)];
                const float norm = p->convertTo0to1 (raw[size_t (i)]->load());
                if (std::abs (p->getValue() - norm) > 1.0e-6f)
                    p->setValueNotifyingHost (norm);
            }
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VerdantProcessor();
}
