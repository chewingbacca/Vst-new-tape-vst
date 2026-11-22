// Factory sample set: synthesised at run time (no audio files shipped), built
// lazily on a non-audio thread and shared by every plugin instance.
#pragma once
#include "../voices/SampleData.h"

namespace atm {

struct FactorySampleInfo
{
    const char* name;        // unique, used in presets and saved projects
    const char* category;    // "Break", "Hit", "Texture", "Tone", "Vox", "Noise" ...
};

int numFactorySamples();
const FactorySampleInfo& factorySampleInfo (int index);
// Returns the shared sample (building it on first use; thread-safe; may take tens of milliseconds).
// The pointer stays valid for the life of the process. nullptr if the name is unknown.
const SampleData* getFactorySample (const char* name);

} // namespace atm
