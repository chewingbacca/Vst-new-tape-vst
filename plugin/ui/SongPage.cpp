// SONG / SETUP tab: pattern chain and song mode, tuning (built-in scales and
// Scala import), MIDI routing (pattern-select channel, per-track MIDI out and
// direct outputs).
#include "DetailPanel.h"
#include "seq/Scale.h"

namespace ui {

using namespace atm;

namespace {

class ChainView : public juce::Component
{
public:
    explicit ChainView (Ctx& c) : ctx (c) {}
    void paint (juce::Graphics& g) override
    {
        fillPanel (g, getLocalBounds(), col::panel(), 3.f);
        const Song& s = ctx.proj().song;
        const int active = ctx.eng.shared.songEntry.load (std::memory_order_relaxed);
        const int cw = 46, ch = 38;
        const int perRow = juce::jmax (1, (getWidth() - 8) / cw);
        for (int i = 0; i <= s.length && i < kChainLen; ++i)
        {
            juce::Rectangle<int> r (4 + (i % perRow) * cw, 4 + (i / perRow) * (ch + 4), cw - 4, ch);
            if (i == s.length)
            {
                strokePanel (g, r, col::line(), 2.f);
                drawLabel (g, "+", r, col::dim(), 14.f);
                continue;
            }
            const SongEntry& e = s.chain[i];
            const bool on = s.songMode && i == active;
            fillPanel (g, r, on ? col::accent() : col::panelHi(), 2.f);
            drawLabel (g, ctx.proj().patterns[e.pattern % kPatterns].name, r.withTrimmedBottom (14), on ? col::bg() : col::text(), 11.f, juce::Justification::centred, true);
            drawLabel (g, "x" + juce::String ((int) e.repeats), r.withTrimmedTop (22), on ? col::bg() : col::dim(), 9.f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const Song& s = ctx.proj().song;
        const int cw = 46, ch = 38;
        const int perRow = juce::jmax (1, (getWidth() - 8) / cw);
        const int i = ((e.y - 4) / (ch + 4)) * perRow + (e.x - 4) / cw;
        if (i < 0 || i > s.length || i >= kChainLen) return;
        juce::PopupMenu m, pats, reps;
        for (int p = 0; p < kPatterns; ++p) pats.addItem (100 + p, ctx.proj().patterns[p].name, true, i < s.length && s.chain[i].pattern == p);
        for (int r : { 1, 2, 3, 4, 6, 8, 12, 16 }) reps.addItem (200 + r, "x" + juce::String (r), i < s.length, i < s.length && s.chain[i].repeats == r);
        m.addSubMenu (i == s.length ? "Append pattern" : "Pattern", pats);
        if (i < s.length)
        {
            m.addSubMenu ("Repeats", reps);
            m.addItem (1, "Insert copy before");
            m.addItem (2, "Remove");
        }
        juce::Component::SafePointer<ChainView> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, i] (int r)
        {
            if (safe == nullptr || r == 0) return;
            Song s = safe->ctx.proj().song;
            if (r >= 100 && r < 100 + kPatterns)
            {
                if (i == s.length) { s.chain[i].pattern = (uint8_t) (r - 100); s.chain[i].repeats = 1; ++s.length; }
                else s.chain[i].pattern = (uint8_t) (r - 100);
            }
            else if (r >= 200) s.chain[i].repeats = (uint8_t) (r - 200);
            else if (r == 1 && s.length < kChainLen) { for (int k = s.length; k > i; --k) s.chain[k] = s.chain[k - 1]; ++s.length; }
            else if (r == 2) { for (int k = i; k < s.length - 1; ++k) s.chain[k] = s.chain[k + 1]; --s.length; }
            safe->ctx.sess.setSong (s);
        });
    }
    void mouseMove (const juce::MouseEvent&) override { ctx.hoverHelp = "Song chain: click an entry to change its pattern or repeats, click + to append. Song mode plays the chain in a loop."; }
    void mouseExit (const juce::MouseEvent&) override { ctx.hoverHelp = {}; }
private:
    Ctx& ctx;
};

class MidiRouting : public juce::Component
{
public:
    explicit MidiRouting (Ctx& c) : ctx (c) {}
    void paint (juce::Graphics& g) override
    {
        fillPanel (g, getLocalBounds(), col::panel(), 3.f);
        drawLabel (g, "TRACK", { 6, 4, 60, 14 }, col::dim(), 9.f, juce::Justification::centredLeft, true);
        drawLabel (g, "MIDI OUT", { 70, 4, 70, 14 }, col::dim(), 9.f, juce::Justification::centred, true);
        drawLabel (g, "CH", { 144, 4, 40, 14 }, col::dim(), 9.f, juce::Justification::centred, true);
        drawLabel (g, "DIRECT OUT", { 188, 4, 90, 14 }, col::dim(), 9.f, juce::Justification::centred, true);
        for (int t = 0; t < kTracks; ++t)
        {
            const TrackSound& ts = ctx.ts (t);
            const int y = 22 + t * 22;
            drawLabel (g, juce::String (t + 1) + " " + juce::String (ts.name).toUpperCase().substring (0, 8), { 6, y, 64, 18 }, col::track (t), 9.5f, juce::Justification::centredLeft, true);
            auto box = [&] (juce::Rectangle<int> r, const juce::String& txt, bool on, juce::Colour c)
            {
                fillPanel (g, r, on ? c : col::well(), 2.f);
                strokePanel (g, r, on ? c : col::line(), 2.f);
                drawLabel (g, txt, r, on ? col::bg() : col::dim(), 9.f, juce::Justification::centred, true);
            };
            box ({ 84, y, 42, 18 }, ts.midiOut ? "ON" : "OFF", ts.midiOut != 0, col::track (t));
            box ({ 148, y, 32, 18 }, juce::String ((int) ts.midiCh), false, col::text());
            box ({ 206, y, 54, 18 }, ts.directOut ? "ONLY" : "+MIX", ts.directOut != 0, col::lock());
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int t = (e.y - 22) / 22;
        if (t < 0 || t >= kTracks) return;
        TrackSound s = ctx.ts (t);
        if (e.x >= 84 && e.x < 126) s.midiOut = ! s.midiOut;
        else if (e.x >= 148 && e.x < 180) s.midiCh = (uint8_t) (e.mods.isPopupMenu() ? (s.midiCh + 14) % 16 + 1 : s.midiCh % 16 + 1);
        else if (e.x >= 206 && e.x < 260) s.directOut = ! s.directOut;
        else return;
        ctx.sess.setTrackSound (t, s);
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        if (e.x >= 84 && e.x < 126) ctx.hoverHelp = "Send this track's notes to the plugin's MIDI output (drive other instruments from the sequencer)";
        else if (e.x >= 148 && e.x < 180) ctx.hoverHelp = "MIDI channel for this track's output and input (click: next, right-click: previous)";
        else if (e.x >= 206 && e.x < 260) ctx.hoverHelp = "Track output bus: +MIX = also in the main mix, ONLY = only on its own bus (enable the bus in your host)";
        else ctx.hoverHelp = {};
    }
    void mouseExit (const juce::MouseEvent&) override { ctx.hoverHelp = {}; }
private:
    Ctx& ctx;
};

class SongPage : public Page
{
public:
    explicit SongPage (Ctx& c) : Page (c), chain (c), midi (c)
    {
        addAndMakeVisible (chain); addAndMakeVisible (midi);
        songBtn.ctx = &ctx; songBtn.help = "Song mode: play the chain instead of the selected pattern";
        songBtn.onClick = [this] { Song s = ctx.proj().song; s.songMode = ! s.songMode; ctx.sess.setSong (s); };
        clearBtn.ctx = &ctx; clearBtn.help = "Remove all chain entries"; clearBtn.colour = col::warn();
        clearBtn.onClick = [this] { Song s = ctx.proj().song; s.length = 0; ctx.sess.setSong (s); };
        sclBtn.ctx = &ctx; sclBtn.help = "Load a Scala (.scl) tuning: step pitches become degrees of that scale on scale-quantised tracks (you can also drop a .scl file on the window)";
        sclBtn.onClick = [this] { loadScala(); };
        for (auto* b : { &songBtn, &clearBtn, &sclBtn }) addAndMakeVisible (b);

        rootPicker.ctx = &ctx; rootPicker.caption = "ROOT"; rootPicker.help = "Tonic of the scale";
        rootPicker.items = [] { return juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; };
        rootPicker.get = [this] { return (int) ctx.proj().kit.tuning.root; };
        rootPicker.set = [this] (int v) { Tuning t = ctx.proj().kit.tuning; t.root = (uint8_t) v; ctx.sess.setTuning (t); };
        scalePicker.ctx = &ctx; scalePicker.caption = "SCALE"; scalePicker.help = "Scale used by scale-quantised tracks";
        scalePicker.items = [this] { juce::StringArray a; for (int i = 0; i < kNumScales; ++i) a.add (juce::String (scaleDef (i).name).toUpperCase());
                                     if (ctx.proj().kit.tuning.scale == 255) a.add (juce::String (ctx.proj().kit.tuning.name).toUpperCase()); return a; };
        scalePicker.get = [this] { return ctx.proj().kit.tuning.scale == 255 ? kNumScales : (int) ctx.proj().kit.tuning.scale; };
        scalePicker.set = [this] (int v)
        {
            if (v >= kNumScales) return;
            Tuning t = ctx.proj().kit.tuning; t.scale = (uint8_t) v;
            std::snprintf (t.name, sizeof (t.name), "%s", scaleDef (v).name);
            ctx.sess.setTuning (t);
        };
        patChBox.ctx = &ctx; patChBox.caption = "PATTERN CH"; patChBox.minV = 0; patChBox.maxV = 16; patChBox.defaultV = 16; patChBox.pixelsPerStep = 8;
        patChBox.help = "MIDI channel whose notes switch patterns (C1 = A1 ... ) and hold Fill (C3) / Break (D3). 0 = off";
        patChBox.get = [this] { return (double) ctx.proj().midiPatternCh; };
        patChBox.set = [this] (double v) { ctx.sess.setProjectMeta (nullptr, ctx.proj().seed, ctx.proj().style, (int) v); };
        patChBox.format = [] (double v) { return v < 0.5 ? juce::String ("OFF") : juce::String ((int) v); };
        patChBox.onGestureStart = [this] { ctx.sess.beginTransaction ("Pattern channel"); };
        patChBox.onGestureEnd = [this] { ctx.sess.endTransaction(); };
        for (juce::Component* p : std::initializer_list<juce::Component*> { &rootPicker, &scalePicker, &patChBox }) addAndMakeVisible (p);
    }

    void tick (bool changed) override
    {
        const int e = ctx.eng.shared.songEntry.load (std::memory_order_relaxed);
        if (changed || e != entry)
        {
            entry = e;
            songBtn.setLit (ctx.proj().song.songMode != 0);
            rootPicker.sync(); scalePicker.sync(); patChBox.sync();
            chain.repaint(); midi.repaint(); repaint();
        }
    }

    void resized() override
    {
        songBtn.setBounds (12, 26, 90, 24);
        clearBtn.setBounds (108, 26, 90, 24);
        chain.setBounds (12, 56, 440, getHeight() - 64);
        rootPicker.setBounds (472, 30, 70, 44);
        scalePicker.setBounds (548, 30, 160, 44);
        sclBtn.setBounds (472, 80, 236, 24);
        patChBox.setBounds (472, 160, 90, 44);
        midi.setBounds (728, 26, 276, 22 + kTracks * 22 + 4);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg());
        drawLabel (g, "SONG CHAIN", { 12, 6, 200, 16 }, col::accent(), 10.f, juce::Justification::centredLeft, true);
        drawLabel (g, "TUNING", { 472, 6, 200, 16 }, col::accent(), 10.f, juce::Justification::centredLeft, true);
        drawLabel (g, "MIDI", { 472, 136, 200, 16 }, col::accent(), 10.f, juce::Justification::centredLeft, true);
        drawLabel (g, "OUTPUTS", { 728, 6, 200, 16 }, col::accent(), 10.f, juce::Justification::centredLeft, true);
        const Tuning& t = ctx.proj().kit.tuning;
        juce::String info;
        if (t.scale == 255) info << t.customCount << " notes per period of " << juce::String (t.customCents[t.customCount - 1], 1) << " cents";
        else info << juce::String (scaleDef (t.scale).name);
        drawLabel (g, info, { 472, 108, 236, 14 }, col::dim(), 9.f, juce::Justification::centredLeft);
        drawWrapped (g, "MIDI in: channels 1-8 play tracks 1-8.", { 572, 160, 140, 44 }, col::faint(), 8.5f);
        drawLabel (g, "AUTOMATA 1.0   JUCE 9   VST3 / STANDALONE", { 1010, getHeight() - 18, 180, 12 }, col::faint(), 8.f, juce::Justification::centredRight);
    }

private:
    void loadScala()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load Scala tuning", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.scl");
        juce::Component::SafePointer<SongPage> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File()) return;
            Tuning t = safe->ctx.proj().kit.tuning; std::string err;
            if (parseScala (fc.getResult().loadFileAsString().toStdString(), t, err)) { safe->ctx.sess.setTuning (t); safe->ctx.status = "Loaded tuning " + juce::String (t.name); }
            else safe->ctx.status = "Scala file: " + juce::String (err);
        });
    }

    ChainView chain;
    MidiRouting midi;
    Btn songBtn { "SONG MODE" }, clearBtn { "CLEAR" }, sclBtn { "LOAD SCALA (.SCL)" };
    Picker rootPicker, scalePicker;
    NumberBox patChBox;
    std::unique_ptr<juce::FileChooser> chooser;
    int entry = -2;
};

} // namespace

std::unique_ptr<Page> makeSongPage (Ctx& c) { return std::make_unique<SongPage> (c); }

} // namespace ui
