// Factory content and project-level behaviour: presets parse, kits build, New Idea is
// deterministic, serialisation round-trips, generators are seeded.
#include "TestFramework.h"
#include "TestHost.h"
#include "factory/Presets.h"
#include "factory/FactorySamples.h"
#include "gen/Idea.h"
#include "gen/Generators.h"
#include "model/Serialize.h"
#include "engine/SamplePool.h"
#include <string>
using namespace atm;

TEST (content_voice_presets_all_parse)
{
    CHECK_MSG (numVoicePresets() >= 150, "only %d voice presets", numVoicePresets());
    SamplePool pool;
    std::string errors;
    for (int i = 0; i < numVoicePresets(); ++i)
    {
        VoicePresetData v;
        loadVoicePresetChecked (i, v, &pool, &errors);
        CHECK (v.sound.engine == voicePresetInfo (i).engine);
        for (int k = 0; k < TP::count; ++k) CHECK (v.sound.params[k] >= 0.f && v.sound.params[k] <= 1.f);
    }
    CHECK_MSG (errors.empty(), "\n%s", errors.c_str());
    // names are unique
    for (int i = 0; i < numVoicePresets(); ++i) CHECK_MSG (findVoicePreset (voicePresetInfo (i).name) == i, "duplicate preset name %s", voicePresetInfo (i).name);
}

TEST (content_factory_samples_build)
{
    for (int i = 0; i < numFactorySamples(); ++i)
    {
        const SampleData* s = getFactorySample (factorySampleInfo (i).name);
        CHECK_MSG (s != nullptr && s->length > 1000, "sample %s", factorySampleInfo (i).name);
        if (s == nullptr) continue;
        CHECK_MSG (s->peak > 0.3f && s->peak <= 1.0f, "sample %s peak %g", factorySampleInfo (i).name, s->peak);
        bool finite = true;
        for (int c = 0; c < s->numChannels; ++c) for (int k = 0; k < s->length; ++k) finite &= std::isfinite (s->data (0, c)[k]);
        CHECK (finite);
    }
    // breaks have several transients
    const SampleData* b = getFactorySample ("Brk_Clatter");
    CHECK (b != nullptr && b->transients.size() >= 12);
}

TEST (content_kits_build_and_have_forty_across_styles)
{
    CHECK (numKitPresets() >= 40);
    int perStyle[Style::count] = {};
    SamplePool pool;
    for (int i = 0; i < numKitPresets(); ++i)
    {
        auto p = std::make_unique<Project>();
        CHECK (buildKitPreset (i, *p, &pool));
        ++perStyle[kitPresetInfo (i).style];
        int hits = 0;
        for (int t = 0; t < kTracks; ++t) hits += p->patterns[0].tracks[t].countOn();
        CHECK_MSG (hits >= 12, "kit %s has only %d hits", kitPresetInfo (i).name, hits);
    }
    for (int s = 0; s < Style::count; ++s) CHECK (perStyle[s] >= 8);
}

TEST (content_new_idea_is_deterministic_and_seed_sensitive)
{
    SamplePool pool;
    auto a = std::make_unique<Project>(), b = std::make_unique<Project>(), c = std::make_unique<Project>();
    initProject (*a); initProject (*b); initProject (*c);
    IdeaOptions o; o.style = Style::DrillBass; o.seed = 4242;
    generateIdea (*a, o, &pool);
    generateIdea (*b, o, &pool);
    o.seed = 4243;
    generateIdea (*c, o, &pool);
    const std::string ja = projectToJson (*a, &pool).dump(), jb = projectToJson (*b, &pool).dump(), jc = projectToJson (*c, &pool).dump();
    CHECK (ja == jb);
    CHECK (ja != jc);
}

TEST (content_new_idea_keeps_locked_tracks)
{
    SamplePool pool;
    auto p = std::make_unique<Project>();
    initProject (*p);
    IdeaOptions o; o.style = Style::Acid; o.seed = 77;
    generateIdea (*p, o, &pool);
    p->kit.tracks[4].locked = 1;
    const TrackSound keepSound = p->kit.tracks[4];
    const TrackPattern keepPat = p->patterns[0].tracks[4];
    o.seed = 78; o.style = Style::ClicksCuts;
    generateIdea (*p, o, &pool);
    CHECK (std::memcmp (&p->kit.tracks[4], &keepSound, sizeof (TrackSound)) == 0);
    CHECK (std::memcmp (&p->patterns[0].tracks[4], &keepPat, sizeof (TrackPattern)) == 0);
}

TEST (content_project_json_round_trip)
{
    SamplePool pool;
    for (int k : { 0, 9, 17, 26, 35 })
    {
        auto p = std::make_unique<Project>();
        buildKitPreset (k, *p, &pool);
        p->patterns[0].tracks[2].setLock (3, TP::FltCut, 0.37f);
        p->patterns[0].tracks[2].setLock (5, kDestGlobalBase + GP::Glitch, 0.8f);
        const std::string a = projectToJson (*p, &pool).dump();
        bool ok = false;
        const Json j = Json::parse (a, &ok);
        CHECK (ok);
        auto q = std::make_unique<Project>();
        CHECK (projectFromJson (j, *q, &pool));
        const std::string b = projectToJson (*q, &pool).dump();
        CHECK_MSG (a == b, "kit %d does not round-trip", k);
        float v = 0.f;
        CHECK (q->patterns[0].tracks[2].getLock (5, kDestGlobalBase + GP::Glitch, v) && std::fabs (v - 0.8f) < 1e-6f);
    }
}

TEST (content_generators_seeded)
{
    for (int g = 0; g < Gen::count; ++g)
    {
        TrackPattern a, b;
        for (int i = 0; i < 16; i += 3) { a.steps[i].on = 1; a.steps[i].pitch = (int8_t) i; }
        b = a;
        a.gen.type = b.gen.type = (uint8_t) g;
        a.gen.seed = b.gen.seed = 99;
        runGenerator (a, Role::Lead);
        runGenerator (b, Role::Lead);
        CHECK (std::memcmp (&a, &b, sizeof (TrackPattern)) == 0);
    }
    TrackPattern e; e.length = 16; e.gen.type = Gen::Euclid; e.gen.p[Gen::Euclid][0] = 5.f / 16.f; e.gen.p[Gen::Euclid][1] = 0.f;
    runGenerator (e, Role::Perc);
    CHECK (e.countOn() == 5);
}

TEST (content_kits_render_clean)
{
    // every kit through the real engine: finite, not silent, not clipping before the limiter
    SamplePool pool;
    for (int i = 0; i < numKitPresets(); ++i)
    {
        auto p = std::make_unique<Project>();
        buildKitPreset (i, *p, &pool);
        const SampleData* samples[kTracks];
        for (int t = 0; t < kTracks; ++t) samples[t] = pool.data (p->kit.tracks[t].sampleId);
        OfflineOptions o; o.bpm = kitPresetInfo (i).bpm; o.bars = 2; o.tailSeconds = 0.5; o.trimTail = false;
        std::vector<float> l, r;
        renderOffline (*p, samples, o, l, r);
        double sq = 0; float pk = 0; bool fin = true;
        for (size_t k = 0; k < l.size(); ++k) { fin &= std::isfinite (l[k]) && std::isfinite (r[k]); pk = std::max (pk, std::max (std::fabs (l[k]), std::fabs (r[k]))); sq += (double) l[k] * l[k]; }
        const double rmsDb = 10.0 * std::log10 (sq / (double) l.size() + 1e-20);
        CHECK_MSG (fin, "kit %s not finite", kitPresetInfo (i).name);
        CHECK_MSG (rmsDb > -32.0 && rmsDb < -6.0, "kit %s rms %.1f dB", kitPresetInfo (i).name, rmsDb);
        CHECK_MSG (pk <= 1.0f, "kit %s peak %.2f", kitPresetInfo (i).name, pk);
    }
}
