// Run one effect over test material and print measurements.
//
//   fx_render <fx> <out.wav> [in=drums|noise|sine|saw|chord|impulse|sweep|<file.wav>] [sr=48000] [dur=4]
//             [bpm=120] [mix=1] <param>=<value> ... [<param>@<t0>:<t1>=<value> ...]
//
// Params use the keys and physical units of the effect's descriptor table. A timed override such as
//   on@1.0:1.5=1     engages a parameter between 1.0 s and 1.5 s (like a parameter lock would).
#include "ToolUtil.h"
#include "fx/Effect.h"
#include "model/Model.h"
#include "util/Denormals.h"
using namespace atm;

int main (int argc, char** argv)
{
    ScopedFlushDenormals fz;
    const tool::Args a = tool::parseArgs (argc, argv);
    if (a.positional.size() < 2) { std::printf ("usage: fx_render <fx> <out.wav> [options] key=value...\n"); return 2; }
    const int type = fxFromKey (a.positional[0].c_str());
    if (type <= 0) { std::printf ("unknown effect '%s'\n", a.positional[0].c_str()); return 2; }
    const double sr = a.num ("sr", 48000.0), dur = a.num ("dur", 4.0), bpm = a.num ("bpm", 120.0);
    const float mix = (float) a.num ("mix", 1.0);

    std::vector<float> l, r;
    const std::string in = a.get ("in", "drums");
    if (in.size() > 4 && in.substr (in.size() - 4) == ".wav")
    {
        std::vector<std::vector<float>> ch; int fsr = 0;
        if (! readWav (in, ch, fsr) || ch.empty()) { std::printf ("cannot read %s\n", in.c_str()); return 2; }
        l = ch[0]; r = ch.size() > 1 ? ch[1] : ch[0];
        l.resize ((size_t) (sr * dur), 0.f); r.resize ((size_t) (sr * dur), 0.f);
    }
    else tool::makeTestSignal (in, sr, dur, bpm, l, r);

    const ParamDesc* descs = fxParamDescs (type);
    float base[kFxParams];
    for (int i = 0; i < kFxParams; ++i) base[i] = descs[i].key[0] ? descs[i].defNorm() : 0.f;
    struct Timed { int idx; double t0, t1; float v; };
    std::vector<Timed> timed;
    static const char* const reserved[] = { "in", "sr", "dur", "bpm", "mix" };
    for (auto& kv : a.kv)
    {
        bool skip = false; for (auto rsv : reserved) if (kv.first == rsv) skip = true;
        if (skip) continue;
        std::string key = kv.first; double t0 = -1, t1 = -1;
        const size_t at = key.find ('@');
        if (at != std::string::npos)
        {
            const std::string range = key.substr (at + 1); key = key.substr (0, at);
            const size_t colon = range.find (':');
            t0 = std::atof (range.substr (0, colon).c_str()); t1 = colon == std::string::npos ? dur : std::atof (range.substr (colon + 1).c_str());
        }
        int idx = -1;
        for (int i = 0; i < kFxParams; ++i) if (descs[i].key[0] && key == descs[i].key) idx = i;
        float n;
        if (idx < 0 || ! parseParam (descs[idx], kv.second.c_str(), n)) { std::printf ("unknown or bad param: %s=%s\n", kv.first.c_str(), kv.second.c_str()); return 2; }
        if (t0 >= 0) timed.push_back ({ idx, t0, t1, n }); else base[idx] = n;
    }

    auto fx = createEffect (type);
    if (! fx) { std::printf ("effect '%s' is not implemented yet\n", a.positional[0].c_str()); return 2; }
    fx->prepare (sr); fx->reset();
    const int total = (int) l.size();
    std::vector<std::vector<float>> out (2);
    out[0] = l; out[1] = r;
    std::vector<float> scL = l, scR = r;
    FxContext ctx; ctx.sampleRate = sr; ctx.bpm = bpm; ctx.playing = true;
    float tl[16], tr[16];
    tool::Timer timer;
    for (int pos = 0; pos < total; pos += 16)
    {
        const int n = std::min (16, total - pos);
        float p[kFxParams]; std::memcpy (p, base, sizeof (p));
        const double t = pos / sr;
        for (auto& tm : timed) if (t >= tm.t0 && t < tm.t1) p[tm.idx] = tm.v;
        ctx.beat = t * bpm / 60.0;
        for (int s = 1; s < kScSources; ++s) { ctx.scL[s] = scL.data() + pos; ctx.scR[s] = scR.data() + pos; }
        fx->setParams (p, ctx);
        std::memcpy (tl, out[0].data() + pos, sizeof (float) * (size_t) n);
        std::memcpy (tr, out[1].data() + pos, sizeof (float) * (size_t) n);
        fx->process (tl, tr, n, ctx);
        for (int i = 0; i < n; ++i)
        {
            out[0][(size_t) (pos + i)] += (tl[i] - out[0][(size_t) (pos + i)]) * mix;
            out[1][(size_t) (pos + i)] += (tr[i] - out[1][(size_t) (pos + i)]) * mix;
        }
    }
    const double el = timer.seconds();
    tool::printStats ("in", tool::analyse (l, sr));
    const auto sl = tool::analyse (out[0], sr), srr = tool::analyse (out[1], sr);
    tool::printStats ("L", sl); tool::printStats ("R", srr);
    std::printf ("cpu    %.2f%% of real time\n", 100.0 * el / dur);
    writeWav (a.positional[1], out, (int) sr, 24);
    return (sl.nan || srr.nan) ? 1 : 0;
}
