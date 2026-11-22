#include "SeqEngine.h"

namespace atm {

namespace {
// salts keep the different random decisions independent
enum Salt : uint32_t { sProb = 0xA1, sDir = 0xB2, sDens = 0xC3, sMutA = 0xD4, sMutB = 0xD5, sMutC = 0xD6, sMutD = 0xD7, sMutE = 0xD8,
                       sCplxA = 0xE1, sCplxB = 0xE2, sCplxC = 0xE3, sChaosT = 0xF1, sChaosV = 0xF2, sGlA = 0x71, sGlB = 0x72, sGlC = 0x73, sHit = 0x81 };

inline uint32_t h5 (uint32_t seed, int track, int64_t a, int64_t b, uint32_t salt)
{
    return hashN (seed, (uint32_t) track * 0x9E3779B1u + salt, (uint32_t) (uint64_t) a, (uint32_t) (uint64_t) b);
}
}

int64_t SeqEngine::stepsPerCycle (const TrackPattern& tp, double cycleBeats)
{
    const double d = tp.stepBeats();
    return std::max<int64_t> (1, (int64_t) std::ceil (cycleBeats / d - 1.0e-9));
}

int SeqEngine::directionIndex (const TrackPattern& tp, int64_t abs, uint32_t seed, int64_t& loop)
{
    const int L = clamp ((int) tp.length, 1, kMaxSteps);
    loop = floorDiv64 (abs, L);
    const int pos = (int) (abs - loop * L);
    switch (tp.dir)
    {
        default:
        case Dir::Forward: return pos;
        case Dir::Reverse: return L - 1 - pos;
        case Dir::PingPong:
        {
            if (L < 2) return 0;
            const int period = 2 * L - 2;
            loop = floorDiv64 (abs, period);
            const int q = (int) (abs - loop * period);
            return q < L ? q : period - q;
        }
        case Dir::Random:
            return (int) (hashN (seed, sDir, (uint32_t) (uint64_t) abs, (uint32_t) ((uint64_t) abs >> 32)) % (uint32_t) L);
        case Dir::Drunk:
        {
            // a seeded walk that restarts each loop: mostly forward, sometimes back or stalled
            int idx = (int) (hashN (seed, sDir, (uint32_t) (uint64_t) loop) % (uint32_t) L);
            for (int j = 0; j < pos; ++j)
            {
                const float r = hashToFloat (hashN (seed, sDir + 1, (uint32_t) (uint64_t) loop, (uint32_t) j));
                idx += r < 0.62f ? 1 : (r < 0.87f ? -1 : 0);
            }
            return wrapMod (idx, L);
        }
    }
}

SeqEngine::Resolved SeqEngine::resolve (const SeqFrame& f, const SeqMacros& m, int track, int64_t cycle, int64_t abs)
{
    const TrackPattern& tp = f.pattern->tracks[track];
    const uint32_t seed = hashN (f.seed, tp.seed);
    const int L = clamp ((int) tp.length, 1, kMaxSteps);
    Resolved r;
    int64_t localLoop = 0;
    // random directions should differ from cycle to cycle
    const int64_t dirAbs = tp.freeRun ? abs : abs + cycle * 7919;
    r.index = (tp.dir == Dir::Random || tp.dir == Dir::Drunk) ? directionIndex (tp, dirAbs, seed, localLoop)
                                                             : directionIndex (tp, abs, seed, localLoop);
    if (tp.dir == Dir::Random || tp.dir == Dir::Drunk) localLoop = floorDiv64 (abs, L);
    if (tp.freeRun) r.loop = localLoop;
    else
    {
        const int64_t spc = stepsPerCycle (tp, f.cycleBeats);
        const int64_t loopsPerCycle = std::max<int64_t> (1, (spc + L - 1) / L);
        r.loop = cycle * loopsPerCycle + localLoop;
    }

    Step s = tp.steps[r.index];
    const int idx = r.index;
    const int role = f.kit ? f.kit->tracks[track].role : (int) Role::Perc;
    const bool melodic = role == Role::Bass || role == Role::Lead || role == Role::Pad;

    // ---- Mutation: per-loop variation of the written pattern
    if (m.mutation > 0.001f)
    {
        const float mu = m.mutation;
        const float a = hashToFloat (h5 (seed, track, idx, r.loop, sMutA));
        const float b = hashToFloat (h5 (seed, track, idx, r.loop, sMutB));
        const float c = hashToFloat (h5 (seed, track, idx, r.loop, sMutC));
        const float d = hashToFloat (h5 (seed, track, idx, r.loop, sMutD));
        const uint32_t e = h5 (seed, track, idx, r.loop, sMutE);
        if (s.on)
        {
            if (a < mu * 0.30f) s.vel = (uint8_t) clamp ((int) ((float) s.vel * (0.45f + 0.8f * hashToFloat (e))), 1, 127);
            if (b < mu * 0.14f && s.vel < 112 && (idx % 4) != 0) s.on = 0;            // drop weaker off-beat hits
            if (melodic && c < mu * 0.25f)
            {
                static constexpr int8_t moves[8] = { -2, -1, 1, 2, 3, -3, 7, -5 };
                s.pitch = (int8_t) clamp ((int) s.pitch + moves[(e >> 8) & 7], -48, 48);
            }
            if (d < mu * 0.16f && s.ratCount <= 1)
            {
                s.ratCount = (uint8_t) (2 + ((e >> 12) % 3));
                s.ratVel = -0.5f; s.ratCurve = (uint8_t) ((e >> 16) % RatCurve::count);
            }
            if (a > 1.f - mu * 0.12f) s.micro = clamp (s.micro + 0.25f * hashToBipolar (e), -0.5f, 0.5f);
        }
        else if (b < mu * 0.10f)
        {
            s.on = 1; s.vel = (uint8_t) (38 + (e % 50)); s.prob = 100; s.cond = Cond::Always;
            s.len = 0.5f; s.ratCount = 1;
            if (melodic)
            {
                // borrow the pitch of the nearest written note
                for (int k = 1; k < L; ++k) { const Step& o = tp.steps[wrapMod (idx - k, L)]; if (o.on) { s.pitch = o.pitch; break; } }
            }
        }
    }

    // ---- Density: nested thresholds, so a sweep adds/removes hits in a fixed order
    if (m.density > 0.001f)
    {
        if (! s.on)
        {
            const float h = hashToFloat (h5 (seed, track, idx, 0, sDens));
            const float w = (idx % 2) ? 0.55f : 0.4f;                 // favour off-beats for ghosts
            const float limit = melodic ? 0.5f : 1.f;                  // keep melodic lines from turning to mush
            if (h < m.density * w * limit)
            {
                const uint32_t e = h5 (seed, track, idx, 1, sDens);
                s.on = 1; s.vel = (uint8_t) (30 + (e % 46)); s.prob = 100; s.cond = Cond::Always; s.len = 0.5f; s.ratCount = 1;
                if (melodic)
                    for (int k = 1; k < L; ++k) { const Step& o = tp.steps[wrapMod (idx - k, L)]; if (o.on) { s.pitch = o.pitch; break; } }
            }
        }
    }
    else if (m.density < -0.001f && s.on)
    {
        const float h = hashToFloat (h5 (seed, track, idx, 0, sDens));
        const float score = (float) s.vel / 127.f * 0.55f + h * 0.45f + ((idx % 4) == 0 ? 0.25f : 0.f) + (idx == 0 ? 0.3f : 0.f);
        if (score < -m.density * 1.45f) s.on = 0;
    }

    // ---- Complexity: ratchets and micro-timing
    if (m.complexity > 0.001f && s.on)
    {
        const float cx = m.complexity;
        const float a = hashToFloat (h5 (seed, track, idx, r.loop & 1, sCplxA));
        if (s.ratCount <= 1 && a < cx * (melodic ? 0.18f : 0.38f))
        {
            const uint32_t e = h5 (seed, track, idx, r.loop & 1, sCplxB);
            static constexpr uint8_t counts[8] = { 2, 2, 3, 2, 4, 3, 6, 8 };
            const int maxIdx = 2 + (int) (cx * 5.99f);
            s.ratCount = counts[(e % (uint32_t) (maxIdx + 1))];
            s.ratCurve = (uint8_t) ((e >> 8) % RatCurve::count);
            s.ratAmt = 0.3f + 0.6f * hashToFloat (e >> 3);
            s.ratVel = -0.2f - 0.6f * hashToFloat (e >> 5);
            if (((e >> 20) & 7) == 0) s.ratPitch = ((e >> 23) & 1) ? 12.f : -12.f;
            else if (((e >> 20) & 7) == 1) s.ratPitch = ((e >> 23) & 1) ? 5.f : -7.f;
        }
        const float j = hashToBipolar (h5 (seed, track, idx, 0, sCplxC));
        if ((idx % 4) != 0) s.micro = clamp (s.micro + j * cx * 0.2f, -0.5f, 0.5f);
    }

    r.step = s;
    return r;
}

bool SeqEngine::passes (const SeqFrame& f, const SeqMacros& m, int track, int64_t cycle, int64_t abs, const Resolved& r, int depth)
{
    const TrackPattern& tp = f.pattern->tracks[track];
    const Step& s = r.step;
    if (! s.on) return false;
    const uint32_t seed = hashN (f.seed, tp.seed);
    if (s.prob < 100)
    {
        const float h = hashToFloat (h5 (seed, track, abs, cycle, sProb));
        if (h * 100.f >= (float) s.prob) return false;
    }
    switch (s.cond)
    {
        case Cond::Always:   return true;
        case Cond::Fill:     return m.fill;
        case Cond::NotFill:  return ! m.fill;
        case Cond::First:    return r.loop == 0;
        case Cond::NotFirst: return r.loop != 0;
        case Cond::Pre:
        case Cond::NotPre:
        {
            bool prev = false;
            if (depth < 4)
            {
                const int L = clamp ((int) tp.length, 1, kMaxSteps);
                const int64_t spc = tp.freeRun ? 0 : stepsPerCycle (tp, f.cycleBeats);
                int64_t c = cycle, a = abs;
                for (int back = 0; back < L; ++back)
                {
                    --a;
                    if (! tp.freeRun && a < 0) { a = spc - 1; --c; }
                    const Resolved pr = resolve (f, m, track, c, a);
                    if (pr.step.on) { prev = passes (f, m, track, c, a, pr, depth + 1); break; }
                }
            }
            return s.cond == Cond::Pre ? prev : ! prev;
        }
        default: break;
    }
    if (condIsAB (s.cond))
    {
        int a, b; condGetAB (s.cond, a, b);
        return wrapMod64 (r.loop, b) == a - 1;
    }
    return true;
}

void SeqEngine::evalStep (const SeqFrame& f, const SeqMacros& m, int track, int64_t cycle, int64_t abs, float glitchSens, StepEval& out)
{
    const TrackPattern& tp = f.pattern->tracks[track];
    const double d = tp.stepBeats();
    const uint32_t seed = hashN (f.seed, tp.seed);
    const Resolved r = resolve (f, m, track, cycle, abs);

    out.abs = abs; out.cycle = cycle; out.index = r.index; out.loop = r.loop;
    out.step = r.step; out.stepBeats = d;
    out.nominal = nominalTime (f, tp, cycle, abs);
    out.velScale = 1.f;

    double off = 0.0;
    const double sw = tp.swing < 0.f ? f.globalSwing : (double) tp.swing;
    if (abs & 1) off += (sw - 0.5) * 2.0 * d;
    if (tp.groove != 0 && tp.grooveAmt > 0.f)
    {
        const GrooveDef& g = grooveDef (tp.groove);
        const int p16 = (int) wrapMod64 (abs, 16);
        off += (double) (g.time[p16] * tp.grooveAmt) * d;
        out.velScale *= lerp (1.f, g.vel[p16], tp.grooveAmt);
    }
    if (r.step.on) off += (double) r.step.micro * d;
    if (m.chaos > 0.001f)
    {
        if ((r.index % 4) != 0) off += (double) (m.chaos * 0.10f * hashToBipolar (h5 (seed, track, abs, cycle, sChaosT))) * d;
        out.velScale *= 1.f + m.chaos * 0.4f * hashToBipolar (h5 (seed, track, abs, cycle, sChaosV));
    }
    out.time = out.nominal + off;
    out.trig = passes (f, m, track, cycle, abs, r, 0);

    // ---- Glitch macro: deterministic per-step decision
    out.glitch = GlitchCmd{};
    const float g = m.glitch * glitchSens * 2.f;
    if (g > 0.001f)
    {
        const float h = hashToFloat (h5 (seed, track, abs, cycle, sGlA));
        if (h < g * 0.22f)
        {
            const uint32_t e = h5 (seed, track, abs, cycle, sGlB);
            const uint32_t e2 = h5 (seed, track, abs, cycle, sGlC);
            const float pick = hashToFloat (e);
            GlitchCmd c;
            static constexpr float lens[4] = { 1.f, 2.f, 2.f, 4.f };
            c.beats = (float) d * lens[(e >> 4) & 3];
            c.amount = clamp01 (0.45f + 0.55f * g);
            if (pick < 0.46f)
            {
                c.type = GlitchType::Repeat;
                static constexpr float div[8] = { 1.f, 2.f, 2.f, 3.f, 4.f, 4.f, 6.f, 8.f };
                c.sliceBeats = (float) d / div[(e2 >> 3) & 7];
                static constexpr float ps[8] = { 0, 0, 0, 0, -1, 1, -2, 12 };
                c.pitchStep = ps[(e2 >> 8) & 7];
            }
            else if (pick < 0.60f) { c.type = GlitchType::Gate; c.sliceBeats = (float) d / (float) (2 + ((e2 >> 3) & 3)); }
            else if (pick < 0.76f) { c.type = GlitchType::Reverse; c.sliceBeats = c.beats; }
            else if (pick < 0.90f) { c.type = GlitchType::Crush; }
            else                   { c.type = GlitchType::Stop; c.beats = std::max (c.beats, (float) d * 2.f); }
            out.glitch = c;
        }
    }
}

double SeqEngine::ratchetPos (int i, int n, int curve, float amount)
{
    if (n <= 1 || i <= 0) return 0.0;
    const double u = (double) i / (double) n;
    amount = clamp01 (amount);
    switch (curve)
    {
        default:
        case RatCurve::Even:  return u;
        case RatCurve::Accel: return 1.0 - std::pow (1.0 - u, 1.0 + 2.5 * amount);
        case RatCurve::Decel: return std::pow (u, 1.0 + 2.5 * amount);
        case RatCurve::Bounce:
        {
            const double r = 0.92 - 0.5 * amount;                        // restitution: each gap is r times the last
            return (1.0 - std::pow (r, i)) / (1.0 - std::pow (r, n));
        }
    }
}

int SeqEngine::expandHits (const StepEval& se, uint32_t seed, SeqHit* hits)
{
    if (! se.trig) return 0;
    const Step& s = se.step;
    const int n = clamp ((int) s.ratCount, 1, kMaxRatchets);
    const float baseVel = clamp ((float) s.vel / 127.f * se.velScale, 0.02f, 1.f);
    const double span = se.stepBeats;
    const double bounceR = 0.92 - 0.5 * clamp01 (s.ratAmt);
    for (int i = 0; i < n; ++i)
    {
        SeqHit& h = hits[i];
        const double p0 = ratchetPos (i, n, s.ratCurve, s.ratAmt);
        const double p1 = (i + 1 < n) ? ratchetPos (i + 1, n, s.ratCurve, s.ratAmt) : 1.0;
        h.beat = se.time + span * p0;
        h.gateBeats = (n == 1) ? (double) clamp (s.len, 0.02f, 64.f) * span
                               : std::max (0.0, (p1 - p0)) * span * (double) clamp (s.ratGate, 0.05f, 1.f);
        float v = baseVel;
        if (n > 1)
        {
            const float u = (float) i / (float) (n - 1);
            if (s.ratVel < 0.f) v *= 1.f + s.ratVel * u;
            else                v *= 1.f - s.ratVel * (1.f - u);
            if (s.ratCurve == RatCurve::Bounce) v *= (float) std::pow (bounceR, 0.7 * i);
            h.pitch = (float) s.pitch + s.ratPitch * u;
        }
        else h.pitch = (float) s.pitch;
        h.velocity = clamp (v, 0.02f, 1.f);
        h.slide = s.slide != 0 && i == 0;
        h.ratIndex = (uint8_t) i;
        h.seed = hashN (seed, sHit, (uint32_t) (uint64_t) se.abs, (uint32_t) (uint64_t) se.cycle, (uint32_t) i);
    }
    return n;
}

} // namespace atm
