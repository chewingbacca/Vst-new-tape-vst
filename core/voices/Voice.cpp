#include "Voice.h"

namespace atm {

std::unique_ptr<EngineBase> createDrumEngine();
std::unique_ptr<EngineBase> createFmEngine();
std::unique_ptr<EngineBase> createSamplerEngine();
std::unique_ptr<EngineBase> createGranularEngine();
std::unique_ptr<EngineBase> createResonatorEngine();
std::unique_ptr<EngineBase> createSynthEngine();

std::unique_ptr<EngineBase> createEngine (int type)
{
    switch (type)
    {
        case Engine::Drum:      return createDrumEngine();
        case Engine::FM:        return createFmEngine();
        case Engine::Sampler:   return createSamplerEngine();
        case Engine::Granular:  return createGranularEngine();
        case Engine::Resonator: return createResonatorEngine();
        case Engine::Synth:     return createSynthEngine();
        default: break;
    }
    return createDrumEngine();
}

void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    for (int i = 0; i < Engine::count; ++i)
    {
        engines[i] = createEngine (i);
        engines[i]->prepare (sampleRate);
    }
    amp.prepare ((float) sr); mod.prepare ((float) sr);
    for (auto& d : dcb) d.prepare ((float) sr, 10.f);
    reset();
}

void Voice::setSample (const SampleData* s)
{
    for (auto& e : engines) if (e) e->setSample (s);
}

void Voice::reset()
{
    isActive = false; gateOpen = false; eng = nullptr; engineType = -1;
    amp.reset(); mod.reset();
    for (auto& ch : svf) for (auto& f : ch) f.reset();
    for (auto& l : lad) l.reset();
    for (auto& o : os) o.reset();
    for (auto& d : dcb) d.reset();
    for (auto& e : engines) if (e) e->reset();
}

static inline float tunedNote (float note, const float* p)
{
    // Tune: -36..36 st, Fine: -100..100 ct (both linear descriptors)
    return note + (p[TP::Tune] * 72.f - 36.f) + (p[TP::Fine] * 2.f - 1.f);
}

void Voice::start (const VoiceStart& s, int type, const float* p)
{
    type = clamp (type, 0, Engine::count - 1);
    eng = engines[type].get();
    engineType = type;
    vel = clamp01 (s.velocity);
    noteRnd = hashToBipolar (hashN (s.seed, 0x51u));
    const float accAmt = p[TP::Accent];
    accent = accAmt * clamp01 ((vel - 0.72f) / 0.28f);

    targetNote = s.note;
    const float glide = p[TP::Glide] * p[TP::Glide] * p[TP::Glide] * 2.f;
    if (s.glideFrom > -500.f && (glide > 0.0005f || s.slide)) curNote = s.glideFrom;
    else curNote = targetNote;

    // fresh filter/drive state for a fresh voice
    for (auto& ch : svf) for (auto& f : ch) f.reset();
    for (auto& l : lad) l.reset();
    for (auto& o : os) o.reset();
    for (auto& d : dcb) d.reset();

    amp.reset(); mod.reset();
    isActive = true; gateOpen = true; firstControl = true;
    gateLeft = s.gateSamples;

    EngineNote en;
    en.note = tunedNote (curNote, p);
    en.velocity = vel;
    en.legato = false;
    en.seed = s.seed;
    eng->noteOn (en, &p[TP::E0]);

    control (p, 1);
    amp.gateOn(); mod.gateOn();
    gCur = gTarget; gStep = 0.f;
}

void Voice::slideTo (const VoiceStart& s, const float* p)
{
    if (! isActive || eng == nullptr) { start (s, engineType < 0 ? 0 : engineType, p); return; }
    targetNote = s.note;
    vel = clamp01 (s.velocity);
    accent = p[TP::Accent] * clamp01 ((vel - 0.72f) / 0.28f);
    gateOpen = true;
    gateLeft = s.gateSamples;
    amp.gateOn (true); mod.gateOn (true);
    if (amp.stage == Envelope::Release) { amp.stage = Envelope::Attack; }
}

void Voice::gateOff()
{
    if (! gateOpen) return;
    gateOpen = false;
    amp.gateOff(); mod.gateOff();
    if (eng) eng->noteOff();
}

void Voice::kill()
{
    if (! isActive) return;
    gateOpen = false;
    amp.kill (0.003f);
}

void Voice::updateFilterTargets (const float* p)
{
    static const ParamDesc& typeDesc = commonTrackParamDesc (TP::FltType);
    fltType = typeDesc.toIndex (p[TP::FltType]);
    if (fltType == 0) return;
    // cutoff in octaves relative to 20 Hz: norm * log2(1000)
    float oct = p[TP::FltCut] * 9.96578f;
    oct += (p[TP::FltEnv] * 2.f - 1.f) * 8.f * (1.f + accent) * mod.level;
    oct += p[TP::FltKey] * (curNote - 60.f) * (1.f / 12.f);
    oct += accent * 1.5f;
    const float hz = clamp (20.f * std::exp2 (oct), 20.f, (float) sr * 0.45f);
    gTarget = std::tan (kPiF * hz / (float) sr);
    const float res = p[TP::FltRes];
    kRes = Svf::kFor (res);
    ladRes = res;
}

void Voice::control (const float* p, int samplesUntilNext)
{
    if (! isActive) return;
    // an engine that has run out of sound frees the voice; decided here, on the control grid,
    // so the result never depends on how the host splits its buffers
    if (eng != nullptr && eng->finished() && amp.stage != Envelope::Kill && amp.stage != Envelope::Idle) amp.kill (0.004f);

    // envelopes
    {
        static const ParamDesc &dA = commonTrackParamDesc (TP::AmpA), &dH = commonTrackParamDesc (TP::AmpH), &dD = commonTrackParamDesc (TP::AmpD),
                               &dR = commonTrackParamDesc (TP::AmpR), &mA = commonTrackParamDesc (TP::ModA), &mD = commonTrackParamDesc (TP::ModD),
                               &mR = commonTrackParamDesc (TP::ModR);
        amp.set (dA.toPhys (p[TP::AmpA]), dH.toPhys (p[TP::AmpH]), dD.toPhys (p[TP::AmpD]), p[TP::AmpS], dR.toPhys (p[TP::AmpR]));
        mod.set (mA.toPhys (p[TP::ModA]), 0.f, mD.toPhys (p[TP::ModD]), p[TP::ModS], mR.toPhys (p[TP::ModR]));
    }

    // pitch glide (control-rate one-pole in the note domain)
    {
        float glide = p[TP::Glide] * p[TP::Glide] * p[TP::Glide] * 2.f;
        if (glide < 0.0005f && std::fabs (curNote - targetNote) > 0.001f) glide = 0.045f;   // slides without a glide time still glide
        if (glide < 0.0005f) curNote = targetNote;
        else
        {
            const float c = 1.f - std::exp (-(float) samplesUntilNext * 3.f / (glide * (float) sr));
            curNote += c * (targetNote - curNote);
            if (std::fabs (curNote - targetNote) < 0.002f) curNote = targetNote;
        }
    }
    if (eng) eng->control (&p[TP::E0], tunedNote (curNote, p));

    // filter
    updateFilterTargets (p);
    if (firstControl) gCur = gTarget;
    gStep = (gTarget - gCur) / (float) std::max (1, samplesUntilNext);

    // drive
    {
        const float amt = p[TP::DrvAmt];
        static const ParamDesc& tDesc = commonTrackParamDesc (TP::DrvType);
        driveOn = amt > 0.0005f || accent > 0.01f;
        drvType = tDesc.toIndex (p[TP::DrvType]);
        drvPost = p[TP::DrvPos] >= 0.5f;
        const float db = (amt + accent * 0.25f) * 36.f;
        drvGain = std::pow (10.f, db * 0.05f);
        drvComp = 1.f / std::sqrt (1.f + (drvGain - 1.f) * 0.12f);
    }

    // amp
    {
        const float vs = p[TP::AmpVel];
        ampGain = (1.f - vs + vs * vel * vel) * (1.f + 0.35f * accent);
    }
    firstControl = false;
}

inline float Voice::shape (float x) const
{
    switch (drvType)
    {
        default:
        case 0: return fastTanh (x);
        case 1: return clamp (x, -1.f, 1.f);
        case 2: { float t = x * 0.25f + 0.25f; t -= std::floor (t); return 4.f * std::fabs (t - 0.5f) - 1.f; }   // triangle fold
        case 3: return x >= 0.f ? fastTanh (x) : fastTanh (x * 0.55f) * 1.25f + 0.f;                             // asymmetric
        case 4: return fastTanh (std::fabs (x) * 1.4f - 0.35f * std::min (1.f, std::fabs (x) * 4.f));            // full-wave rectifier
    }
}

void Voice::render (float* outL, float* outR, int n)
{
    if (! isActive || eng == nullptr) return;

    // If the gate ends inside this slice, render up to that sample first, so the engine hears its
    // note-off on the exact sample (bowed and gated engines change their sound at note-off).
    if (gateOpen && gateLeft > 0 && gateLeft < n)
    {
        const int k = gateLeft;
        render (outL, outR, k);
        render (outL + k, outR + k, n - k);
        return;
    }

    eng->render (bufL, bufR, n);
    const bool stereo = eng->isStereo();
    const int chans = stereo ? 2 : 1;
    float* bufs[2] = { bufL, bufR };

    // gate countdown (sample accurate release)
    int gateOffAt = -1;
    if (gateOpen && gateLeft > 0)
    {
        if (gateLeft <= n) { gateOffAt = gateLeft - 1; gateLeft = 0; }
        else gateLeft -= n;
    }

    // --- drive (pre)
    auto runDrive = [&]
    {
        for (int c = 0; c < chans; ++c)
        {
            float* b = bufs[c];
            for (int i = 0; i < n; ++i)
            {
                const float y = os[c].process (b[i] * drvGain, [this] (float v) { return shape (v); });
                b[i] = (drvType >= 3 ? dcb[c].process (y) : y) * drvComp;
            }
        }
    };
    if (driveOn && ! drvPost) runDrive();

    // --- filter with per-sample interpolated cutoff
    if (fltType != 0)
    {
        float g = gCur;
        if (fltType == 7)
        {
            for (int i = 0; i < n; ++i)
            {
                g += gStep;
                for (int c = 0; c < chans; ++c)
                {
                    lad[c].setG (std::min (g, 3.0f));
                    lad[c].k = 4.1f * ladRes; lad[c].comp = 1.f + 0.6f * lad[c].k;
                    bufs[c][i] = lad[c].tick (bufs[c][i], 1.f);
                }
            }
        }
        else
        {
            const bool twoPole = (fltType == 1 || fltType == 3 || fltType == 4 || fltType == 6);
            for (int i = 0; i < n; ++i)
            {
                g += gStep;
                for (int c = 0; c < chans; ++c)
                {
                    Svf& a = svf[c][0];
                    a.setGK (g, kRes);
                    const float x = bufs[c][i];
                    const auto o = a.tick (x);
                    float y;
                    switch (fltType)
                    {
                        default:
                        case 1: y = o.lp; break;
                        case 2: { Svf& b = svf[c][1]; b.setGK (g, 1.4142f); y = b.tick (o.lp).lp; break; }
                        case 3: y = o.bp * std::max (kRes, 0.35f); break;      // keep band-pass level sensible at low Q
                        case 4: y = o.hp; break;
                        case 5: { Svf& b = svf[c][1]; b.setGK (g, 1.4142f); y = b.tick (o.hp).hp; break; }
                        case 6: y = o.lp + o.hp; break;
                    }
                    (void) twoPole;
                    bufs[c][i] = y;
                }
            }
        }
        gCur = g;
        for (int c = 0; c < chans; ++c) { svf[c][0].sanitise(); svf[c][1].sanitise(); }
    }

    if (driveOn && drvPost) runDrive();

    // --- amp + mod envelope, accumulate
    for (int i = 0; i < n; ++i)
    {
        const float a = amp.tick() * ampGain;
        mod.tick();
        const float l = bufL[i] * a;
        outL[i] += l;
        outR[i] += stereo ? bufR[i] * a : l;
        if (i == gateOffAt) { gateOpen = false; amp.gateOff(); mod.gateOff(); eng->noteOff(); }
    }

    if (! amp.active()) { isActive = false; gateOpen = false; }
}

} // namespace atm
