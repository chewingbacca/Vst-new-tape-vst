// Minimal placeholder set; the full factory library replaces this file.
#include "FactorySamples.h"
#include "../util/Math.h"
#include "../util/Rng.h"
#include <cstring>
#include <mutex>
#include <vector>

namespace atm {

static const FactorySampleInfo kInfos[] = {
    { "Sine A2", "Tone" },
    { "Noise Burst", "Noise" },
};
static constexpr int kCount = (int) (sizeof (kInfos) / sizeof (kInfos[0]));

int numFactorySamples() { return kCount; }
const FactorySampleInfo& factorySampleInfo (int i) { return kInfos[clamp (i, 0, kCount - 1)]; }

const SampleData* getFactorySample (const char* name)
{
    static std::mutex m;
    static SampleData* cache[kCount] = {};
    std::lock_guard<std::mutex> lock (m);
    for (int i = 0; i < kCount; ++i)
    {
        if (std::strcmp (kInfos[i].name, name) != 0) continue;
        if (cache[i] == nullptr)
        {
            const double sr = 44100.0; const int n = (int) sr;
            std::vector<float> l ((size_t) n);
            Noise nz; nz.seed (7);
            for (int k = 0; k < n; ++k)
                l[(size_t) k] = i == 0 ? 0.7f * (float) std::sin (kTau * 110.0 * k / sr) * (float) std::exp (-k / (sr * 0.4))
                                       : 0.7f * nz.next() * (float) std::exp (-k / (sr * 0.08));
            const float* ch[1] = { l.data() };
            cache[i] = makeSampleData (ch, 1, n, sr, kInfos[i].name, i == 0 ? 45.f : 60.f);
        }
        return cache[i];
    }
    return nullptr;
}

} // namespace atm
