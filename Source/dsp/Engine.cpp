#include "Engine.h"

#include "Fuzz.h"
#include "Output.h"
#include "Util.h"

namespace swoon
{

namespace
{
/** Macro "amount" law: 0 -> nothing, 0.5 -> the module's own value, 1 -> pushed most of the way to full. */
float macroAmount (float macro, float base) noexcept
{
    macro = clamp01 (macro);
    if (macro <= 0.5f)
    {
        const float t = macro * 2.0f;
        return base * t * (2.0f - t);          // eases into the module value
    }
    return base + (1.0f - base) * (macro * 2.0f - 1.0f) * 0.7f;
}
} // namespace

struct Engine::Impl
{
    float fs = 48000.0f;
    int latency = 0;
    bool first = true;
    EngineMeters meters;

    // modules
    Fuzz fuzz;
    OutputStage output;
    SoftLimiter limiter;

    // global
    DelayLine dryLine[2];
    Smooth inGainS, mixS, bypassS;

    // module order with click-free switching
    std::array<int, kNumSlots> order { 0, 1, 2, 3, 4, 5, 6 };
    std::array<int, kNumSlots> pendingOrder { 0, 1, 2, 3, 4, 5, 6 };
    float orderGain = 1.0f, orderStep = 0.01f;

    void prepare (double sampleRate, int maxBlock)
    {
        fs = static_cast<float> (sampleRate);
        fuzz.prepare (sampleRate, maxBlock);
        output.prepare (sampleRate);
        limiter.prepare (fs);

        latency = fuzz.latencySamples();
        for (auto& d : dryLine)
            d.prepare (latency + 8);

        inGainS.setTime (0.03f, fs);
        mixS.setTime (0.03f, fs);
        bypassS.setTime (0.015f, fs);
        orderStep = 1.0f / (0.008f * fs);
        reset();
    }

    void reset()
    {
        fuzz.reset();
        output.reset();
        limiter.reset();
        for (auto& d : dryLine)
            d.reset();
        orderGain = 1.0f;
        first = true;
        meters = {};
    }

    void processSlot (int slot, float* l, float* r, int n, const ParamValues& pv)
    {
        switch (static_cast<Slot> (slot))
        {
            case Slot::Fuzz:
            {
                Fuzz::Params fp;
                const float wall = pv[P::wall];
                fp.on = pv.on (P::fuzzOn);
                fp.type = pv.choice (P::fuzzType);
                fp.voice = pv.choice (P::fuzzVoice);
                fp.sustain = pv[P::fuzzSustain] + (wall - 0.5f) * 0.5f;
                fp.tone = pv[P::fuzzTone] - (pv[P::warmth] - 0.5f) * 0.35f;
                fp.gate = pv[P::fuzzGate];
                fp.stack = pv[P::fuzzStack] + (wall - 0.5f) * 0.6f;
                fp.levelDb = pv[P::fuzzLevel];
                fp.mix = macroAmount (wall, pv[P::fuzzMix]);
                fuzz.process (l, r, n, fp);
                break;
            }
            case Slot::Glide:
            case Slot::Amp:
            case Slot::Mod:
            case Slot::Focus:
            case Slot::Reverse:
            case Slot::Wash:
                break;
        }
    }

    void processChunk (float* l, float* r, int n, const ParamValues& pv)
    {
        // ---- targets ------------------------------------------------------------------
        inGainS.target = dbToGain (pv[P::inGain]);
        mixS.target = clamp01 (pv[P::mix]);
        bypassS.target = pv.on (P::bypass) ? 1.0f : 0.0f;
        pendingOrder = resolveOrder (pv);
        if (first)
        {
            inGainS.snap();
            mixS.snap();
            bypassS.snap();
            order = pendingOrder;
        }

        // ---- dry reference, input gain ------------------------------------------------
        float dry[2][kChunk];
        float inPeak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            dryLine[0].push (l[i]);
            dryLine[1].push (r[i]);
            dry[0][i] = dryLine[0].read (latency);
            dry[1][i] = dryLine[1].read (latency);
            const float g = inGainS.next();
            l[i] *= g;
            r[i] *= g;
            inPeak = std::max (inPeak, std::max (std::abs (l[i]), std::abs (r[i])));
        }
        meters.inPeak = std::max (meters.inPeak, inPeak);

        // ---- reorderable chain --------------------------------------------------------
        for (int s = 0; s < kNumSlots; ++s)
            processSlot (order[static_cast<size_t> (s)], l, r, n, pv);

        // ---- output stage -------------------------------------------------------------
        OutputStage::Params op;
        const float warmth = pv[P::warmth];
        op.tilt = pv[P::outTilt] - (warmth - 0.5f) * 0.9f;
        op.width = pv[P::outWidth];
        op.gainDb = pv[P::outGain];
        op.warmShelfDb = (warmth - 0.5f) * 5.0f;
        output.process (l, r, n, op);

        // ---- order switching fade, global mix, limiter, bypass ------------------------
        const bool orderChanging = pendingOrder != order;
        float outPeakL = 0.0f, outPeakR = 0.0f;
        bool bad = false;

        for (int i = 0; i < n; ++i)
        {
            if (orderChanging)
                orderGain = std::max (0.0f, orderGain - orderStep);
            else if (orderGain < 1.0f)
                orderGain = std::min (1.0f, orderGain + orderStep);

            const float mix = mixS.next();
            float a = dry[0][i] * (1.0f - mix) + l[i] * mix * orderGain;
            float b = dry[1][i] * (1.0f - mix) + r[i] * mix * orderGain;
            limiter.process (a, b);

            const float byp = bypassS.next();
            a += byp * (dry[0][i] - a);
            b += byp * (dry[1][i] - b);

            bad = bad || isBad (a) || isBad (b);
            l[i] = a;
            r[i] = b;
            outPeakL = std::max (outPeakL, std::abs (a));
            outPeakR = std::max (outPeakR, std::abs (b));
        }

        if (orderChanging && orderGain <= 0.0f)
            order = pendingOrder;

        if (bad)
        {
            // Should never happen; if it does, fail safe: silence this chunk and start clean.
            std::fill (l, l + n, 0.0f);
            std::fill (r, r + n, 0.0f);
            reset();
            return;
        }

        meters.outPeakL = std::max (meters.outPeakL, outPeakL);
        meters.outPeakR = std::max (meters.outPeakR, outPeakR);
        meters.limiterReductionDb = limiter.getReductionDb();
        first = false;
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int maxBlockSize) { impl->prepare (sampleRate, maxBlockSize); }
void Engine::reset() { impl->reset(); }
int Engine::getLatencySamples() const { return impl->latency; }
const EngineMeters& Engine::getMeters() const { return impl->meters; }

void Engine::process (float* left, float* right, int numSamples, const ParamValues& params)
{
    impl->meters.inPeak = impl->meters.outPeakL = impl->meters.outPeakR = 0.0f;

    for (int offset = 0; offset < numSamples; offset += kChunk)
    {
        const int n = std::min (kChunk, numSamples - offset);
        impl->processChunk (left + offset, right + offset, n, params);
    }
}

} // namespace swoon
