#include "Engine.h"

#include "AmpTape.h"
#include "Fuzz.h"
#include "Glide.h"
#include "Modulation.h"
#include "Output.h"
#include "Reverse.h"
#include "SoftFocus.h"
#include "Util.h"
#include "Wash.h"

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
    Amp amp;
    Modulation mod;
    Glide glide;
    SoftFocus focus;
    Reverse reverse;
    Wash wash;
    Tape tape;
    OutputStage output;
    SoftLimiter limiter;

    // global
    DelayLine dryLine[2];
    Smooth inGainS, mixS, bypassS, swellS;
    AutoSwell swell;

    // module order with click-free switching
    std::array<int, kNumSlots> order { 0, 1, 2, 3, 4, 5, 6 };
    std::array<int, kNumSlots> pendingOrder { 0, 1, 2, 3, 4, 5, 6 };
    float orderGain = 1.0f, orderStep = 0.01f;

    void prepare (double sampleRate, int maxBlock)
    {
        fs = static_cast<float> (sampleRate);
        fuzz.prepare (sampleRate, maxBlock);
        amp.prepare (sampleRate);
        mod.prepare (sampleRate);
        glide.prepare (sampleRate);
        focus.prepare (sampleRate);
        reverse.prepare (sampleRate);
        wash.prepare (sampleRate);
        tape.prepare (sampleRate);
        output.prepare (sampleRate);
        limiter.prepare (fs);

        // Every module keeps its latency constant whether it is on or off, so this never changes at run time.
        latency = fuzz.latencySamples() + Amp::kLatency + tape.latencySamples();
        for (auto& d : dryLine)
            d.prepare (latency + 8);

        inGainS.setTime (0.03f, fs);
        mixS.setTime (0.03f, fs);
        bypassS.setTime (0.015f, fs);
        swellS.setTime (0.05f, fs);
        swell.prepare (fs);
        orderStep = 1.0f / (0.008f * fs);
        reset();
    }

    void reset()
    {
        fuzz.reset();
        amp.reset();
        mod.reset();
        glide.reset();
        focus.reset();
        reverse.reset();
        wash.reset();
        tape.reset();
        output.reset();
        limiter.reset();
        swell.reset();
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
            case Slot::Amp:
            {
                Amp::Params ap;
                ap.on = pv.on (P::ampOn);
                ap.drive = pv[P::ampDrive] + std::max (0.0f, pv[P::wall] - 0.5f) * 0.3f;
                ap.bass = pv[P::ampBass];
                ap.treble = pv[P::ampTreble] - (pv[P::warmth] - 0.5f) * 0.25f;
                ap.cab = pv.choice (P::ampCab);
                ap.levelDb = pv[P::ampLevel];
                ap.mix = pv[P::ampMix];
                amp.process (l, r, n, ap);
                break;
            }
            case Slot::Mod:
            {
                Modulation::Params mp;
                const float blur = pv[P::blur];
                mp.on = pv.on (P::modOn);
                mp.mode = pv.choice (P::modMode);
                mp.rate = pv[P::modRate];
                mp.depth = pv[P::modDepth] * (0.7f + 0.6f * blur);
                mp.color = pv[P::modColor];
                mp.width = pv[P::modWidth];
                mp.mix = macroAmount (blur, pv[P::modMix]);
                mod.process (l, r, n, mp);
                break;
            }
            case Slot::Glide:
            {
                Glide::Params gp;
                const float g = pv[P::glide];
                const float amount = 2.0f * g;               // 0 .. 2x the module's depths
                gp.on = pv.on (P::glideOn);
                gp.wobbleCents = pv[P::glideWobble] * amount;
                gp.rateHz = pv[P::glideRate];
                gp.driftCents = pv[P::glideDrift] * amount;
                gp.dipCents = pv[P::glideDip] * amount;
                gp.returnMs = pv[P::glideReturn];
                gp.spread = pv[P::glideSpread];
                gp.mix = pv[P::glideMix] * clamp01 (g * 8.0f);   // macro at zero takes the module out entirely
                glide.process (l, r, n, gp);
                break;
            }
            case Slot::Focus:
            {
                SoftFocus::Params sp;
                const float blur = pv[P::blur];
                sp.on = pv.on (P::focOn);
                sp.timeMs = pv[P::focTime];
                sp.feedback = pv[P::focFeedback];
                sp.detuneCents = pv[P::focDetune] * (0.6f + 0.8f * blur);
                sp.haze = pv[P::focHaze] + (blur - 0.5f) * 0.4f;
                sp.tone = pv[P::focTone] - (pv[P::warmth] - 0.5f) * 0.3f;
                sp.mix = macroAmount (blur, pv[P::focMix]);
                focus.process (l, r, n, sp);
                break;
            }
            case Slot::Reverse:
            {
                Reverse::Params rp;
                rp.on = pv.on (P::revOn);
                rp.mode = pv.choice (P::revMode);
                rp.timeMs = pv[P::revTime];
                rp.feedback = pv[P::revFeedback];
                rp.smear = pv[P::revSmear];
                rp.tone = pv[P::revTone] - (pv[P::warmth] - 0.5f) * 0.3f;
                rp.mix = pv[P::revMix];
                reverse.process (l, r, n, rp);
                break;
            }
            case Slot::Wash:
            {
                Wash::Params wp;
                const float w = pv[P::wash];
                wp.on = pv.on (P::washOn);
                wp.decay = pv[P::washDecay] * std::pow (2.0f, (w - 0.5f) * 3.0f);
                wp.size = pv[P::washSize] + (w - 0.5f) * 0.4f;
                wp.preDelayMs = pv[P::washPre];
                wp.highCutHz = pv[P::washDamp] * std::pow (2.0f, -(pv[P::warmth] - 0.5f) * 2.4f);
                wp.lowCutHz = pv[P::washLow];
                wp.mod = pv[P::washMod];
                wp.shimmer = pv[P::washShimmer];
                wp.shimmerMode = pv.choice (P::washShimMode);
                wp.duck = pv[P::washDuck];
                wp.freeze = pv.on (P::washFreeze);
                wp.mix = macroAmount (w, pv[P::washMix]);
                wash.process (l, r, n, wp);
                break;
            }
        }
    }

    void processChunk (float* l, float* r, int n, const ParamValues& pv)
    {
        // ---- targets ------------------------------------------------------------------
        inGainS.target = dbToGain (pv[P::inGain]);
        mixS.target = clamp01 (pv[P::mix]);
        bypassS.target = pv.on (P::bypass) ? 1.0f : 0.0f;
        swellS.target = clamp01 (pv[P::inSwell]);
        pendingOrder = resolveOrder (pv);
        if (first)
        {
            inGainS.snap();
            mixS.snap();
            bypassS.snap();
            swellS.snap();
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
            const float peak = std::max (std::abs (l[i]), std::abs (r[i]));
            inPeak = std::max (inPeak, peak);
            const float sg = swell.process (peak, swellS.next());
            l[i] *= sg;
            r[i] *= sg;
        }
        meters.inPeak = std::max (meters.inPeak, inPeak);

        // ---- reorderable chain --------------------------------------------------------
        for (int s = 0; s < kNumSlots; ++s)
            processSlot (order[static_cast<size_t> (s)], l, r, n, pv);

        // ---- tape, then output stage --------------------------------------------------
        const float warmth = pv[P::warmth];
        Tape::Params tp;
        tp.on = pv.on (P::tapeOn);
        tp.drive = pv[P::tapeDrive] + (warmth - 0.5f) * 0.4f;
        tp.wow = pv[P::tapeWow];
        tp.flutter = pv[P::tapeFlutter];
        tp.age = pv[P::tapeAge] + (warmth - 0.5f) * 0.5f;
        tp.mix = pv[P::tapeMix];
        tape.process (l, r, n, tp);

        OutputStage::Params op;
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
