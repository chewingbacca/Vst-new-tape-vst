#include "Parameters.h"

using namespace juce;
using splinter::EngineParams;

namespace
{
using Layout = AudioProcessorValueTreeState::ParameterLayout;

std::unique_ptr<RangedAudioParameter> fl (const char* id, const String& name, float lo, float hi, float def, const String& unit = {}, float skewCentre = 0.0f, int decimals = 2)
{
    NormalisableRange<float> r (lo, hi);
    if (skewCentre > 0.0f) r.setSkewForCentre (skewCentre);
    return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
        AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction ([decimals, unit] (float v, int) { return String (v, decimals) + (unit.isEmpty() ? String() : " " + unit); }));
}
std::unique_ptr<RangedAudioParameter> pct (const char* id, const String& name, float def)
{
    return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })
                                       .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue() / 100.0f; }));
}
std::unique_ptr<RangedAudioParameter> in (const String& id, const String& name, int lo, int hi, int def)
{
    return std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def);
}
std::unique_ptr<RangedAudioParameter> ch (const char* id, const String& name, StringArray choices, int def)
{
    return std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, def);
}
std::unique_ptr<RangedAudioParameter> bo (const String& id, const String& name, bool def)
{
    return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def);
}
} // namespace

Layout createSplinterLayout()
{
    Layout l;
    StringArray divs; for (int i = 0; i < splinter::kNumStutterDivs; ++i) divs.add (splinter::stutterDivName (i));
    StringArray ddivs; for (int i = 0; i < 6; ++i) ddivs.add (splinter::delayDivName (i));
    StringArray scales; for (int i = 0; i < splinter::kNumScales; ++i) scales.add (splinter::scaleDef (i).name);
    const StringArray keys { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    auto macros = std::make_unique<AudioProcessorParameterGroup> ("macros", "Macros", "|");
    macros->addChild (pct (pid::chop, "Chop", 0.5f), pct (pid::stutter, "Stutter", 0.5f), pct (pid::warp, "Warp", 0.5f),
                      pct (pid::harmony, "Harmony", 0.0f), pct (pid::crush, "Crush", 0.5f), pct (pid::chaos, "Chaos", 0.0f), pct (pid::mix, "Mix", 1.0f));
    l.add (std::move (macros));

    auto global = std::make_unique<AudioProcessorParameterGroup> ("global", "Global", "|");
    global->addChild (ch (pid::sourceMode, "Source", { "Loop", "Live Input" }, 0), in (pid::liveBars, "Capture Bars", 1, 8, 2),
                      ch (pid::midiMode, "MIDI Mode", { "Slices", "Patterns" }, 0), ch (pid::sliceTrig, "Slice Trigger", { "Gate", "One-Shot" }, 0),
                      bo (pid::loopPlay, "Loop Play", true), ch (pid::quantize, "Switch Quantise", { "Off", "1/16", "1/8", "1/4", "Bar" }, 3),
                      fl (pid::declick, "Edge Fade", 1.0f, 30.0f, 4.0f, "ms", 6.0f, 1));
    global->addChild (in (pid::timeSlot, "Time Slot", 1, splinter::kNumSlots, 1), in (pid::volSlot, "Volume Slot", 1, splinter::kNumSlots, 1),
                      in (pid::seqSlot, "Sequence Slot", 1, splinter::kNumSlots, 1), in (pid::seed, "Chaos Seed", 1, 999, 1),
                      pct (pid::volDepth, "Volume Curve Depth", 1.0f), fl (pid::volSmooth, "Volume Curve Smooth", 0.5f, 50.0f, 3.0f, "ms", 6.0f, 1),
                      ch (pid::stretchMode, "Stretch Mode", { "Auto", "Tonal", "Beats", "Tape" }, 0));
    l.add (std::move (global));

    auto lanes = std::make_unique<AudioProcessorParameterGroup> ("lanes", "Effects", "|");
    lanes->addChild (ch (pid::stutterDiv, "Stutter Division", divs, 3), fl (pid::stutterPitch, "Stutter Pitch Ramp", -24.0f, 24.0f, 0.0f, "st", 0.0f, 1),
                     pct (pid::stutterGate, "Stutter Gate", 1.0f), pct (pid::stutterDecay, "Stutter Decay", 0.0f),
                     fl (pid::stutterAccel, "Stutter Accelerate", -1.0f, 1.0f, 0.0f), ch (pid::stutterArp, "Stutter Arp", { "Off", "Up", "Down", "Up-Down", "Random" }, 0));
    lanes->addChild (pct (pid::tapeTime, "Tape Stop Time", 0.8f), in (pid::pitchDegrees, "Pitch Steps", -7, 7, -2), pct (pid::pitchGlide, "Pitch Glide", 0.0f),
                     fl (pid::crushBits, "Crush Bits", 1.0f, 16.0f, 8.0f, "bit", 0.0f, 1), fl (pid::crushRate, "Crush Rate", 200.0f, 48000.0f, 8000.0f, "Hz", 4000.0f, 0));
    lanes->addChild (in (pid::combDegree, "Comb Note", 0, 7, 0), in (pid::combOctave, "Comb Octave", -1, 2, 0), fl (pid::combFeedback, "Comb Feedback", 0.0f, 0.98f, 0.9f),
                     fl (pid::combDamp, "Comb Damping", 500.0f, 16000.0f, 6000.0f, "Hz", 3000.0f, 0), in (pid::ringDegree, "Ring Note", 0, 7, 0), in (pid::ringOctave, "Ring Octave", -1, 3, 1));
    lanes->addChild (ch (pid::filterShape, "Filter Sweep", { "LP Close", "LP Open", "HP Rise", "HP Fall", "BP Up", "BP Down" }, 0), pct (pid::filterReso, "Filter Resonance", 0.3f),
                     fl (pid::freezeMs, "Freeze Grain", 20.0f, 400.0f, 80.0f, "ms", 100.0f, 0));
    lanes->addChild (ch (pid::delayDiv, "Delay Time", ddivs, 1), fl (pid::delayFeedback, "Delay Feedback", 0.0f, 0.95f, 0.5f), fl (pid::delayTone, "Delay Tone", 500.0f, 16000.0f, 5000.0f, "Hz", 3000.0f, 0),
                     pct (pid::delayLevel, "Delay Level", 0.7f), pct (pid::delayPingPong, "Delay Ping-Pong", 1.0f),
                     fl (pid::reverbTime, "Reverb Time", 0.3f, 8.0f, 2.2f, "s", 2.0f, 1), fl (pid::reverbDamp, "Reverb Damping", 1000.0f, 16000.0f, 6000.0f, "Hz", 4000.0f, 0),
                     pct (pid::reverbLevel, "Reverb Level", 0.6f));
    l.add (std::move (lanes));

    auto harm = std::make_unique<AudioProcessorParameterGroup> ("harmony", "Harmony", "|");
    harm->addChild (ch (pid::key, "Key", keys, 0), ch (pid::scale, "Scale", scales, 1), ch (pid::harmMode, "Harmony Mode", { "Scale Steps", "Semitones" }, 0),
                    pct (pid::formant, "Formant Preserve", 1.0f), ch (pid::harmGate, "Harmony Trigger", { "Always", "Sequenced" }, 0));
    static const int defInterval[4] = { 2, 4, 7, -7 };
    static const float defPan[4] = { -0.5f, 0.5f, -0.2f, 0.2f };
    static const float defLevel[4] = { 0.8f, 0.7f, 0.6f, 0.6f };
    for (int i = 0; i < splinter::kNumHarmVoices; ++i)
    {
        const String n = "Voice " + String (i + 1) + " ";
        harm->addChild (bo (pid::voice (i, "On"), n + "On", i < 2), in (pid::voice (i, "Interval"), n + "Interval", -24, 24, defInterval[i]));
        harm->addChild (std::make_unique<AudioParameterFloat> (ParameterID { pid::voice (i, "Pan"), 1 }, n + "Pan", NormalisableRange<float> (-1.0f, 1.0f), defPan[i]),
                        std::make_unique<AudioParameterFloat> (ParameterID { pid::voice (i, "Detune"), 1 }, n + "Detune", NormalisableRange<float> (-50.0f, 50.0f), 0.0f, AudioParameterFloatAttributes().withLabel ("ct")),
                        std::make_unique<AudioParameterFloat> (ParameterID { pid::voice (i, "Level"), 1 }, n + "Level", NormalisableRange<float> (0.0f, 1.0f), defLevel[i]));
    }
    l.add (std::move (harm));

    auto out = std::make_unique<AudioProcessorParameterGroup> ("output", "Output", "|");
    out->addChild (fl (pid::hpFreq, "High-Pass", 20.0f, 2000.0f, 20.0f, "Hz", 200.0f, 0), fl (pid::lpFreq, "Low-Pass", 200.0f, 20000.0f, 20000.0f, "Hz", 2500.0f, 0),
                   fl (pid::filterQ, "Filter Q", 0.5f, 4.0f, 0.7f), fl (pid::tilt, "Tilt", -6.0f, 6.0f, 0.0f, "dB", 0.0f, 1),
                   fl (pid::width, "Width", 0.0f, 2.0f, 1.0f), fl (pid::outGain, "Output Gain", -24.0f, 12.0f, 0.0f, "dB", 0.0f, 1),
                   bo (pid::limiter, "Limiter", true), bo (pid::inputMonitor, "Input Monitor", false), bo (pid::bypass, "Bypass", false));
    l.add (std::move (out));
    return l;
}

void ParamBridge::attach (AudioProcessorValueTreeState& apvts)
{
    binds.clear();
    auto bind = [&] (const String& id, std::function<void (EngineParams&, float)> fn)
    {
        auto* raw = apvts.getRawParameterValue (id);
        jassert (raw != nullptr);
        if (raw != nullptr) binds.push_back ({ raw, std::move (fn) });
    };
#define B_F(idn, field) bind (pid::idn, [] (EngineParams& p, float v) { p.field = v; });
#define B_I(idn, field) bind (pid::idn, [] (EngineParams& p, float v) { p.field = (int) std::lround (v); });
#define B_B(idn, field) bind (pid::idn, [] (EngineParams& p, float v) { p.field = v > 0.5f; });
    B_I (sourceMode, sourceMode) B_I (liveBars, liveBars) B_I (midiMode, midiMode) B_I (sliceTrig, sliceTrig) B_B (loopPlay, loopPlay)
    B_I (quantize, quantize) B_F (declick, declickMs)
    bind (pid::timeSlot, [] (EngineParams& p, float v) { p.timeSlot = (int) std::lround (v) - 1; });
    bind (pid::volSlot, [] (EngineParams& p, float v) { p.volSlot = (int) std::lround (v) - 1; });
    bind (pid::seqSlot, [] (EngineParams& p, float v) { p.seqSlot = (int) std::lround (v) - 1; });
    B_F (chop, chop) B_F (stutter, stutter) B_F (warp, warp) B_F (harmony, harmony) B_F (crush, crush) B_F (chaos, chaos) B_F (mix, mix)
    B_I (seed, seed) B_F (volDepth, volDepth) B_F (volSmooth, volSmoothMs)
    B_I (stutterDiv, stutterDiv) B_F (stutterPitch, stutterPitch) B_F (stutterGate, stutterGate) B_F (stutterDecay, stutterDecay) B_F (stutterAccel, stutterAccel) B_I (stutterArp, stutterArp)
    B_F (tapeTime, tapeTime) B_I (pitchDegrees, pitchDegrees) B_F (pitchGlide, pitchGlide) B_F (crushBits, crushBits) B_F (crushRate, crushRate)
    B_I (combDegree, combDegree) B_I (combOctave, combOctave) B_F (combFeedback, combFeedback) B_F (combDamp, combDamp) B_I (ringDegree, ringDegree) B_I (ringOctave, ringOctave)
    bind (pid::filterShape, [] (EngineParams& p, float v) { p.filterShape = (int) std::lround (v) + 1; });
    B_F (filterReso, filterReso) B_F (freezeMs, freezeMs)
    B_I (delayDiv, delayDiv) B_F (delayFeedback, delayFeedback) B_F (delayTone, delayTone) B_F (delayLevel, delayLevel) B_F (delayPingPong, delayPingPong)
    B_F (reverbTime, reverbTime) B_F (reverbDamp, reverbDamp) B_F (reverbLevel, reverbLevel)
    B_I (key, key) B_I (scale, scale) B_I (harmMode, harmMode) B_F (formant, formant) B_I (harmGate, harmGate)
    B_F (hpFreq, hpFreq) B_F (lpFreq, lpFreq) B_F (filterQ, filterQ) B_F (tilt, tiltDb) B_F (width, width) B_F (outGain, outGainDb)
    B_B (limiter, limiter) B_B (inputMonitor, inputMonitor) B_B (bypass, bypass)
#undef B_F
#undef B_I
#undef B_B
    for (int i = 0; i < splinter::kNumHarmVoices; ++i)
    {
        bind (pid::voice (i, "On"), [i] (EngineParams& p, float v) { p.voiceOn[i] = v > 0.5f; });
        bind (pid::voice (i, "Interval"), [i] (EngineParams& p, float v) { p.voiceInterval[i] = (int) std::lround (v); });
        bind (pid::voice (i, "Pan"), [i] (EngineParams& p, float v) { p.voicePan[i] = v; });
        bind (pid::voice (i, "Detune"), [i] (EngineParams& p, float v) { p.voiceDetune[i] = v; });
        bind (pid::voice (i, "Level"), [i] (EngineParams& p, float v) { p.voiceLevel[i] = v; });
    }
    pStretch = apvts.getRawParameterValue (pid::stretchMode);
}

void ParamBridge::fill (EngineParams& p) const
{
    for (auto& b : binds) b.second (p, b.first->load (std::memory_order_relaxed));
}
