// SEQ / GEN tab: track timing (length, speed, direction, swing, groove, polymeter),
// the generators (each with seed, seed lock and undo through the global history),
// and the global feel controls (swing, evolve, fill/break intensity).
#include "DetailPanel.h"
#include "gen/Generators.h"

namespace ui {

using namespace atm;

namespace {

const char* const kGenHelp[Gen::count] = {
    "Euclidean: spreads N hits as evenly as possible over the track length",
    "Cellular automaton: a 1-D rule evolved from a seeded row; neighbours set the accents",
    "Probability density: hits placed by metric weight, syncopation and clustering, with seeded ratchets",
    "Markov: re-walks the existing pattern, following what tends to come after what",
    "Mutate: varies the existing pattern by an amount, weighted between rhythm, pitch and detail",
};
const char* const kGenParamNames[Gen::count][kGenParams] = {
    { "Pulses", "Rotate", "Accents", "Vel Var" },
    { "Rule", "Seed Dens", "Gens", "Vel Nbr" },
    { "Density", "Syncop", "Cluster", "Ratchets" },
    { "Adherence", "Pitch Free", "Vel Free", "-" },
    { "Amount", "Rhythm", "Pitch", "Detail" },
};

class SeqPage : public Page
{
public:
    explicit SeqPage (Ctx& c) : Page (c)
    {
        // ---- track timing
        auto box = [this] (NumberBox& b, const char* cap, double lo, double hi, double def, const char* help)
        {
            b.ctx = &ctx; b.caption = cap; b.minV = lo; b.maxV = hi; b.defaultV = def; b.help = help;
            b.onGestureStart = [this] { ctx.sess.beginTransaction ("Track timing"); };
            b.onGestureEnd = [this] { ctx.sess.endTransaction(); };
            addAndMakeVisible (b);
        };
        box (lenBox, "LENGTH", 1, kMaxSteps, 16, "Track length in steps (1..64). Different lengths run against each other polymetrically.");
        lenBox.pixelsPerStep = 5;
        lenBox.get = [this] { return (double) ctx.tp().length; };
        lenBox.set = [this] (double v) { editTp ([v] (TrackPattern& p) { p.length = (uint8_t) v; }); };
        box (swingBox, "SWING", 24, 80, 24, "Track swing in percent (50 = straight, below 50 pushes, above drags). Minimum = follow the global swing.");
        swingBox.get = [this] { return ctx.tp().swing < 0.f ? 24.0 : (double) std::lround (ctx.tp().swing * 100.f); };
        swingBox.set = [this] (double v) { editTp ([v] (TrackPattern& p) { p.swing = v <= 24.5 ? -1.f : (float) v / 100.f; }); };
        swingBox.format = [] (double v) { return v <= 24.5 ? juce::String ("GLOBAL") : juce::String ((int) v) + "%"; };
        box (baseBox, "BASE NOTE", 0, 127, 60, "MIDI note played by pitch 0 on this track");
        baseBox.get = [this] { return (double) ctx.ts().baseNote; };
        baseBox.set = [this] (double v) { TrackSound s = ctx.ts(); s.baseNote = (uint8_t) v; ctx.sess.setTrackSound (ctx.track, s); };
        baseBox.format = [] (double v) { static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String (n[(int) v % 12]) + juce::String ((int) v / 12 - 1); };
        box (seedBox, "PAT SEED", 0, 99999, 1, "Seed for this track's probabilities, random directions and macro variations");
        seedBox.pixelsPerStep = 2;
        seedBox.get = [this] { return (double) (ctx.tp().seed % 100000u); };
        seedBox.set = [this] (double v) { editTp ([v] (TrackPattern& p) { p.seed = (uint32_t) v; }); };

        auto pick = [this] (Picker& p, const char* cap, const char* help) { p.ctx = &ctx; p.caption = cap; p.help = help; addAndMakeVisible (p); };
        pick (speedPicker, "SPEED", "Step length: straight, dotted, triplet and odd tuplets (5:4, 7:4, 9:8) for polyrhythms");
        speedPicker.items = [] { juce::StringArray a; for (int i = 0; i < kNumSpeeds; ++i) a.add (speedDef (i).name); return a; };
        speedPicker.get = [this] { return speedIndex (ctx.tp().spNum, ctx.tp().spDen); };
        speedPicker.set = [this] (int v) { Session::Transaction tx (ctx.sess, "Speed"); editTp ([v] (TrackPattern& p) { p.spNum = speedDef (v).num; p.spDen = speedDef (v).den; }); };
        pick (dirPicker, "DIRECTION", "Playback direction: forward, reverse, ping-pong, random (seeded) or a drunk walk");
        dirPicker.items = [] { juce::StringArray a; for (int i = 0; i < Dir::count; ++i) a.add (juce::String (dirName (i)).toUpperCase()); return a; };
        dirPicker.get = [this] { return (int) ctx.tp().dir; };
        dirPicker.set = [this] (int v) { Session::Transaction tx (ctx.sess, "Direction"); editTp ([v] (TrackPattern& p) { p.dir = (uint8_t) v; }); };
        pick (groovePicker, "GROOVE", "Groove template: per-16th timing and accent pattern");
        groovePicker.items = [] { juce::StringArray a; for (int i = 0; i < kNumGrooves; ++i) a.add (juce::String (grooveDef (i).name).toUpperCase()); return a; };
        groovePicker.get = [this] { return (int) ctx.tp().groove; };
        groovePicker.set = [this] (int v) { Session::Transaction tx (ctx.sess, "Groove"); editTp ([v] (TrackPattern& p) { p.groove = (uint8_t) v; if (v > 0 && p.grooveAmt <= 0.f) p.grooveAmt = 0.6f; }); };
        pick (rolePicker, "ROLE", "What this track is for: guides New Idea, Evolve and the macros (melodic roles keep their notes)");
        rolePicker.items = [] { juce::StringArray a; for (int i = 0; i < Role::count; ++i) a.add (juce::String (roleName (i)).toUpperCase()); return a; };
        rolePicker.get = [this] { return (int) ctx.ts().role; };
        rolePicker.set = [this] (int v) { TrackSound s = ctx.ts(); s.role = (uint8_t) v; ctx.sess.setTrackSound (ctx.track, s); };

        grooveAmt.ctx = &ctx; grooveAmt.caption = "Groove Amt"; grooveAmt.help = "How much of the groove template is applied";
        grooveAmt.get = [this] { return ctx.tp().grooveAmt; };
        grooveAmt.set = [this] (float v) { editTp ([v] (TrackPattern& p) { p.grooveAmt = v; }); };
        grooveAmt.format = [] (float v) { return juce::String ((int) std::lround (v * 100.f)) + "%"; };
        grooveAmt.onGestureStart = [this] { ctx.sess.beginTransaction ("Groove amount"); };
        grooveAmt.onGestureEnd = [this] { ctx.sess.endTransaction(); };
        addAndMakeVisible (grooveAmt);

        auto toggle = [this] (Btn& b, const char* help) { b.ctx = &ctx; b.help = help; addAndMakeVisible (b); };
        toggle (freeBtn, "Free run: the track never resets with the pattern, so odd lengths keep drifting against the bar (true polymeter)");
        freeBtn.onClick = [this] { Session::Transaction tx (ctx.sess, "Free run"); editTp ([] (TrackPattern& p) { p.freeRun = ! p.freeRun; }); };
        toggle (scaleBtn, "Quantise this track's pitches to the project scale (step pitch = scale degree)");
        scaleBtn.onClick = [this] { TrackSound s = ctx.ts(); s.scaleOn = ! s.scaleOn; ctx.sess.setTrackSound (ctx.track, s); };
        toggle (rotL, "Rotate the pattern one step left (with its locks)");
        rotL.onClick = [this] { Session::Transaction tx (ctx.sess, "Rotate"); editTp ([] (TrackPattern& p) { rotatePattern (p, -1); }); };
        toggle (rotR, "Rotate the pattern one step right (with its locks)");
        rotR.onClick = [this] { Session::Transaction tx (ctx.sess, "Rotate"); editTp ([] (TrackPattern& p) { rotatePattern (p, 1); }); };

        // ---- generator
        pick (genPicker, "GENERATOR", "Pattern generator for this track");
        genPicker.items = [] { juce::StringArray a; for (int i = 0; i < Gen::count; ++i) a.add (juce::String (genName (i)).toUpperCase()); return a; };
        genPicker.get = [this] { return (int) ctx.tp().gen.type; };
        genPicker.set = [this] (int v) { Session::Transaction tx (ctx.sess, "Generator"); editTp ([v] (TrackPattern& p) { p.gen.type = (uint8_t) v; }); rebuildGenKnobs(); };
        for (int k = 0; k < kGenParams; ++k)
        {
            auto& kn = genKnobs[k];
            kn.ctx = &ctx;
            kn.get = [this, k] { return ctx.tp().gen.p[ctx.tp().gen.type][k]; };
            kn.set = [this, k] (float v)
            {
                TrackPattern p = ctx.tp();
                p.gen.p[p.gen.type][k] = v;
                if (liveGen) runGenerator (p, ctx.ts().role);
                ctx.sess.setTrackPattern (ctx.pattern(), ctx.track, p);
            };
            kn.onGestureStart = [this] { ctx.sess.beginTransaction ("Generator"); };
            kn.onGestureEnd = [this] { ctx.sess.endTransaction(); };
            kn.colour = col::accent();
            addAndMakeVisible (kn);
        }
        box (genSeed, "GEN SEED", 0, 99999, 1, "Generator seed: the same seed and settings always give the same pattern");
        genSeed.pixelsPerStep = 2; genSeed.colour = col::accent();
        genSeed.get = [this] { return (double) (ctx.tp().gen.seed % 100000u); };
        genSeed.set = [this] (double v)
        {
            TrackPattern p = ctx.tp(); p.gen.seed = (uint32_t) v;
            if (liveGen) runGenerator (p, ctx.ts().role);
            ctx.sess.setTrackPattern (ctx.pattern(), ctx.track, p);
        };
        toggle (genBtn, "Generate with the current seed and settings (G re-rolls the seed unless it is locked)");
        genBtn.text = "GENERATE"; genBtn.colour = col::accent();
        genBtn.onClick = [this] { generate (false); };
        toggle (rollBtn, "New seed, then generate (respects the seed lock)");
        rollBtn.text = "RE-ROLL"; rollBtn.colour = col::accent();
        rollBtn.onClick = [this] { generate (true); };
        toggle (seedLockBtn, "Lock the generator seed: re-rolls keep it, so you can tweak the settings around a result you like");
        seedLockBtn.text = "LOCK SEED"; seedLockBtn.colour = col::lock();
        seedLockBtn.onClick = [this] { Session::Transaction tx (ctx.sess, "Seed lock"); editTp ([] (TrackPattern& p) { p.gen.seedLocked = ! p.gen.seedLocked; }); };
        toggle (liveBtn, "Live: regenerate while you turn the generator knobs (each gesture is one undo step)");
        liveBtn.text = "LIVE"; liveBtn.colour = col::accent();
        liveBtn.onClick = [this] { liveGen = ! liveGen; liveBtn.setLit (liveGen); };
        toggle (allBtn, "Generate every unlocked track with its own generator and a new seed");
        allBtn.text = "ALL TRACKS"; allBtn.colour = col::accent();
        allBtn.onClick = [this] { generateAll(); };
        rebuildGenKnobs();

        // ---- global
        global = std::make_unique<ParamGroup> (ctx, "Feel and performance");
        global->rows = 2; global->accent = col::accent();
        for (int gp : { GP::Swing, GP::EvolveRate, GP::EvolveAmt, GP::FillAmt, GP::BreakAmt, GP::Tempo }) global->add (ParamRef::G (gp));
        addAndMakeVisible (*global);
    }

    void rebuild() override
    {
        const juce::Colour tc = col::track (ctx.track);
        for (auto* p : { &speedPicker, &dirPicker, &groovePicker, &rolePicker }) p->colour = tc;
        grooveAmt.colour = tc;
        freeBtn.colour = scaleBtn.colour = rotL.colour = rotR.colour = tc;
        rebuildGenKnobs();
        tick (true);
    }

    void tick (bool changed) override
    {
        global->sync();
        if (! changed) return;
        for (auto* b : { &lenBox, &swingBox, &baseBox, &seedBox, &genSeed }) b->sync();
        for (auto* p : { &speedPicker, &dirPicker, &groovePicker, &rolePicker, &genPicker }) p->sync();
        grooveAmt.sync();
        for (auto& k : genKnobs) k.sync();
        freeBtn.setLit (ctx.tp().freeRun != 0);
        scaleBtn.setLit (ctx.ts().scaleOn != 0);
        seedLockBtn.setLit (ctx.tp().gen.seedLocked != 0);
        if (shownGen != ctx.tp().gen.type) rebuildGenKnobs();
        repaint();
    }

    void resized() override
    {
        // left block: track timing
        int x = 12, y = 30;
        auto row = [&] (std::initializer_list<juce::Component*> cs, int w, int h)
        {
            int xx = x;
            for (auto* c : cs) { c->setBounds (xx, y, w, h); xx += w + 6; }
            y += h + 8;
        };
        row ({ &lenBox, &speedPicker, &dirPicker, &swingBox }, 82, 44);
        row ({ &groovePicker, &rolePicker, &baseBox, &seedBox }, 82, 44);
        grooveAmt.setBounds (x, y, 64, 64);
        freeBtn.setBounds (x + 72, y + 4, 82, 24); scaleBtn.setBounds (x + 160, y + 4, 82, 24);
        rotL.setBounds (x + 72, y + 34, 82, 24); rotR.setBounds (x + 160, y + 34, 82, 24);
        // middle block: generator
        x = 392; y = 30;
        genPicker.setBounds (x, y, 132, 44);
        genSeed.setBounds (x + 138, y, 70, 44);
        seedLockBtn.setBounds (x + 214, y + 4, 92, 24);
        liveBtn.setBounds (x + 214, y + 32, 92, 14);
        int kx = x;
        for (auto& k : genKnobs) { k.setBounds (kx, y + 54, 70, 72); kx += 76; }
        genBtn.setBounds (x, y + 136, 104, 28);
        rollBtn.setBounds (x + 110, y + 136, 96, 28);
        allBtn.setBounds (x + 212, y + 136, 96, 28);
        // right block: global
        global->setBounds (740, 24, juce::jmin (getWidth() - 752, global->idealWidth (2)), 150);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg());
        drawLabel (g, "TRACK " + juce::String (ctx.track + 1) + " TIMING", { 12, 8, 300, 16 }, col::track (ctx.track), 10.f, juce::Justification::centredLeft, true);
        drawLabel (g, "GENERATOR", { 392, 8, 300, 16 }, col::accent(), 10.f, juce::Justification::centredLeft, true);
        drawWrapped (g, kGenHelp[ctx.tp().gen.type], { 392, 232, 340, 30 }, col::dim(), 9.f);
        g.setColour (col::line());
        g.fillRect (380, 12, 1, getHeight() - 24);
        g.fillRect (728, 12, 1, getHeight() - 24);
        // the generated row as a preview of the pattern's on/off state
        const TrackPattern& tp = ctx.tp();
        const int len = tp.length;
        const float w = 330.f / (float) juce::jmax (16, len);
        for (int i = 0; i < len; ++i)
        {
            g.setColour (tp.steps[i].on ? col::track (ctx.track).withAlpha (0.4f + 0.6f * (float) tp.steps[i].vel / 127.f) : col::panelHi());
            g.fillRect (392.f + w * (float) i, 214.f, w - 1.f, 10.f);
        }
        drawWrapped (g, "Evolve mutates unlocked tracks every N bars while playing. Fill and Break push density, ratchets and glitch until the next bar line.",
                   { 740, 182, getWidth() - 752, 40 }, col::faint(), 9.f);
    }

private:
    void editTp (const std::function<void (TrackPattern&)>& fn)
    {
        TrackPattern p = ctx.tp();
        fn (p);
        ctx.sess.setTrackPattern (ctx.pattern(), ctx.track, p);
    }
    void rebuildGenKnobs()
    {
        shownGen = ctx.tp().gen.type;
        for (int k = 0; k < kGenParams; ++k)
        {
            auto& kn = genKnobs[k];
            kn.caption = kGenParamNames[shownGen][k];
            kn.setVisible (std::strcmp (kGenParamNames[shownGen][k], "-") != 0);
            kn.steps = 0;
            if (shownGen == Gen::Euclid && k < 2) { const int L = ctx.tp().length; kn.steps = L + 1; kn.format = [L] (float v) { return juce::String ((int) std::lround (v * (float) L)); }; }
            else if (shownGen == Gen::Cellular && k == 0) { kn.steps = 256; kn.format = [] (float v) { return "Rule " + juce::String ((int) std::lround (v * 255.f)); }; }
            else if (shownGen == Gen::Cellular && k == 2) { kn.steps = 33; kn.format = [] (float v) { return juce::String ((int) std::lround (v * 32.f)); }; }
            else kn.format = [] (float v) { return juce::String ((int) std::lround (v * 100.f)) + "%"; };
            kn.help = juce::String (kGenHelp[shownGen]);
            kn.repaint();
        }
    }
    void generate (bool newSeed)
    {
        TrackPattern p = ctx.tp();
        if (newSeed && ! p.gen.seedLocked) p.gen.seed = (uint32_t) juce::Random::getSystemRandom().nextInt (99999);
        runGenerator (p, ctx.ts().role);
        Session::Transaction tx (ctx.sess, "Generate");
        ctx.sess.setTrackPattern (ctx.pattern(), ctx.track, p);
        ctx.status = juce::String ("Generated ") + genName (p.gen.type) + "  seed " + juce::String ((int) (p.gen.seed % 100000u)) + (p.gen.seedLocked ? "  (locked)" : "");
    }
    void generateAll()
    {
        Session::Transaction tx (ctx.sess, "Generate all");
        for (int t = 0; t < kTracks; ++t)
        {
            if (ctx.ts (t).locked) continue;
            TrackPattern p = ctx.tp (t);
            if (! p.gen.seedLocked) p.gen.seed = (uint32_t) juce::Random::getSystemRandom().nextInt (99999);
            runGenerator (p, ctx.ts (t).role);
            ctx.sess.setTrackPattern (ctx.pattern(), t, p);
        }
        ctx.status = "Generated all unlocked tracks";
    }

    NumberBox lenBox, swingBox, baseBox, seedBox, genSeed;
    Picker speedPicker, dirPicker, groovePicker, rolePicker, genPicker;
    ValueKnob grooveAmt, genKnobs[kGenParams];
    Btn freeBtn { "FREE RUN" }, scaleBtn { "SCALE" }, rotL { "<< ROT" }, rotR { "ROT >>" };
    Btn genBtn, rollBtn, seedLockBtn, liveBtn, allBtn;
    std::unique_ptr<ParamGroup> global;
    int shownGen = -1;
    bool liveGen = false;
};

} // namespace

std::unique_ptr<Page> makeSeqPage (Ctx& c) { return std::make_unique<SeqPage> (c); }

} // namespace ui
