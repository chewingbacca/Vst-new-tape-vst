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

    currentPresetName = factoryPresets().front().name;
    applyValues (factoryPresets().front().resolve());
}

//==============================================================================
bool VerdantProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo())
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void VerdantProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (engine.latencySamples());
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
    const int numCh = buffer.getNumChannels();
    if (n == 0 || numCh < 2)
        return;

    // Mono input feeds both sides.
    if (numIn < 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    for (int ch = 2; ch < numCh; ++ch)
        buffer.clear (ch, 0, n);

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

    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, transport);
}

//==============================================================================
juce::AudioProcessorEditor* VerdantProcessor::createEditor()
{
    return new VerdantEditor (*this);
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
    currentPresetName = fp[size_t (index)].name;
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
            currentPresetName = info.name;
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

    currentPresetName = safe;
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
    state.setProperty ("presetName", currentPresetName, nullptr);
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

            currentPresetName = state.getProperty ("presetName", currentPresetName).toString();
            currentProgram = juce::jlimit (0, juce::jmax (0, getNumPrograms() - 1), int (state.getProperty ("program", 0)));
            editorWidth  = int (state.getProperty ("editorWidth", 0));
            editorHeight = int (state.getProperty ("editorHeight", 0));
            editorPage   = int (state.getProperty ("editorPage", 0));
            apvts.replaceState (state);
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VerdantProcessor();
}
