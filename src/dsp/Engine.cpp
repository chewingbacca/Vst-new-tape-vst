#include "Engine.h"

namespace verdant
{

void Engine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sr = float (sampleRate);
    tickRate = sr / float (kChunk);

    tape.prepare (sr);
    grain.prepare (sr);
    arp.prepare (sr);
    tone.prepare (sr);
    limiter.prepare (sr);

    for (auto& d : dryDelay)    d.prepare (tape.nominalDelay() + 8);
    for (auto& d : bypassDelay) d.prepare (latencySamples() + 8);

    static constexpr float driftHz[4] = { 0.071f, 0.113f, 0.047f, 0.163f };
    for (int i = 0; i < 4; ++i)
        drift[i].prepare (tickRate, 0xC0FFEEu + uint32_t (i) * 7919u, driftHz[i]);
    follower.prepare (sr);

    for (auto* s : { &sBloom, &sGrowth, &sMoisture, &sSunlight, &sRoots })
        s->setTime (0.06f, tickRate);
    sMix.setTime (0.03f, tickRate);
    sBypass.setTime (0.02f, tickRate);
    sOutGain.setTime (0.03f, tickRate);
    sWidth.setTime (0.05f, tickRate);
    sDuck.setTime (0.05f, tickRate);
    sGrainMix.setTime (0.04f, tickRate);
    sArpMix.setTime (0.04f, tickRate);
    sArpWash.setTime (0.04f, tickRate);

    bandLo.setCutoff (250.0f, sr);
    bandHi.setCutoff (2500.0f, sr);
    duckAtk = 1.0f - std::exp (-1.0f / (0.012f * sr));
    duckRel = 1.0f - std::exp (-1.0f / (0.45f * sr));

    reset();
}

void Engine::reset()
{
    tape.reset();
    grain.reset();
    arp.reset();
    tone.reset();
    limiter.reset();
    for (auto& d : dryDelay) d.clear();
    for (auto& d : bypassDelay) d.clear();
    for (auto& d : drift) d.reset();
    follower.reset();
    bandLo.reset(); bandHi.reset();
    chunkPos = 0;
    firstBlock = true;
    meterIn = meterOutL = meterOutR = meterWet = 0.0f;
    eLow = eMid = eHigh = 0.0f;
    duckEnv = 0.0f;
}

void Engine::controlTick()
{
    const ParamValues& p = target;

    sBloom.set (p[pBloom]);       sGrowth.set (p[pGrowth]);   sMoisture.set (p[pMoisture]);
    sSunlight.set (p[pSunlight]); sRoots.set (p[pRoots]);
    sMix.set (p[pMix]);
    sBypass.set (p[pBypass] >= 0.5f ? 1.0f : 0.0f);
    sOutGain.set (dbToGain (p[pOutGain]));
    sWidth.set (p[pOutWidth]);
    sDuck.set (p[pOutDuck]);

    if (firstBlock)
        for (auto* s : { &sBloom, &sGrowth, &sMoisture, &sSunlight, &sRoots, &sMix, &sBypass, &sOutGain, &sWidth, &sDuck })
            s->snap (s->target);

    const float bloom = sBloom.tick(), growth = sGrowth.tick(), moisture = sMoisture.tick();
    const float sunlight = sSunlight.tick(), roots = sRoots.tick();
    const float mix = sMix.tick(), byp = sBypass.tick(), outGain = sOutGain.tick();
    const float width = sWidth.tick(), duck = sDuck.tick();

    // ---- modulation sources
    const float growthRate = macroScale (growth, 0.2f, 5.0f);
    const float driftDepth = p[pDrift] * macroScale (bloom, 0.5f, 1.5f);
    for (int i = 0; i < 4; ++i)
    {
        driftVal[i] = drift[i].tick (growthRate) * driftDepth;
        meters.drift[i].store (drift[i].get(), std::memory_order_relaxed);
    }
    const float bloomChar = macroScale (bloom, 0.35f, 1.5f);

    // ---- tape
    {
        Tape::Controls c;
        c.on = p[pTapeOn] >= 0.5f;
        c.mix = p[pTapeMix];
        c.speedSemis = p[pTapeSpeed];
        c.stretch = p[pTapeStretch];
        c.wow = clampf (p[pTapeWow] * bloomChar * (1.0f + 0.35f * driftVal[0]), 0.0f, 1.0f);
        c.flutter = clampf (p[pTapeFlutter] * bloomChar, 0.0f, 1.0f);
        c.drive = clampf (p[pTapeDrive] * macroScale (bloom, 0.6f, 1.3f) + 0.15f * macroBipolar (roots), 0.0f, 1.0f);
        c.age = clampf (p[pTapeAge] - 0.35f * macroBipolar (sunlight), 0.0f, 1.0f);
        c.bodyDb = 2.0f * macroBipolar (roots);
        c.stutter = clampf (p[pTapeStutter] * macroScale (bloom, 0.5f, 1.4f), 0.0f, 1.0f);
        c.stutterLen = int (p[pTapeStutterLen] + 0.5f);
        c.motion = macroScale (growth, 0.5f, 2.2f);
        c.drift = driftVal[0];
        c.beat = beat;
        c.samplesPerBeat = samplesPerBeat;
        tape.control (c);
    }

    // ---- grain cloud
    const float env = follower.shaped();
    const float touch = p[pTouch];
    {
        GrainCloud::Controls c;
        c.on = p[pGrainOn] >= 0.5f;
        c.density = p[pGrainDensity] * macroScale (bloom, 0.5f, 1.8f) * macroScale (growth, 0.8f, 1.3f)
                    * (1.0f + 1.2f * touch * env) * (1.0f + 0.35f * driftVal[1]);
        c.sizeMs = p[pGrainSize] * (1.0f + 0.25f * driftVal[2]);
        c.scatter = clampf (p[pGrainScatter] * (1.0f + 0.3f * driftVal[1]), 0.0f, 1.0f);
        c.pitchSemis = p[pGrainPitch];
        c.spread = clampf (p[pGrainSpread] * macroScale (bloom, 0.6f, 1.4f) + 0.25f * std::max (0.0f, macroBipolar (sunlight)), 0.0f, 1.0f);
        c.reverse = p[pGrainReverse];
        c.pan = p[pGrainPan];
        c.shape = p[pGrainShape];
        c.feedback = p[pGrainFeedback];
        c.upBias = sunlight;
        c.downBias = roots;
        c.octavesOnly = int (p[pScale] + 0.5f) == 14;
        grain.control (c);

        sGrainMix.set (clampf (p[pGrainMix] * macroScale (bloom, 0.3f, 1.5f), 0.0f, 1.25f));
        if (firstBlock) { sGrainMix.snap (sGrainMix.target); rGrainMix.snap (sGrainMix.target); }
        rGrainMix.begin (sGrainMix.tick(), kChunk);
    }

    // ---- arp / shimmer
    {
        Arp::Controls c;
        c.on = p[pArpOn] >= 0.5f;
        c.rate = int (p[pArpRate] + 0.5f);
        c.pattern = int (p[pArpPattern] + 0.5f);
        c.range = int (p[pArpRange] + 0.5f);
        c.gate = p[pArpGate];
        c.sparkle = clampf (p[pArpShimmer] + 0.3f * std::max (0.0f, macroBipolar (sunlight)), 0.0f, 1.0f);
        c.hold = p[pArpHold];
        c.track = p[pArpTrack] >= 0.5f;
        c.key = int (p[pKey] + 0.5f);
        c.scale = int (p[pScale] + 0.5f);
        c.beat = beat;
        c.samplesPerBeat = samplesPerBeat;
        arp.control (c);

        sArpMix.set (clampf (p[pArpMix] * macroScale (bloom, 0.3f, 1.5f), 0.0f, 1.25f));
        sArpWash.set (clampf (p[pArpWash] + 0.3f * macroBipolar (moisture), 0.0f, 1.0f));
        if (firstBlock) { sArpMix.snap (sArpMix.target); sArpWash.snap (sArpWash.target); }
        const float am = sArpMix.tick(), wash = sArpWash.tick();
        const float direct = am * std::cos (wash * 0.5f * kPi), send = am * std::sin (wash * 0.5f * kPi);
        if (firstBlock) { rArpDirect.snap (direct); rArpSend.snap (send); }
        rArpDirect.begin (direct, kChunk);
        rArpSend.begin (send, kChunk);
    }

    // ---- output tone
    {
        const float tiltDb = clampf (6.0f * p[pOutTilt] + 4.5f * macroBipolar (sunlight) + 1.0f * driftVal[3], -9.0f, 9.0f);
        const float rootsDb = 6.0f * macroBipolar (roots);
        tone.setTone (tiltDb, rootsDb);
    }

    const float dry = std::cos (mix * 0.5f * kPi);
    const float wet = std::sin (mix * 0.5f * kPi) * outGain;

    if (firstBlock)
    {
        rDry.snap (dry); rWet.snap (wet); rBypass.snap (byp); rInGate.snap (1.0f - byp);
        rWidth.snap (width); rDuck.snap (duck);
        firstBlock = false;
    }
    rDry.begin (dry, kChunk);
    rWet.begin (wet, kChunk);
    rBypass.begin (byp, kChunk);
    rInGate.begin (1.0f - byp, kChunk);
    rWidth.begin (width, kChunk);
    rDuck.begin (duck, kChunk);

    meters.envelope.store (follower.shaped(), std::memory_order_relaxed);
    meters.stutterPulse.store (tape.stutterEvents, std::memory_order_relaxed);
    meters.arpPulse.store (arp.noteCount, std::memory_order_relaxed);
    meters.arpDegree.store (arp.lastDegree, std::memory_order_relaxed);
    meters.pitchHz.store (arp.pitchHz(), std::memory_order_relaxed);
    meters.grainPulse.store (grain.spawnCount, std::memory_order_relaxed);
    meters.grainActivity.store (clampf (grain.activity, 0.0f, 1.0f), std::memory_order_relaxed);
}

void Engine::processChunk (float* l, float* r, int n)
{
    float inL[kChunk] = {}, inR[kChunk] = {}, tL[kChunk], tR[kChunk], gL[kChunk], gR[kChunk], aL[kChunk], aR[kChunk], duckG[kChunk];

    // ---- input: followers, bypass gate
    for (int i = 0; i < n; ++i)
    {
        const float g = rInGate.next();
        inL[i] = l[i] * g;
        inR[i] = r[i] * g;
        follower.process (inL[i], inR[i]);
        meterIn = std::max (meterIn * 0.9995f, std::max (std::abs (l[i]), std::abs (r[i])));

        const float e = follower.env;
        duckEnv += (e > duckEnv ? duckAtk : duckRel) * (e - duckEnv);
        const float amount = clampf (duckEnv * 3.0f, 0.0f, 1.0f);
        duckG[i] = 1.0f - 0.8f * rDuck.next() * amount;
    }

    // ---- tape
    tape.process (inL, inR, tL, tR, n);

    // ---- grain cloud (reads the tape output)
    grain.process (tL, tR, gL, gR, n);

    // ---- arp (records the tape output while it sounds)
    arp.process (tL, tR, aL, aR, n);

    // ---- output
    const int dryLag = tape.nominalDelay();
    const int bypLag = latencySamples();

    for (int i = 0; i < n; ++i)
    {
        const float gm = rGrainMix.next() * duckG[i];
        const float ad = rArpDirect.next() * duckG[i], as = rArpSend.next();
        float wetL = tL[i] + gm * gL[i] + (ad + as) * aL[i];
        float wetR = tR[i] + gm * gR[i] + (ad + as) * aR[i];

        tone.process (wetL, wetR, rWidth.next());
        meterWet = std::max (meterWet * 0.9995f, std::max (std::abs (wetL), std::abs (wetR)));

        dryDelay[0].push (l[i]);
        dryDelay[1].push (r[i]);
        bypassDelay[0].push (l[i]);
        bypassDelay[1].push (r[i]);

        const float dg = rDry.next(), wg = rWet.next(), bp = rBypass.next();
        float outL = dryDelay[0].tap (dryLag) * dg + wetL * wg;
        float outR = dryDelay[1].tap (dryLag) * dg + wetR * wg;

        limiter.process (outL, outR);

        // latency-matched clean path for click-free bypass
        outL += (bypassDelay[0].tap (bypLag) - outL) * bp;
        outR += (bypassDelay[1].tap (bypLag) - outR) * bp;

        // metering
        meterOutL = std::max (meterOutL * 0.9995f, std::abs (outL));
        meterOutR = std::max (meterOutR * 0.9995f, std::abs (outR));
        const float m = 0.5f * (outL + outR);
        const float lo = bandLo.process (m);
        const float hiLp = bandHi.process (m);
        const float hi = m - hiLp, md = hiLp - lo;
        eLow  += 0.002f * (lo * lo - eLow);
        eMid  += 0.002f * (md * md - eMid);
        eHigh += 0.002f * (hi * hi - eHigh);

        l[i] = outL;
        r[i] = outR;
    }
}

void Engine::process (float* left, float* right, int n, const Transport& transport)
{
    samplesPerBeat = 60.0 * double (sr) / std::max (20.0, transport.bpm);
    beatInc = 1.0 / samplesPerBeat;
    if (transport.playing)
        beat = transport.ppq;

    int done = 0;
    while (done < n)
    {
        if (chunkPos == 0)
            controlTick();

        const int m = std::min (n - done, kChunk - chunkPos);
        processChunk (left + done, right + done, m);
        done += m;
        beat += double (m) * beatInc;
        chunkPos = (chunkPos + m) % kChunk;
    }

    // Last line of defence: never hand the host a non-finite sample.
    bool bad = false;
    for (int i = 0; i < n; ++i)
        bad = bad || isBad (left[i]) || isBad (right[i]);
    if (bad)
    {
        reset();
        std::fill (left, left + n, 0.0f);
        std::fill (right, right + n, 0.0f);
    }

    auto energy = [] (float e) { return clampf (std::sqrt (std::max (0.0f, e)) * 4.0f, 0.0f, 1.0f); };
    meters.inLevel.store (meterIn, std::memory_order_relaxed);
    meters.outLevelL.store (meterOutL, std::memory_order_relaxed);
    meters.outLevelR.store (meterOutR, std::memory_order_relaxed);
    meters.wetLevel.store (meterWet, std::memory_order_relaxed);
    meters.low.store (energy (eLow), std::memory_order_relaxed);
    meters.mid.store (energy (eMid), std::memory_order_relaxed);
    meters.high.store (energy (eHigh), std::memory_order_relaxed);
    meters.limiterGain.store (limiter.gainReduction(), std::memory_order_relaxed);
}

} // namespace verdant
