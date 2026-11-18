#include "Engine.h"

namespace verdant
{

void Engine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sr = float (sampleRate);
    tickRate = sr / float (kChunk);

    limiter.prepare (sr);
    const int lat = std::max (1, limiter.latency());
    bypassDelayL.assign (size_t (lat), 0.0f);
    bypassDelayR.assign (size_t (lat), 0.0f);

    sMix.setTime (0.03f, tickRate);
    sBypass.setTime (0.02f, tickRate);
    sOutGain.setTime (0.03f, tickRate);

    reset();
}

void Engine::reset()
{
    limiter.reset();
    std::fill (bypassDelayL.begin(), bypassDelayL.end(), 0.0f);
    std::fill (bypassDelayR.begin(), bypassDelayR.end(), 0.0f);
    bypassPos = 0;
    chunkPos = 0;
    firstBlock = true;
    meterIn = meterOutL = meterOutR = 0.0f;
}

void Engine::controlTick (const Transport&)
{
    sMix.set (target[pMix]);
    sBypass.set (target[pBypass] >= 0.5f ? 1.0f : 0.0f);
    sOutGain.set (dbToGain (target[pOutGain]));

    if (firstBlock)
    {
        sMix.snap (sMix.target);
        sBypass.snap (sBypass.target);
        sOutGain.snap (sOutGain.target);
    }

    const float mix = sMix.tick();
    const float byp = sBypass.tick();
    sOutGain.tick();

    const float dry = std::cos (mix * 0.5f * kPi);
    const float wet = std::sin (mix * 0.5f * kPi);

    if (firstBlock)
    {
        rDry.snap (dry); rWet.snap (wet); rBypass.snap (byp);
        firstBlock = false;
    }
    rDry.begin (dry, kChunk);
    rWet.begin (wet, kChunk);
    rBypass.begin (byp, kChunk);
}

void Engine::processChunk (float* l, float* r, int n)
{
    const int lat = int (bypassDelayL.size());

    for (int i = 0; i < n; ++i)
    {
        const float inL = l[i], inR = r[i];
        meterIn = std::max (meterIn * 0.9995f, std::max (std::abs (inL), std::abs (inR)));

        // (modules go here) -----------------------------------------------
        float wetL = inL, wetR = inR;

        const float dg = rDry.next(), wg = rWet.next(), bp = rBypass.next();
        float outL = inL * dg + wetL * wg;
        float outR = inR * dg + wetR * wg;

        limiter.process (outL, outR);

        // latency-matched clean path for click-free bypass
        const float cleanL = bypassDelayL[size_t (bypassPos)];
        const float cleanR = bypassDelayR[size_t (bypassPos)];
        bypassDelayL[size_t (bypassPos)] = inL;
        bypassDelayR[size_t (bypassPos)] = inR;
        bypassPos = (bypassPos + 1) % lat;

        outL += (cleanL - outL) * bp;
        outR += (cleanR - outR) * bp;

        meterOutL = std::max (meterOutL * 0.9995f, std::abs (outL));
        meterOutR = std::max (meterOutR * 0.9995f, std::abs (outR));
        l[i] = outL;
        r[i] = outR;
    }
}

void Engine::process (float* left, float* right, int n, const Transport& transport)
{
    int done = 0;
    while (done < n)
    {
        if (chunkPos == 0)
            controlTick (transport);

        const int m = std::min (n - done, kChunk - chunkPos);
        processChunk (left + done, right + done, m);
        done += m;
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

    meters.inLevel.store (meterIn, std::memory_order_relaxed);
    meters.outLevelL.store (meterOutL, std::memory_order_relaxed);
    meters.outLevelR.store (meterOutR, std::memory_order_relaxed);
    meters.limiterGain.store (limiter.gainReduction(), std::memory_order_relaxed);
}

} // namespace verdant
