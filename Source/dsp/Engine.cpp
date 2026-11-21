#include "Engine.h"

#include "AmpTape.h"
#include "Fuzz.h"
#include "Glide.h"
#include "Macros.h"
#include "Modulation.h"
#include "Output.h"
#include "Reverse.h"
#include "SoftFocus.h"
#include "Util.h"
#include "Wash.h"

namespace swoon
{

struct Engine::Impl
{
    float fs = 48000.0f, bpm = 120.0f;
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
    enum class OrderState { idle, fadeOut, drain, settle, fadeIn };
    std::array<int, kNumSlots> order { 0, 1, 2, 3, 4, 5, 6 };
    std::array<int, kNumSlots> pendingOrder { 0, 1, 2, 3, 4, 5, 6 };
    OrderState orderState = OrderState::idle;
    float orderGain = 1.0f, orderStep = 0.01f;
    int orderHold = 0;

    // The engine always works on whole chunks of kChunk samples, whatever block sizes the host uses: input is
    // collected here and each finished chunk is handed back one chunk later. That costs kChunk samples of
    // (reported) latency and makes the result independent of the host's buffer size.
    float fifoIn[2][kChunk] {}, fifoOut[2][kChunk] {};
    int fifoPos = 0;
    float chunkInPeak = 0.0f, chunkOutPeakL = 0.0f, chunkOutPeakR = 0.0f;

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

    void resetModules()
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
        orderHold = 0;
        orderState = OrderState::idle;
        first = true;
    }

    /** Full reset including the block FIFO (not safe to call from inside processChunk). */
    void reset()
    {
        resetModules();
        for (auto& ch : fifoIn)
            std::fill (std::begin (ch), std::end (ch), 0.0f);
        for (auto& ch : fifoOut)
            std::fill (std::begin (ch), std::end (ch), 0.0f);
        fifoPos = 0;
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
                fp.sustain = pv[P::fuzzSustain] + macro::sustainOffset (wall);
                fp.tone = pv[P::fuzzTone] + macro::fuzzToneOffset (pv[P::warmth]);
                fp.gate = pv[P::fuzzGate];
                fp.stack = pv[P::fuzzStack] + macro::stackOffset (wall);
                fp.levelDb = pv[P::fuzzLevel];
                fp.mix = macro::amount (wall, pv[P::fuzzMix]);
                fuzz.process (l, r, n, fp);
                break;
            }
            case Slot::Amp:
            {
                Amp::Params ap;
                ap.on = pv.on (P::ampOn);
                ap.drive = pv[P::ampDrive] + macro::ampDriveOffset (pv[P::wall]);
                ap.bass = pv[P::ampBass];
                ap.treble = pv[P::ampTreble] + macro::ampTrebleOffset (pv[P::warmth]);
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
                mp.depth = pv[P::modDepth] * macro::modDepthMul (blur);
                mp.color = pv[P::modColor];
                mp.width = pv[P::modWidth];
                mp.mix = macro::amount (blur, pv[P::modMix]);
                mod.process (l, r, n, mp);
                break;
            }
            case Slot::Glide:
            {
                Glide::Params gp;
                const float g = pv[P::glide];
                const float amount = macro::glideDepthMul (g);   // 0 .. 2x the module's depths
                gp.on = pv.on (P::glideOn);
                gp.wobbleCents = pv[P::glideWobble] * amount;
                gp.rateHz = pv[P::glideRate];
                gp.driftCents = pv[P::glideDrift] * amount;
                gp.dipCents = pv[P::glideDip] * amount;
                gp.returnMs = pv[P::glideReturn];
                gp.spread = pv[P::glideSpread];
                gp.mix = pv[P::glideMix] * macro::glideMixMul (g);   // macro at zero takes the module out entirely
                glide.process (l, r, n, gp);
                break;
            }
            case Slot::Focus:
            {
                SoftFocus::Params sp;
                const float blur = pv[P::blur];
                sp.on = pv.on (P::focOn);
                sp.timeMs = macro::syncedMs (pv.choice (P::focSync), bpm, pv[P::focTime]);
                sp.rightRatio = pv.choice (P::focSync) > 0 ? 1.5f : SoftFocus::kRightRatio;   // dotted against straight
                sp.feedback = pv[P::focFeedback];
                sp.detuneCents = pv[P::focDetune] * macro::detuneMul (blur);
                sp.haze = pv[P::focHaze] + macro::hazeOffset (blur);
                sp.tone = pv[P::focTone] + macro::toneOffset (pv[P::warmth]);
                sp.mix = macro::amount (blur, pv[P::focMix]);
                focus.process (l, r, n, sp);
                break;
            }
            case Slot::Reverse:
            {
                Reverse::Params rp;
                rp.on = pv.on (P::revOn);
                rp.mode = pv.choice (P::revMode);
                rp.timeMs = macro::syncedMs (pv.choice (P::revSync), bpm, pv[P::revTime]);
                rp.feedback = pv[P::revFeedback];
                rp.smear = pv[P::revSmear];
                rp.tone = pv[P::revTone] + macro::toneOffset (pv[P::warmth]);
                rp.mix = pv[P::revMix];
                reverse.process (l, r, n, rp);
                break;
            }
            case Slot::Wash:
            {
                Wash::Params wp;
                const float w = pv[P::wash];
                wp.on = pv.on (P::washOn);
                wp.decay = pv[P::washDecay] * macro::decayMul (w);
                wp.size = pv[P::washSize] + macro::sizeOffset (w);
                wp.preDelayMs = pv[P::washPre];
                wp.highCutHz = pv[P::washDamp] * macro::washDampMul (pv[P::warmth]);
                wp.lowCutHz = pv[P::washLow];
                wp.mod = pv[P::washMod];
                wp.shimmer = pv[P::washShimmer];
                wp.shimmerMode = pv.choice (P::washShimMode);
                wp.duck = pv[P::washDuck];
                wp.freeze = pv.on (P::washFreeze);
                wp.mix = macro::amount (w, pv[P::washMix]);
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

        // ---- reorderable chain --------------------------------------------------------
        // An order change must not put a step into any module's memory (delay lines, reverbs), so as well as
        // dipping the chain's output we fade *every module's input* to silence, swap while nothing is flowing
        // between modules, and fade back in. Reverb and delay tails survive the change and re-emerge after it.
        const bool orderChanging = pendingOrder != order;
        switch (orderState)
        {
            case OrderState::idle:
                if (orderChanging)
                    orderState = OrderState::fadeOut;
                break;
            case OrderState::fadeOut:
                if (! orderChanging)
                    orderState = OrderState::fadeIn;            // withdrawn before anything was swapped
                else if (orderGain <= 0.0f)
                {
                    // inputs are silent: let the latency delay lines inside the modules drain, then swap
                    orderState = OrderState::drain;
                    orderHold = latency + 2 * kChunk;
                }
                break;
            case OrderState::drain:
                orderHold -= n;
                if (orderHold <= 0)
                {
                    order = pendingOrder;                       // whatever is wanted by now
                    orderState = OrderState::settle;
                }
                break;
            case OrderState::settle:
                orderState = OrderState::fadeIn;                // one silent chunk in the new order
                break;
            case OrderState::fadeIn:
                if (pendingOrder != order)
                    orderState = OrderState::fadeOut;
                else if (orderGain >= 1.0f)
                    orderState = OrderState::idle;
                break;
        }

        const bool fading = orderState != OrderState::idle;
        float fade[kChunk];
        if (fading)
        {
            const float step = orderState == OrderState::fadeOut ? -orderStep
                             : orderState == OrderState::fadeIn ? orderStep : 0.0f;
            if (orderState == OrderState::drain || orderState == OrderState::settle)
                orderGain = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                orderGain = std::clamp (orderGain + step, 0.0f, 1.0f);
                fade[i] = smoothstep (orderGain);
            }
        }

        for (int s = 0; s < kNumSlots; ++s)
        {
            if (fading)
                for (int i = 0; i < n; ++i)
                {
                    l[i] *= fade[i];
                    r[i] *= fade[i];
                }
            processSlot (order[static_cast<size_t> (s)], l, r, n, pv);
        }
        if (fading)
            for (int i = 0; i < n; ++i)     // and the chain's output, so a tail that changes position isn't cut mid-swing
            {
                l[i] *= fade[i];
                r[i] *= fade[i];
            }

        // ---- tape, then output stage --------------------------------------------------
        const float warmth = pv[P::warmth];
        Tape::Params tp;
        tp.on = pv.on (P::tapeOn);
        tp.drive = pv[P::tapeDrive] + macro::tapeDriveOffset (warmth);
        tp.wow = pv[P::tapeWow];
        tp.flutter = pv[P::tapeFlutter];
        tp.age = pv[P::tapeAge] + macro::tapeAgeOffset (warmth);
        tp.mix = pv[P::tapeMix];
        tape.process (l, r, n, tp);

        OutputStage::Params op;
        op.tilt = pv[P::outTilt] + macro::tiltOffset (warmth);
        op.width = pv[P::outWidth];
        op.gainDb = pv[P::outGain];
        op.warmShelfDb = macro::warmShelfDb (warmth);
        output.process (l, r, n, op);

        // ---- global mix, limiter, bypass ----------------------------------------------
        float outPeakL = 0.0f, outPeakR = 0.0f;
        bool bad = false;

        for (int i = 0; i < n; ++i)
        {
            const float mix = mixS.next();
            float a = dry[0][i] * (1.0f - mix) + l[i] * mix;
            float b = dry[1][i] * (1.0f - mix) + r[i] * mix;
            limiter.process (a, b);

            const float byp = bypassS.next();
            if (byp >= 1.0f)
            {
                a = dry[0][i];              // fully bypassed: the delayed input, bit for bit
                b = dry[1][i];
            }
            else if (byp > 0.0f)
            {
                a += byp * (dry[0][i] - a);
                b += byp * (dry[1][i] - b);
            }

            bad = bad || isBad (a) || isBad (b);
            l[i] = a;
            r[i] = b;
            outPeakL = std::max (outPeakL, std::abs (a));
            outPeakR = std::max (outPeakR, std::abs (b));
        }

        if (bad)
        {
            // Should never happen; if it does, fail safe: silence this chunk and start clean.
            std::fill (l, l + n, 0.0f);
            std::fill (r, r + n, 0.0f);
            resetModules();
            return;
        }

        chunkInPeak = inPeak;
        chunkOutPeakL = outPeakL;
        chunkOutPeakR = outPeakR;
        first = false;
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int maxBlockSize) { impl->prepare (sampleRate, maxBlockSize); }
void Engine::reset() { impl->reset(); }
int Engine::getLatencySamples() const { return impl->latency + kChunk; }
void Engine::setTempo (double beatsPerMinute) { impl->bpm = static_cast<float> (beatsPerMinute > 0.0 ? beatsPerMinute : 120.0); }
const EngineMeters& Engine::getMeters() const { return impl->meters; }

void Engine::process (float* left, float* right, int numSamples, const ParamValues& params)
{
    auto& e = *impl;
    bool anyChunk = false;

    for (int i = 0; i < numSamples; ++i)
    {
        const int p = e.fifoPos;
        e.fifoIn[0][p] = sanitise (left[i]);
        e.fifoIn[1][p] = sanitise (right[i]);
        left[i] = e.fifoOut[0][p];
        right[i] = e.fifoOut[1][p];

        if (++e.fifoPos == kChunk)
        {
            e.fifoPos = 0;
            std::memcpy (e.fifoOut[0], e.fifoIn[0], sizeof (e.fifoOut[0]));
            std::memcpy (e.fifoOut[1], e.fifoIn[1], sizeof (e.fifoOut[1]));
            e.chunkInPeak = e.chunkOutPeakL = e.chunkOutPeakR = 0.0f;
            e.processChunk (e.fifoOut[0], e.fifoOut[1], kChunk, params);

            // meters: the peak over the chunks finished during this call (left alone if none finished)
            if (! anyChunk)
                e.meters.inPeak = e.meters.outPeakL = e.meters.outPeakR = 0.0f;
            anyChunk = true;
            e.meters.inPeak = std::max (e.meters.inPeak, e.chunkInPeak);
            e.meters.outPeakL = std::max (e.meters.outPeakL, e.chunkOutPeakL);
            e.meters.outPeakR = std::max (e.meters.outPeakR, e.chunkOutPeakR);
            e.meters.limiterReductionDb = e.limiter.getReductionDb();
        }
    }
}

} // namespace swoon
