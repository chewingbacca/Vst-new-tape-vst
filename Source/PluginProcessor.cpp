#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "dsp/Presets.h"

namespace
{
juce::String formatValue (const swoon::ParamDef& d, float v)
{
    const juce::String unit (d.unit);

    if (unit == "%")
    {
        // 0..1 shown as 0..100 %, bipolar/2x ranges scale the same way
        return juce::String (juce::roundToInt (v * 100.0f)) + " %";
    }
    if (unit == "dB")
        return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
    if (unit == "Hz")
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                            : juce::String (v, v < 10.0f ? 2 : 0) + " Hz";
    if (unit == "ms")
        return juce::String (juce::roundToInt (v)) + " ms";
    if (unit == "s")
        return juce::String (v, v < 10.0f ? 2 : 1) + " s";
    if (unit == "ct")
        return juce::String (v, 1) + " ct";

    return juce::String (v, 2);
}
/** On/off parameter that always holds exactly 0 or 1.

    juce::AudioParameterBool stores whatever normalised value the host sends (say 0.58). The value-tree state
    only sees the snapped value, so restoring "on" over a stored 0.58 is treated as no change and the raw value
    is left behind - hosts and validators then read back something that was never saved. Snapping on the way
    in keeps the parameter, the state and the host in agreement. */
class SwitchParameter final : public juce::RangedAudioParameter
{
public:
    SwitchParameter (const juce::ParameterID& pid, const juce::String& paramName, bool defaultOn)
        : RangedAudioParameter (pid, paramName),
          value (defaultOn ? 1.0f : 0.0f),
          defaultValue (defaultOn ? 1.0f : 0.0f)
    {
    }

    float getValue() const override { return value.load (std::memory_order_relaxed); }
    void setValue (float newValue) override { value.store (newValue >= 0.5f ? 1.0f : 0.0f, std::memory_order_relaxed); }
    float getDefaultValue() const override { return defaultValue; }
    int getNumSteps() const override { return 2; }
    bool isDiscrete() const override { return true; }
    bool isBoolean() const override { return true; }
    const juce::NormalisableRange<float>& getNormalisableRange() const override { return range; }

    juce::String getText (float v, int) const override { return v >= 0.5f ? "On" : "Off"; }
    float getValueForText (const juce::String& text) const override
    {
        const auto t = text.trim().toLowerCase();
        return (t == "on" || t == "yes" || t == "true" || t.getFloatValue() >= 0.5f) ? 1.0f : 0.0f;
    }

private:
    std::atomic<float> value;
    const float defaultValue;
    const juce::NormalisableRange<float> range { 0.0f, 1.0f, 1.0f };
};
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout SwoonAudioProcessor::createParameterLayout()
{
    using namespace swoon;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& d : paramDefs())
    {
        const juce::ParameterID pid { d.id, 1 };

        switch (d.kind)
        {
            case ParamKind::Float:
            {
                juce::NormalisableRange<float> range (d.min, d.max);
                if (d.skewMid > d.min && d.skewMid < d.max)
                    range.setSkewForCentre (d.skewMid);

                const auto* defPtr = &d;
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    pid, d.name, range, d.def,
                    juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([defPtr] (float v, int) { return formatValue (*defPtr, v); })
                        .withValueFromStringFunction ([defPtr] (const juce::String& s)
                        {
                            auto v = s.retainCharacters ("0123456789.-+").getFloatValue();
                            const juce::String unit (defPtr->unit);
                            if (unit == "%")
                                v *= 0.01f;
                            else if (unit == "Hz" && s.containsIgnoreCase ("k"))
                                v *= 1000.0f;
                            return v;
                        })));
                break;
            }
            case ParamKind::Int:
                layout.add (std::make_unique<juce::AudioParameterInt> (pid, d.name, (int) d.min, (int) d.max, (int) d.def));
                break;
            case ParamKind::Bool:
                layout.add (std::make_unique<SwitchParameter> (pid, d.name, d.def > 0.5f));
                break;
            case ParamKind::Choice:
                layout.add (std::make_unique<juce::AudioParameterChoice> (
                    pid, d.name, juce::StringArray::fromTokens (d.choices, "|", ""), (int) d.def));
                break;
        }
    }

    return layout;
}

//==============================================================================
SwoonAudioProcessor::SwoonAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SwoonState", createParameterLayout())
{
    for (const auto& d : swoon::paramDefs())
    {
        const auto i = static_cast<size_t> (d.index);
        rawParams[i] = apvts.getRawParameterValue (d.id);
        rangedParams[i] = apvts.getParameter (d.id);
        jassert (rawParams[i] != nullptr && rangedParams[i] != nullptr);
    }

    bypassParam = apvts.getParameter ("bypass");
    presetManager = std::make_unique<PresetManager> (*this);
}

SwoonAudioProcessor::~SwoonAudioProcessor() = default;

//==============================================================================
void SwoonAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, juce::jmax (1, samplesPerBlock));
    setLatencySamples (engine.getLatencySamples());
    stereoScratch.setSize (2, juce::jmax (1, samplesPerBlock), false, false, true);

    vizState = {};
    vizState.lowCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 250.0f / (float) sampleRate);
    vizState.highCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 2500.0f / (float) sampleRate);
    vizState.envCoeff = 1.0f - std::exp (-1.0f / (0.08f * (float) sampleRate));
    vizState.peakDecay = std::exp (-1.0f / (0.35f * (float) sampleRate));
    vizState.decim = juce::jmax (1, juce::roundToInt (sampleRate / 6000.0));
}

void SwoonAudioProcessor::releaseResources() {}

double SwoonAudioProcessor::getTailLengthSeconds() const
{
    // Hosts use this to decide how long to keep rendering after the input stops, so follow the settings:
    // the hall's decay (as scaled by the Wash macro) dominates; delays and the reverse buffer need a few seconds.
    const auto value = [this] (swoon::P p) { return rawParams[static_cast<size_t> (p)]->load (std::memory_order_relaxed); };

    double tail = 6.0;
    if (value (swoon::P::washOn) > 0.5f && value (swoon::P::wash) > 0.0f)
    {
        if (value (swoon::P::washFreeze) > 0.5f)
            return 600.0;
        const double decay = value (swoon::P::washDecay) * std::pow (2.0, (value (swoon::P::wash) - 0.5) * 3.0);
        tail = juce::jmax (tail, juce::jmin (decay, 120.0) * (value (swoon::P::washShimmer) > 0.01f ? 1.6 : 1.1) + 2.0);
    }
    return tail;
}

void SwoonAudioProcessor::reset()
{
    engine.reset();
}

bool SwoonAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    // mono->mono, mono->stereo, stereo->stereo
    return ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

void SwoonAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer, false);
}

void SwoonAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Keep the engine running (so tails and latency stay consistent) but force its
    // internal, click-free bypass path.
    processInternal (buffer, true);
}

void SwoonAudioProcessor::processInternal (juce::AudioBuffer<float>& buffer, bool hostBypassed)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    if (numSamples == 0 || numOut == 0)
        return;

    for (size_t i = 0; i < rawParams.size(); ++i)
        paramValues.v[i] = rawParams[i]->load (std::memory_order_relaxed);

    if (hostBypassed)
        paramValues[swoon::P::bypass] = 1.0f;

    // The host may hand us more samples than it promised in prepareToPlay; process in slices.
    const int maxSlice = stereoScratch.getNumSamples();
    float* scratchL = stereoScratch.getWritePointer (0);
    float* scratchR = stereoScratch.getWritePointer (1);

    for (int offset = 0; offset < numSamples; offset += maxSlice)
    {
        const int n = juce::jmin (maxSlice, numSamples - offset);

        const float* inL = numIn > 0 ? buffer.getReadPointer (0, offset) : nullptr;
        const float* inR = numIn > 1 ? buffer.getReadPointer (1, offset) : inL;

        if (inL != nullptr)
        {
            juce::FloatVectorOperations::copy (scratchL, inL, n);
            juce::FloatVectorOperations::copy (scratchR, inR, n);
        }
        else
        {
            juce::FloatVectorOperations::clear (scratchL, n);
            juce::FloatVectorOperations::clear (scratchR, n);
        }

        engine.process (scratchL, scratchR, n, paramValues);
        updateViz (scratchL, scratchR, n);

        if (numOut >= 2)
        {
            buffer.copyFrom (0, offset, scratchL, n);
            buffer.copyFrom (1, offset, scratchR, n);
        }
        else
        {
            float* out = buffer.getWritePointer (0, offset);
            for (int i = 0; i < n; ++i)
                out[i] = 0.5f * (scratchL[i] + scratchR[i]);
        }
    }

    for (int ch = 2; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);
}

void SwoonAudioProcessor::updateViz (const float* l, const float* r, int n)
{
    auto& s = vizState;

    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]);
        s.lp1 += s.lowCoeff * (m - s.lp1);
        s.lp2 += s.highCoeff * (m - s.lp2);
        const float low = s.lp1, mid = s.lp2 - s.lp1, high = m - s.lp2;
        s.eLow += s.envCoeff * (low * low - s.eLow);
        s.eMid += s.envCoeff * (mid * mid - s.eMid);
        s.eHigh += s.envCoeff * (high * high - s.eHigh);

        s.peakL = juce::jmax (std::abs (l[i]), s.peakL * s.peakDecay);
        s.peakR = juce::jmax (std::abs (r[i]), s.peakR * s.peakDecay);

        s.accL += l[i];
        s.accR += r[i];
        if (++s.decimCount >= s.decim)
        {
            const float inv = 1.0f / (float) s.decim;
            viz.pushScope (s.accL * inv, s.accR * inv);
            s.accL = s.accR = 0.0f;
            s.decimCount = 0;
        }
    }

    const auto& meters = engine.getMeters();
    s.peakIn = juce::jmax (meters.inPeak, s.peakIn * std::pow (s.peakDecay, (float) n));

    viz.inLevel.store (s.peakIn, std::memory_order_relaxed);
    viz.outLevelL.store (s.peakL, std::memory_order_relaxed);
    viz.outLevelR.store (s.peakR, std::memory_order_relaxed);
    viz.limiterReductionDb.store (meters.limiterReductionDb, std::memory_order_relaxed);
    viz.bandLow.store (std::sqrt (s.eLow), std::memory_order_relaxed);
    viz.bandMid.store (std::sqrt (s.eMid), std::memory_order_relaxed);
    viz.bandHigh.store (std::sqrt (s.eHigh), std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* SwoonAudioProcessor::createEditor()
{
    return new SwoonAudioProcessorEditor (*this);
}

//==============================================================================
int SwoonAudioProcessor::getNumPrograms() { return presetManager->getNumFactoryPresets(); }
int SwoonAudioProcessor::getCurrentProgram() { return juce::jmax (0, presetManager->getCurrentFactoryIndex()); }
void SwoonAudioProcessor::setCurrentProgram (int index) { presetManager->loadFactoryPreset (index); }
const juce::String SwoonAudioProcessor::getProgramName (int index) { return presetManager->getFactoryPresetName (index); }

//==============================================================================
juce::ValueTree SwoonAudioProcessor::getUiState()
{
    return apvts.state.getOrCreateChildWithName ("UI", nullptr);
}

void SwoonAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
    state.setProperty ("presetName", presetManager->getCurrentName(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SwoonAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (state);
            presetManager->restoreCurrentName (state.getProperty ("presetName", "").toString());
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SwoonAudioProcessor();
}
