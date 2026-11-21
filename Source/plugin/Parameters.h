// Splinter — parameter IDs, layout, and the bridge to EngineParams.
#pragma once
#include "dsp/Engine.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace pid
{
#define SPLINTER_PARAM(name) inline constexpr const char* name = #name;
SPLINTER_PARAM (sourceMode) SPLINTER_PARAM (liveBars) SPLINTER_PARAM (midiMode) SPLINTER_PARAM (sliceTrig) SPLINTER_PARAM (loopPlay)
SPLINTER_PARAM (quantize) SPLINTER_PARAM (declick) SPLINTER_PARAM (timeSlot) SPLINTER_PARAM (volSlot) SPLINTER_PARAM (seqSlot)
SPLINTER_PARAM (chop) SPLINTER_PARAM (stutter) SPLINTER_PARAM (warp) SPLINTER_PARAM (harmony) SPLINTER_PARAM (crush) SPLINTER_PARAM (chaos) SPLINTER_PARAM (mix)
SPLINTER_PARAM (seed) SPLINTER_PARAM (volDepth) SPLINTER_PARAM (volSmooth) SPLINTER_PARAM (stretchMode)
SPLINTER_PARAM (stutterDiv) SPLINTER_PARAM (stutterPitch) SPLINTER_PARAM (stutterGate) SPLINTER_PARAM (stutterDecay) SPLINTER_PARAM (stutterAccel) SPLINTER_PARAM (stutterArp)
SPLINTER_PARAM (tapeTime) SPLINTER_PARAM (pitchDegrees) SPLINTER_PARAM (pitchGlide)
SPLINTER_PARAM (crushBits) SPLINTER_PARAM (crushRate)
SPLINTER_PARAM (combDegree) SPLINTER_PARAM (combOctave) SPLINTER_PARAM (combFeedback) SPLINTER_PARAM (combDamp)
SPLINTER_PARAM (ringDegree) SPLINTER_PARAM (ringOctave)
SPLINTER_PARAM (filterShape) SPLINTER_PARAM (filterReso) SPLINTER_PARAM (freezeMs)
SPLINTER_PARAM (delayDiv) SPLINTER_PARAM (delayFeedback) SPLINTER_PARAM (delayTone) SPLINTER_PARAM (delayLevel) SPLINTER_PARAM (delayPingPong)
SPLINTER_PARAM (reverbTime) SPLINTER_PARAM (reverbDamp) SPLINTER_PARAM (reverbLevel)
SPLINTER_PARAM (key) SPLINTER_PARAM (scale) SPLINTER_PARAM (harmMode) SPLINTER_PARAM (formant) SPLINTER_PARAM (harmGate)
SPLINTER_PARAM (hpFreq) SPLINTER_PARAM (lpFreq) SPLINTER_PARAM (filterQ) SPLINTER_PARAM (tilt) SPLINTER_PARAM (width) SPLINTER_PARAM (outGain)
SPLINTER_PARAM (limiter) SPLINTER_PARAM (inputMonitor) SPLINTER_PARAM (bypass)
#undef SPLINTER_PARAM
inline juce::String voice (int i, const char* what) { return "v" + juce::String (i + 1) + what; }   // v1On, v1Interval, v1Pan, v1Detune, v1Level
} // namespace pid

juce::AudioProcessorValueTreeState::ParameterLayout createSplinterLayout();

// Caches raw parameter pointers and fills an EngineParams each block (lock free).
class ParamBridge
{
public:
    void attach (juce::AudioProcessorValueTreeState& apvts);
    void fill (splinter::EngineParams& p) const;
    int stretchMode() const { return (int) *pStretch; }
private:
    std::vector<std::pair<std::atomic<float>*, std::function<void (splinter::EngineParams&, float)>>> binds;
    std::atomic<float>* pStretch = nullptr;
};
