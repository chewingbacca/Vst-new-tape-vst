#include "Generators.h"

namespace atm {

void clearSteps (TrackPattern& tp)
{
    for (auto& s : tp.steps) { const int8_t pitch = s.pitch; s = Step{}; s.pitch = pitch; }
}

void rotatePattern (TrackPattern& tp, int steps)
{
    const int L = clamp ((int) tp.length, 1, kMaxSteps);
    steps = wrapMod (steps, L);
    if (steps == 0) return;
    Step tmp[kMaxSteps]; float sm[kMaxSteps]; float lv[kLockLanes][kMaxSteps]; uint64_t masks[kLockLanes] = {};
    for (int i = 0; i < L; ++i)
    {
        const int j = (i + steps) % L;
        tmp[j] = tp.steps[i]; sm[j] = tp.stepMod[i];
        for (int l = 0; l < kLockLanes; ++l)
        {
            lv[l][j] = tp.lanes[l].value[i];
            if (tp.lanes[l].has (i)) masks[l] |= (uint64_t) 1 << j;
        }
    }
    for (int i = 0; i < L; ++i)
    {
        tp.steps[i] = tmp[i]; tp.stepMod[i] = sm[i];
        for (int l = 0; l < kLockLanes; ++l) tp.lanes[l].value[i] = lv[l][i];
    }
    for (int l = 0; l < kLockLanes; ++l)
    {
        const uint64_t keep = L >= 64 ? 0 : (tp.lanes[l].mask & ~(((uint64_t) 1 << L) - 1));
        tp.lanes[l].mask = keep | masks[l];
    }
}

// Strength of a step within the bar: 1 on the downbeat, falling through beats, eighths and sixteenths.
float metricWeight (int step, int length)
{
    (void) length;
    const int s = step % 16;
    if (s == 0) return 1.f;
    if (s % 8 == 0) return 0.85f;
    if (s % 4 == 0) return 0.7f;
    if (s % 2 == 0) return 0.45f;
    return 0.25f;
}

static void ensurePitches (TrackPattern& tp, Rng& rng, int role)
{
    if (! roleIsMelodic (role)) return;
    const int L = clamp ((int) tp.length, 1, kMaxSteps);
    bool any = false;
    for (int i = 0; i < L; ++i) any |= tp.steps[i].pitch != 0;
    if (any) return;
    // a small seeded contour around the root: steps, thirds and the odd leap
    static constexpr int8_t pool[10] = { 0, 0, 2, 4, -3, 7, 2, -1, 4, 9 };
    int cur = 0;
    for (int i = 0; i < L; ++i)
    {
        if (rng.chance (0.55f)) cur = pool[rng.irange (0, 9)];
        tp.steps[i].pitch = (int8_t) cur;
    }
}

void genEuclid (TrackPattern& tp, uint32_t seed, const float* p, int role)
{
    const int L = clamp ((int) tp.length, 1, kMaxSteps);
    const int k = clamp ((int) std::lround (p[0] * (float) L), 0, L);
    const int rot = (int) std::lround (p[1] * (float) L);
    Rng rng (seed);
    clearSteps (tp);
    for (int i = 0; i < L; ++i)
    {
        const int j = wrapMod (i - rot, L);
        const bool on = k > 0 && ((j * k) % L) < k;
        Step& s = tp.steps[i];
        s.on = on;
        if (! on) continue;
        const bool accent = rng.chance (p[2]) || (i == 0 && p[2] > 0.05f);
        const float base = accent ? 118.f : 88.f;
        s.vel = (uint8_t) clamp ((int) (base + rng.bip() * p[3] * 40.f), 20, 127);
        s.len = roleIsMelodic (role) ? 0.9f : 0.5f;
    }
    ensurePitches (tp, rng, role);
}

void genCellular (TrackPattern& tp, uint32_t seed, const float* p, int role)
{
    const int L = clamp ((int) tp.length, 1, kMaxSteps);
    const int rule = clamp ((int) std::lround (p[0] * 255.f), 0, 255);
    const int gens = (int) std::lround (p[2] * 32.f);
    Rng rng (seed);
    bool row[kMaxSteps] = {}, next[kMaxSteps];
    int alive = 0;
    for (int i = 0; i < L; ++i) { row[i] = rng.chance (clamp (p[1], 0.02f, 0.95f)); alive += row[i]; }
    if (alive == 0) row[L / 2] = true;
    for (int g = 0; g < gens; ++g)
    {
        for (int i = 0; i < L; ++i)
        {
            const int n = (row[wrapMod (i - 1, L)] ? 4 : 0) | (row[i] ? 2 : 0) | (row[wrapMod (i + 1, L)] ? 1 : 0);
            next[i] = (rule >> n) & 1;
        }
        for (int i = 0; i < L; ++i) row[i] = next[i];
    }
    clearSteps (tp);
    for (int i = 0; i < L; ++i)
    {
        Step& s = tp.steps[i];
        s.on = row[i];
        if (! row[i]) continue;
        // louder in crowded neighbourhoods, softer when isolated
        const int nb = (row[wrapMod (i - 1, L)] ? 1 : 0) + (row[wrapMod (i + 1, L)] ? 1 : 0);
        const float v = 96.f + ((float) nb - 1.f) * 26.f * p[3] + (metricWeight (i, L) - 0.5f) * 24.f;
        s.vel = (uint8_t) clamp ((int) v, 20, 127);
        s.len = roleIsMelodic (role) ? 0.9f : 0.5f;
    }
    ensurePitches (tp, rng, role);
}

void genDensity (TrackPattern& tp, uint32_t seed, const float* p, int role)
{
    const int L = clamp ((int) tp.length, 1, kMaxSteps);
    Rng rng (seed);
    clearSteps (tp);
    bool prev = false;
    for (int i = 0; i < L; ++i)
    {
        const float mw = metricWeight (i, L);
        float w = lerp (mw, 1.15f - mw, clamp01 (p[1]));
        float prob = clamp01 (p[0] * 1.6f * w);
        // clustering: hits attract hits, gaps attract gaps
        if (p[2] > 0.f) prob = clamp01 (prev ? prob + p[2] * 0.45f : prob * (1.f - p[2] * 0.5f));
        Step& s = tp.steps[i];
        s.on = rng.chance (prob);
        prev = s.on;
        if (! s.on) continue;
        s.vel = (uint8_t) clamp ((int) (70.f + mw * 45.f + rng.bip() * 14.f), 20, 127);
        s.len = roleIsMelodic (role) ? 0.9f : 0.5f;
        if (rng.chance (p[3] * 0.5f))
        {
            static constexpr uint8_t counts[5] = { 2, 2, 3, 4, 6 };
            s.ratCount = counts[rng.irange (0, 4)];
            s.ratCurve = (uint8_t) rng.irange (0, RatCurve::count - 1);
            s.ratAmt = rng.range (0.3f, 0.9f);
            s.ratVel = rng.range (-0.8f, -0.1f);
        }
    }
    ensurePitches (tp, rng, role);
}

void genMarkov (TrackPattern& tp, uint32_t seed, const float* p, int role)
{
    const int L = clamp ((int) tp.length, 1, kMaxSteps);
    // tokens are the distinct steps of the existing pattern; transitions are what followed what
    int next[kMaxSteps];
    int onCount = 0;
    for (int i = 0; i < L; ++i) { next[i] = (i + 1) % L; onCount += tp.steps[i].on; }
    if (onCount == 0) return;
    auto similar = [&] (int a, int b)
    {
        const Step& x = tp.steps[a]; const Step& y = tp.steps[b];
        if (x.on != y.on) return false;
        if (! x.on) return true;
        return x.pitch == y.pitch && std::abs ((int) x.vel - (int) y.vel) < 24;
    };
    Rng rng (seed);
    Step out[kMaxSteps];
    int cur = 0;
    for (int i = 0; i < L; ++i)
    {
        out[i] = tp.steps[cur];
        if (out[i].on)
        {
            if (roleIsMelodic (role) && rng.chance (p[1] * 0.4f))
            {
                static constexpr int8_t moves[6] = { -2, -1, 1, 2, 3, -3 };
                out[i].pitch = (int8_t) clamp ((int) out[i].pitch + moves[rng.irange (0, 5)], -48, 48);
            }
            if (rng.chance (p[2] * 0.6f)) out[i].vel = (uint8_t) clamp ((int) out[i].vel + (int) (rng.bip() * 30.f), 20, 127);
        }
        // choose the successor: follow one of the steps that came after a similar step, or jump anywhere
        if (rng.chance (clamp (p[0], 0.f, 1.f) * 0.9f + 0.1f))
        {
            int cands[kMaxSteps], n = 0;
            for (int j = 0; j < L; ++j) if (similar (j, cur)) cands[n++] = next[j];
            cur = n > 0 ? cands[rng.irange (0, n - 1)] : next[cur];
        }
        else cur = rng.irange (0, L - 1);
    }
    for (int i = 0; i < L; ++i) tp.steps[i] = out[i];
}

bool genMutate (TrackPattern& tp, uint32_t seed, float amount, float rhythmW, float pitchW, float detailW, int role)
{
    const int L = clamp ((int) tp.length, 1, kMaxSteps);
    Rng rng (seed);
    bool changed = false;
    const bool melodic = roleIsMelodic (role);
    const bool anchor = role == Role::Kick || role == Role::Snare;

    for (int i = 0; i < L; ++i)
    {
        Step& s = tp.steps[i];
        const float mw = metricWeight (i, L);
        // --- rhythm
        if (rng.chance (amount * rhythmW * 0.35f))
        {
            if (s.on)
            {
                // strong beats of anchor tracks resist removal
                const float keep = anchor ? mw : mw * 0.5f;
                if (! rng.chance (keep))
                {
                    if (rng.chance (0.5f))
                    {
                        // nudge the hit to a free neighbour instead of deleting it
                        const int j = wrapMod (i + (rng.chance (0.5f) ? 1 : -1), L);
                        if (! tp.steps[j].on) { tp.steps[j] = s; s.on = 0; changed = true; }
                    }
                    else { s.on = 0; changed = true; }
                }
            }
            else
            {
                s.on = 1; s.prob = 100; s.cond = Cond::Always; s.ratCount = 1; s.micro = 0.f;
                s.vel = (uint8_t) clamp ((int) (55.f + mw * 40.f + rng.bip() * 15.f), 20, 127);
                s.len = melodic ? 0.9f : 0.5f;
                if (melodic)
                    for (int k = 1; k < L; ++k) { const Step& o = tp.steps[wrapMod (i - k, L)]; if (o.on) { s.pitch = o.pitch; break; } }
                changed = true;
            }
        }
        if (! s.on) continue;
        // --- velocity
        if (rng.chance (amount * 0.4f))
        {
            s.vel = (uint8_t) clamp ((int) s.vel + (int) (rng.bip() * 36.f * amount + 0.5f), 20, 127);
            changed = true;
        }
        // --- pitch
        if (melodic && rng.chance (amount * pitchW * 0.4f))
        {
            static constexpr int8_t moves[8] = { -1, 1, -2, 2, 3, -3, 4, 7 };
            s.pitch = (int8_t) clamp ((int) s.pitch + moves[rng.irange (0, amount > 0.5f ? 7 : 3)], -36, 36);
            changed = true;
        }
        // --- detail
        if (rng.chance (amount * detailW * 0.25f))
        {
            const int what = rng.irange (0, 3);
            if (what == 0)
            {
                if (s.ratCount > 1) s.ratCount = 1;
                else
                {
                    static constexpr uint8_t counts[6] = { 2, 2, 3, 4, 3, 6 };
                    s.ratCount = counts[rng.irange (0, 5)];
                    s.ratCurve = (uint8_t) rng.irange (0, RatCurve::count - 1);
                    s.ratAmt = rng.range (0.3f, 0.9f);
                    s.ratVel = rng.range (-0.8f, 0.2f);
                    if (rng.chance (0.2f)) s.ratPitch = rng.chance (0.5f) ? 12.f : -12.f;
                }
            }
            else if (what == 1) s.micro = clamp (s.micro + rng.bip() * 0.2f, -0.45f, 0.45f);
            else if (what == 2) s.prob = (uint8_t) clamp ((int) s.prob + (int) (rng.bip() * 40.f), 30, 100);
            else s.len = clamp (s.len * rng.range (0.5f, 1.8f), 0.1f, 8.f);
            changed = true;
        }
    }
    // never leave a track completely empty
    if (tp.countOn() == 0) { tp.steps[0].on = 1; tp.steps[0].vel = 100; changed = true; }
    return changed;
}

bool evolveTrackPattern (TrackPattern& tp, float amount, uint32_t seed, int role)
{
    return genMutate (tp, seed, clamp01 (amount) * 0.45f, 0.5f, 0.6f, 0.6f, role);
}

void runGenerator (TrackPattern& tp, int role)
{
    const GeneratorState g = tp.gen;      // copy: generators may clear steps but must not lose the settings
    const float* p = g.p[clamp ((int) g.type, 0, (int) Gen::count - 1)];
    switch (g.type)
    {
        case Gen::Euclid:   genEuclid (tp, g.seed, p, role); break;
        case Gen::Cellular: genCellular (tp, g.seed, p, role); break;
        case Gen::Density:  genDensity (tp, g.seed, p, role); break;
        case Gen::Markov:   genMarkov (tp, g.seed, p, role); break;
        case Gen::Mutate:   genMutate (tp, g.seed, p[0], p[1], p[2], p[3], role); break;
        default: break;
    }
    tp.gen = g;
}

} // namespace atm
