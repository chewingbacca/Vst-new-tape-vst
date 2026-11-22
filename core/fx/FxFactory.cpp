#include "Effect.h"

namespace atm {

std::unique_ptr<Effect> createStutterFx();
std::unique_ptr<Effect> createReverseFx();
std::unique_ptr<Effect> createTapeStopFx();
std::unique_ptr<Effect> createCrushFx();
std::unique_ptr<Effect> createDistortFx();
std::unique_ptr<Effect> createCombFx();
std::unique_ptr<Effect> createResonatorFx();
std::unique_ptr<Effect> createFreqShiftFx();
std::unique_ptr<Effect> createRingModFx();
std::unique_ptr<Effect> createSpectralFx();
std::unique_ptr<Effect> createGrainDelayFx();
std::unique_ptr<Effect> createDelayFx();
std::unique_ptr<Effect> createPlateFx();
std::unique_ptr<Effect> createCloudFx();
std::unique_ptr<Effect> createCompressorFx();
std::unique_ptr<Effect> createFilterFx();

std::unique_ptr<Effect> createEffect (int type)
{
    switch (type)
    {
        case Fx::Stutter:    return createStutterFx();
        case Fx::Reverse:    return createReverseFx();
        case Fx::TapeStop:   return createTapeStopFx();
        case Fx::Crush:      return createCrushFx();
        case Fx::Distort:    return createDistortFx();
        case Fx::Comb:       return createCombFx();
        case Fx::Resonator:  return createResonatorFx();
        case Fx::FreqShift:  return createFreqShiftFx();
        case Fx::RingMod:    return createRingModFx();
        case Fx::Spectral:   return createSpectralFx();
        case Fx::GrainDelay: return createGrainDelayFx();
        case Fx::Delay:      return createDelayFx();
        case Fx::Plate:      return createPlateFx();
        case Fx::Cloud:      return createCloudFx();
        case Fx::Compressor: return createCompressorFx();
        case Fx::Filter:     return createFilterFx();
        default: break;
    }
    return nullptr;
}

} // namespace atm
