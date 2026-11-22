#pragma once
#include "Params.h"
namespace atm {
extern const ParamDesc kDrumParams[kEngineParams];
extern const ParamDesc kFmParams[kEngineParams];
extern const ParamDesc kSamplerParams[kEngineParams];
extern const ParamDesc kGranularParams[kEngineParams];
extern const ParamDesc kResonatorParams[kEngineParams];
extern const ParamDesc kSynthParams[kEngineParams];
extern const ParamDesc kFxStutter[kFxParams], kFxReverse[kFxParams], kFxTapeStop[kFxParams], kFxCrush[kFxParams],
    kFxDistort[kFxParams], kFxComb[kFxParams], kFxResonator[kFxParams], kFxFreqShift[kFxParams], kFxRingMod[kFxParams],
    kFxSpectral[kFxParams], kFxGrainDelay[kFxParams], kFxDelay[kFxParams], kFxPlate[kFxParams], kFxCloud[kFxParams],
    kFxCompressor[kFxParams], kFxFilter[kFxParams];
}
