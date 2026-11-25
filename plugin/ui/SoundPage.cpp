// SOUND tab: engine, voice preset, sample, engine parameters, filter, drive, envelopes, pitch and mix.
#include "DetailPanel.h"
#include "factory/Presets.h"
#include "factory/FactorySamples.h"

namespace ui {

using namespace atm;

namespace {

// Envelope shapes or, for sample engines, the waveform with the played region.
class SoundDisplay : public juce::Component
{
public:
    explicit SoundDisplay (Ctx& c) : ctx (c) {}
    void tick (bool projectChanged) { if (projectChanged) repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        fillPanel (g, r, col::panel(), 3.f);
        const TrackSound& ts = ctx.ts();
        const juce::Colour tc = col::track (ctx.track);
        auto inner = r.reduced (8, 6);
        const bool sampleEngine = ts.engine == Engine::Sampler || ts.engine == Engine::Granular;
        if (sampleEngine)
        {
            const SamplePool::Entry* e = ctx.sess.samples().find (ts.sampleId);
            drawLabel (g, e ? juce::String (e->name).toUpperCase() : "NO SAMPLE  -  DROP AN AUDIO FILE ON THE TRACK", inner.removeFromTop (14), col::dim(), 9.5f, juce::Justification::centredLeft, true);
            auto wf = inner.reduced (0, 2);
            g.setColour (col::well()); g.fillRect (wf);
            if (e == nullptr || e->data == nullptr || e->data->length < 2) return;
            const SampleData& sd = *e->data;
            const float* l = sd.data (0, 0);
            const int n = sd.length, w = wf.getWidth();
            // region
            float start = 0.f, len = 1.f;
            if (ts.engine == Engine::Sampler) { start = ts.params[TP::E0]; len = ts.params[TP::E1] * (1.f - start); }
            else { start = ts.params[TP::E0]; len = 0.f; }
            g.setColour (tc.withAlpha (0.12f));
            g.fillRect ((float) wf.getX() + start * (float) w, (float) wf.getY(), juce::jmax (1.5f, len * (float) w), (float) wf.getHeight());
            g.setColour (tc.withAlpha (0.85f));
            const float mid = (float) wf.getCentreY(), hh = (float) wf.getHeight() * 0.48f;
            for (int x = 0; x < w; ++x)
            {
                const int a = (int) ((int64_t) x * n / w), b = juce::jmax (a + 1, (int) ((int64_t) (x + 1) * n / w));
                float lo = 0.f, hi = 0.f;
                const int stride = juce::jmax (1, (b - a) / 64);
                for (int i = a; i < b && i < n; i += stride) { lo = juce::jmin (lo, l[i]); hi = juce::jmax (hi, l[i]); }
                g.fillRect ((float) (wf.getX() + x), mid - hi * hh, 1.f, juce::jmax (1.f, (hi - lo) * hh));
            }
            // slices
            if (ts.engine == Engine::Sampler && kSamplerModeIsSlice (ts))
            {
                const int slices = commonSlices (ts);
                g.setColour (col::text().withAlpha (0.35f));
                for (int s = 1; s < slices; ++s) g.fillRect ((float) wf.getX() + (float) s / (float) slices * (float) w, (float) wf.getY(), 1.f, (float) wf.getHeight());
            }
            g.setColour (col::text());
            g.fillRect ((float) wf.getX() + start * (float) w, (float) wf.getY(), 1.f, (float) wf.getHeight());
            return;
        }
        // envelopes
        drawLabel (g, "ENVELOPES", inner.removeFromTop (14), col::dim(), 9.5f, juce::Justification::centredLeft, true);
        auto area = inner.reduced (0, 2).toFloat();
        g.setColour (col::well()); g.fillRect (area);
        auto phys = [&] (int tp) { return commonTrackParamDesc (tp).toPhys (ts.params[tp]); };
        auto drawEnv = [&] (float a, float h, float d, float s, float rel, juce::Colour c)
        {
            // log time axis so 1 ms and 10 s are both visible
            auto tx = [&] (float t) { return area.getX() + area.getWidth() * juce::jlimit (0.f, 1.f, std::log10 (1.f + t * 400.f) / std::log10 (1.f + 12.f * 400.f)); };
            juce::Path p;
            p.startNewSubPath (area.getX(), area.getBottom());
            float t = a; p.lineTo (tx (t), area.getY() + 3.f);
            t += h; p.lineTo (tx (t), area.getY() + 3.f);
            const float sy = area.getBottom() - (area.getHeight() - 3.f) * s;
            for (int i = 1; i <= 16; ++i)
            {
                const float u = (float) i / 16.f;
                p.lineTo (tx (t + d * u), sy + (area.getY() + 3.f - sy) * std::exp (-6.9f * u));
            }
            t += d;
            const float hold = 0.25f;
            p.lineTo (tx (t + hold), sy); t += hold;
            for (int i = 1; i <= 16; ++i)
            {
                const float u = (float) i / 16.f;
                p.lineTo (tx (t + rel * u), area.getBottom() + (sy - area.getBottom()) * std::exp (-6.9f * u));
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.4f));
        };
        drawEnv (phys (TP::ModA), 0.f, phys (TP::ModD), ts.params[TP::ModS], phys (TP::ModR), col::mod().withAlpha (0.8f));
        drawEnv (phys (TP::AmpA), phys (TP::AmpH), phys (TP::AmpD), ts.params[TP::AmpS], phys (TP::AmpR), tc);
        drawLabel (g, "AMP", juce::Rectangle<int> ((int) area.getRight() - 64, (int) area.getY() + 2, 28, 10), tc, 8.5f, juce::Justification::centredLeft, true);
        drawLabel (g, "MOD", juce::Rectangle<int> ((int) area.getRight() - 34, (int) area.getY() + 2, 28, 10), col::mod(), 8.5f, juce::Justification::centredLeft, true);
    }

private:
    static bool kSamplerModeIsSlice (const TrackSound& ts) { return engineParamDescs (Engine::Sampler)[5].toIndex (ts.params[TP::E5]) == 2; }
    static int commonSlices (const TrackSound& ts) { return (int) engineParamDescs (Engine::Sampler)[6].toPhys (ts.params[TP::E6]); }
    Ctx& ctx;
};

class SoundPage : public Page
{
public:
    explicit SoundPage (Ctx& c) : Page (c), display (c)
    {
        enginePicker.ctx = &ctx; enginePicker.caption = "ENGINE"; enginePicker.help = "Sound engine of this track";
        enginePicker.items = [] { juce::StringArray a; for (int i = 0; i < Engine::count; ++i) a.add (juce::String (engineName (i)).toUpperCase()); return a; };
        enginePicker.get = [this] { return (int) ctx.ts().engine; };
        enginePicker.set = [this] (int v)
        {
            TrackSound ts = ctx.ts();
            if (ts.engine == v) return;
            ts.engine = (uint8_t) v; setEngineDefaults (ts, v);
            std::snprintf (ts.name, sizeof (ts.name), "%s", engineName (v));
            ctx.sess.setTrackSound (ctx.track, ts);
        };
        presetPicker.ctx = &ctx; presetPicker.caption = "VOICE PRESET"; presetPicker.help = "Factory voice presets (replaces this track's sound and its modulation routings)";
        presetPicker.items = [this] { return presetItems(); };
        presetPicker.get = [this] { return currentPresetItem(); };
        presetPicker.set = [this] (int item)
        {
            if (! juce::isPositiveAndBelow (item, (int) itemToPreset.size()) || itemToPreset[(size_t) item] < 0) return;
            VoicePresetData v;
            if (loadVoicePreset (itemToPreset[(size_t) item], v, &ctx.sess.samples())) ctx.sess.applyVoice (ctx.track, v);
        };
        samplePicker.ctx = &ctx; samplePicker.caption = "SAMPLE"; samplePicker.help = "Factory samples, or load an audio file (you can also drop a file on the track)";
        samplePicker.items = [this] { return sampleItems(); };
        samplePicker.get = [this] { return currentSampleItem(); };
        samplePicker.set = [this] (int item) { chooseSample (item); };
        polyBtn.ctx = &ctx; polyBtn.help = "Mono: each note cuts the previous one (tight drums, acid lines). Poly: up to three notes ring over each other.";
        polyBtn.onClick = [this] { TrackSound ts = ctx.ts(); ts.poly = ! ts.poly; ctx.sess.setTrackSound (ctx.track, ts); };
        auditionBtn.ctx = &ctx; auditionBtn.help = "Audition the sound (also: A key)";
        auditionBtn.onPressChange = [this] (bool d) { ctx.sess.previewNote (ctx.track, 0, 0.85f, d); };
        for (auto* c : std::initializer_list<juce::Component*> { &enginePicker, &presetPicker, &samplePicker, &polyBtn, &auditionBtn, &display }) addAndMakeVisible (c);
    }

    void rebuild() override
    {
        groups.clear();
        const juce::Colour tc = col::track (ctx.track);
        auto group = [&] (const char* title, std::initializer_list<int> ids, int rows)
        {
            auto g = std::make_unique<ParamGroup> (ctx, title);
            g->accent = tc; g->rows = rows;
            for (int id : ids) g->add (ParamRef::T (id));
            addAndMakeVisible (*g);
            groups.push_back (std::move (g));
        };
        group (engineName (ctx.ts().engine), { TP::E0, TP::E1, TP::E2, TP::E3, TP::E4, TP::E5, TP::E6, TP::E7, TP::E8, TP::E9, TP::E10, TP::E11, TP::E12, TP::E13, TP::E14, TP::E15 }, 2);
        group ("Filter", { TP::FltType, TP::FltCut, TP::FltRes, TP::FltEnv, TP::FltKey, TP::Accent }, 2);
        group ("Drive", { TP::DrvAmt, TP::DrvType, TP::DrvPos }, 2);
        group ("Pitch", { TP::Tune, TP::Fine, TP::Glide }, 2);
        group ("Amp Env", { TP::AmpA, TP::AmpH, TP::AmpD, TP::AmpS, TP::AmpR, TP::AmpVel }, 2);
        group ("Mod Env", { TP::ModA, TP::ModD, TP::ModS, TP::ModR }, 2);
        group ("Mix", { TP::Level, TP::Pan, TP::SendA, TP::SendB, TP::GlitchSens }, 2);
        samplePicker.setVisible (ctx.ts().engine == Engine::Sampler || ctx.ts().engine == Engine::Granular);
        enginePicker.colour = presetPicker.colour = tc;
        polyBtn.colour = auditionBtn.colour = tc;
        resized();
        tick (true);
    }

    void tick (bool projectChanged) override
    {
        for (auto& g : groups) g->sync();
        if (projectChanged)
        {
            enginePicker.sync(); presetPicker.sync(); samplePicker.sync();
            polyBtn.setText (ctx.ts().poly ? "POLY" : "MONO");
            polyBtn.setLit (ctx.ts().poly != 0);
            display.tick (true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 6).withTrimmedRight (6);
        const int rowH = (r.getHeight() - 6) / 2;
        // voice block
        auto vb = r.removeFromLeft (150);
        enginePicker.setBounds (vb.getX(), vb.getY(), 146, 34);
        presetPicker.setBounds (vb.getX(), vb.getY() + 38, 146, 34);
        samplePicker.setBounds (vb.getX(), vb.getY() + 76, 146, 34);
        polyBtn.setBounds (vb.getX(), vb.getBottom() - 28, 70, 24);
        auditionBtn.setBounds (vb.getX() + 74, vb.getBottom() - 28, 72, 24);
        // groups flow in two rows
        int x = r.getX() + 4, y = r.getY(), row = 0;
        int lastRight[2] = { x, x };
        for (size_t i = 0; i < groups.size(); ++i)
        {
            const int w = groups[i]->idealWidth (2);
            if (i == 4) { row = 1; x = r.getX() + 4; y = r.getY() + rowH + 6; }
            groups[i]->setBounds (x, y, w, rowH);
            x += w + 6;
            lastRight[row] = x;
        }
        display.setBounds (lastRight[1], r.getY() + rowH + 6, juce::jmax (60, r.getRight() - lastRight[1]), rowH);
    }

    void paint (juce::Graphics& g) override { g.fillAll (col::bg()); }

private:
    juce::StringArray presetItems()
    {
        juce::StringArray a;
        itemToPreset.clear();
        juce::String lastCat;
        for (int i = 0; i < numVoicePresets(); ++i)
        {
            const VoicePresetInfo& info = voicePresetInfo (i);
            if (lastCat != info.category) { lastCat = info.category; a.add ("--" + lastCat); itemToPreset.push_back (-1); }
            a.add (info.name); itemToPreset.push_back (i);
        }
        return a;
    }
    int currentPresetItem()
    {
        if (itemToPreset.empty()) presetItems();
        const int idx = findVoicePreset (ctx.ts().name);
        for (size_t i = 0; i < itemToPreset.size(); ++i) if (itemToPreset[i] == idx && idx >= 0) return (int) i;
        return -1;
    }
    juce::StringArray sampleItems()
    {
        juce::StringArray a;
        itemToSample.clear();
        juce::String lastCat;
        for (int i = 0; i < numFactorySamples(); ++i)
        {
            const FactorySampleInfo& info = factorySampleInfo (i);
            if (lastCat != info.category) { lastCat = info.category; a.add ("--" + lastCat); itemToSample.push_back (-1); }
            a.add (info.name); itemToSample.push_back (i);
        }
        a.add ("--File"); itemToSample.push_back (-1);
        a.add ("Load audio file..."); itemToSample.push_back (-2);
        return a;
    }
    int currentSampleItem()
    {
        if (itemToSample.empty()) sampleItems();
        const SamplePool::Entry* e = ctx.sess.samples().find (ctx.ts().sampleId);
        if (e == nullptr || e->kind != SamplePool::Factory) return -1;
        for (size_t i = 0; i < itemToSample.size(); ++i)
            if (itemToSample[i] >= 0 && e->name == factorySampleInfo (itemToSample[i]).name) return (int) i;
        return -1;
    }
    void chooseSample (int item)
    {
        if (! juce::isPositiveAndBelow (item, (int) itemToSample.size())) return;
        const int s = itemToSample[(size_t) item];
        if (s >= 0)
        {
            const int id = ctx.sess.samples().addFactory (factorySampleInfo (s).name);
            if (id != 0) ctx.sess.setTrackSample (ctx.track, id);
        }
        else if (s == -2)
        {
            chooser = std::make_unique<juce::FileChooser> ("Load sample", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            const int track = ctx.track;
            juce::Component::SafePointer<SoundPage> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, track] (const juce::FileChooser& fc)
            {
                if (safe != nullptr && fc.getResult() != juce::File()) safe->ctx.proc.loadSampleAsync (track, fc.getResult());
            });
        }
    }

    Picker enginePicker, presetPicker, samplePicker;
    Btn polyBtn { "MONO" }, auditionBtn { "AUDITION" };
    SoundDisplay display;
    std::vector<std::unique_ptr<ParamGroup>> groups;
    std::vector<int> itemToPreset, itemToSample;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace

std::unique_ptr<Page> makeSoundPage (Ctx& c) { return std::make_unique<SoundPage> (c); }

} // namespace ui
