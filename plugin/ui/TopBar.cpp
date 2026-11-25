#include "TopBar.h"
#include "../Export.h"
#include "factory/Presets.h"
#include "gen/Idea.h"

namespace ui {

using namespace atm;

// ============================================================================ PatternSlots
int PatternSlots::slotAt (juce::Point<int> p) const
{
    const int cw = getWidth() / 8, ch = getHeight() / 2;
    const int c = juce::jlimit (0, 7, p.x / juce::jmax (1, cw)), r = juce::jlimit (0, 1, p.y / juce::jmax (1, ch));
    return r * 8 + c;
}

void PatternSlots::tick()
{
    const int p = ctx.eng.shared.currentPattern.load (std::memory_order_relaxed);
    const int q = ctx.eng.shared.queuedPattern.load (std::memory_order_relaxed);
    const int e = ctx.pattern();
    ++blink;
    if (p != playing || q != queued || e != edit || rev != ctx.sess.revision() || (q >= 0 && (blink % 6) == 0))
    {
        playing = p; queued = q; edit = e; rev = ctx.sess.revision();
        repaint();
    }
}

void PatternSlots::paint (juce::Graphics& g)
{
    const int cw = getWidth() / 8, ch = getHeight() / 2;
    for (int i = 0; i < kPatterns; ++i)
    {
        juce::Rectangle<int> r ((i % 8) * cw, (i / 8) * ch, cw - 2, ch - 2);
        const Pattern& pat = ctx.proj().patterns[i];
        int hits = 0;
        for (int t = 0; t < kTracks; ++t) hits += pat.tracks[t].countOn();
        const bool isPlaying = i == playing, isQueued = i == queued && ((blink / 6) % 2 == 0);
        fillPanel (g, r, isPlaying ? col::accent() : (isQueued ? col::accent().withAlpha (0.45f) : (hits > 0 ? col::panelHi().brighter (0.08f) : col::well())), 2.f);
        strokePanel (g, r, i == edit ? col::text() : col::line(), 2.f);
        drawLabel (g, juce::String (pat.name), r, isPlaying ? col::bg() : (hits > 0 ? col::text() : col::faint()), 9.5f, juce::Justification::centred, true);
    }
}

void PatternSlots::mouseDown (const juce::MouseEvent& e)
{
    const int i = slotAt (e.getPosition());
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Pattern " + juce::String (ctx.proj().patterns[i].name));
        m.addItem (1, "Copy"); m.addItem (2, "Paste", clip != nullptr); m.addItem (3, "Clear");
        m.addItem (4, "Append to song chain");
        juce::Component::SafePointer<PatternSlots> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, i] (int r)
        {
            if (safe == nullptr || r == 0) return;
            Ctx& c = safe->ctx;
            if (r == 1) safe->clip = std::make_unique<Pattern> (c.proj().patterns[i]);
            else if (r == 2 && safe->clip) { auto p = std::make_unique<Pattern> (*safe->clip); std::memcpy (p->name, c.proj().patterns[i].name, sizeof (p->name)); c.sess.replacePattern (i, *p); }
            else if (r == 3)
            {
                auto p = std::make_unique<Pattern>();
                std::memcpy (p->name, c.proj().patterns[i].name, sizeof (p->name));
                for (int t = 0; t < kTracks; ++t) { p->tracks[t].seed = c.proj().patterns[i].tracks[t].seed; p->tracks[t].gen.seed = c.proj().patterns[i].tracks[t].gen.seed; }
                c.sess.replacePattern (i, *p);
            }
            else if (r == 4)
            {
                Song s = c.proj().song;
                if (s.length < kChainLen) { s.chain[s.length].pattern = (uint8_t) i; s.chain[s.length].repeats = 1; ++s.length; c.sess.setSong (s); }
            }
        });
        return;
    }
    ctx.sess.setEditPattern (i);
    ctx.clearStepSelection();
    ctx.sess.selectPattern (i, e.mods.isShiftDown());
}

// ============================================================================ TopBar
TopBar::TopBar (Ctx& c) : ctx (c), slots (c), viz (c)
{
    auto addBtn = [this] (Btn& b, const juce::String& help) { b.ctx = &ctx; b.help = help; addAndMakeVisible (b); };
    addBtn (presetPrev, "Previous factory preset");
    addBtn (presetNext, "Next factory preset");
    addBtn (presetName, "Factory presets (kits and patterns)");
    addBtn (saveBtn, "Save the project to a file (samples included)");
    addBtn (loadBtn, "Load a project file");
    addBtn (playBtn, "Play / stop the internal transport (Space). Follows the host when the host is playing.");
    addBtn (fillBtn, "Fill: more density and ratchets, FILL conditions fire. Hold, or tap to latch until the next bar (F)");
    addBtn (breakBtn, "Break: glitches, ratchets and mutation go up until the next bar (B)");
    addBtn (evolveBtn, "Evolve: unlocked tracks slowly mutate while playing (E)");
    addBtn (undoBtn, "Undo (Ctrl+Z)");
    addBtn (redoBtn, "Redo (Ctrl+Shift+Z)");
    addBtn (midiDrag, "Drag into your DAW: the current pattern as a MIDI file (one channel per track)");
    addBtn (wavDrag, "Drag into your DAW: the current pattern rendered to audio (Shift-drag = selected track only)");
    addBtn (songBtn, "Song mode: follow the pattern chain (edit it in the SONG tab)");
    addBtn (ideaBtn, "New Idea: a new kit and pattern set from a fresh seed in the chosen style. Locked tracks are kept (N)");
    presetName.bold = true; presetName.textHeight = 12.f;
    ideaBtn.textHeight = 14.f;
    fillBtn.colour = breakBtn.colour = col::warn();
    evolveBtn.colour = col::mod();
    midiDrag.colour = wavDrag.colour = col::text();

    presetPrev.onClick = [this] { stepPreset (-1); };
    presetNext.onClick = [this] { stepPreset (1); };
    presetName.onClick = [this] { showPresetMenu(); };
    saveBtn.onClick = [this] { saveProject(); };
    loadBtn.onClick = [this] { loadProject(); };
    playBtn.onClick = [this] { togglePlay(); };
    fillBtn.onPressChange = [this] (bool d) { ctx.sess.setFill (d); };
    breakBtn.onPressChange = [this] (bool d) { ctx.sess.setBreak (d); };
    evolveBtn.onClick = [this] { ctx.proc.setEvolve (! ctx.proj().evolveOn); };
    undoBtn.onClick = [this] { ctx.proc.undo(); };
    redoBtn.onClick = [this] { ctx.proc.redo(); };
    songBtn.onClick = [this] { Song s = ctx.proj().song; s.songMode = ! s.songMode; ctx.sess.setSong (s); };
    ideaBtn.onClick = [this] { newIdea (true); };
    midiDrag.onDragStart = [this] { startMidiDrag(); };
    wavDrag.onDragStart = [this] { startAudioDrag(); };
    midiDrag.onClick = [this] { ctx.status = "Drag this button into your DAW or a folder to export the pattern as MIDI"; };
    wavDrag.onClick = [this] { ctx.status = "Drag this button into your DAW or a folder to export the pattern as audio"; };

    tempoBox.ctx = &ctx; tempoBox.caption = "BPM"; tempoBox.help = "Internal tempo (the host's tempo is used whenever it is available)";
    tempoBox.minV = 30; tempoBox.maxV = 300; tempoBox.step = 1; tempoBox.defaultV = 140; tempoBox.pixelsPerStep = 3;
    tempoBox.get = [this] { return (double) std::lround (ctx.eng.shared.playing.load() || hostPlaying ? ctx.eng.shared.bpm.load() : globalParamDesc (GP::Tempo, nullptr).toPhys (ctx.proj().kit.gparams[GP::Tempo])); };
    tempoBox.set = [this] (double v) { ctx.sess.setGlobalParam (GP::Tempo, globalParamDesc (GP::Tempo, nullptr).toNorm ((float) v)); };
    tempoBox.onGestureStart = [this] { ctx.sess.beginTransaction ("Tempo"); };
    tempoBox.onGestureEnd = [this] { ctx.sess.endTransaction(); };
    addAndMakeVisible (tempoBox);

    barsBox.ctx = &ctx; barsBox.caption = "BARS"; barsBox.help = "Length of the edited pattern in bars (cycle for non-free tracks, chaining and quantised switches)";
    barsBox.minV = 1; barsBox.maxV = 16; barsBox.defaultV = 1; barsBox.pixelsPerStep = 8;
    barsBox.get = [this] { return (double) ctx.proj().patterns[ctx.pattern()].lengthBars; };
    barsBox.set = [this] (double v) { ctx.sess.setPatternBars (ctx.pattern(), (int) v); };
    barsBox.onGestureStart = [this] { ctx.sess.beginTransaction ("Pattern length"); };
    barsBox.onGestureEnd = [this] { ctx.sess.endTransaction(); };
    addAndMakeVisible (barsBox);

    seedBox.ctx = &ctx; seedBox.caption = "SEED"; seedBox.help = "The seed of the current idea. Set it to recall an idea exactly: same seed, same style, same result.";
    seedBox.minV = 1; seedBox.maxV = 99999; seedBox.defaultV = 1; seedBox.pixelsPerStep = 2; seedBox.colour = col::accent();
    seedBox.get = [this] { return (double) (ctx.proj().seed % 100000u); };
    seedBox.set = [this] (double v) { ctx.sess.setProjectMeta (nullptr, (uint32_t) v, ctx.proj().style, ctx.proj().midiPatternCh); };
    seedBox.onGestureStart = [this] { ctx.sess.beginTransaction ("Seed"); };
    seedBox.onGestureEnd = [this] { ctx.sess.endTransaction(); newIdea (false); };
    seedBox.format = [] (double v) { return juce::String ((int) v).paddedLeft ('0', 5); };
    addAndMakeVisible (seedBox);

    stylePicker.ctx = &ctx; stylePicker.caption = "STYLE"; stylePicker.help = "Style rule-set used by New Idea";
    stylePicker.items = [] { juce::StringArray a; for (int i = 0; i < Style::count; ++i) a.add (juce::String (styleName (i)).toUpperCase()); return a; };
    stylePicker.get = [this] { return (int) ctx.proj().style; };
    stylePicker.set = [this] (int v) { ctx.sess.setProjectMeta (nullptr, ctx.proj().seed, v, ctx.proj().midiPatternCh); };
    addAndMakeVisible (stylePicker);

    static constexpr int macros[6] = { GP::Density, GP::Complexity, GP::Mutation, GP::Chaos, GP::Glitch, GP::Space };
    for (int gp : macros)
    {
        auto k = std::make_unique<Knob> (ctx, ParamRef::G (gp));
        k->big = true;
        addAndMakeVisible (*k);
        macroKnobs.push_back (std::move (k));
    }
    for (int i = 0; i < 4; ++i)
    {
        auto k = std::make_unique<Knob> (ctx, ParamRef::G (GP::Macro1 + i));
        k->setAccent (col::mod());
        addAndMakeVisible (*k);
        modKnobs.push_back (std::move (k));
    }
    masterKnob = std::make_unique<Knob> (ctx, ParamRef::G (GP::Master));
    masterKnob->setAccent (col::text());
    addAndMakeVisible (*masterKnob);
    swingKnob = std::make_unique<Knob> (ctx, ParamRef::G (GP::Swing));
    swingKnob->setAccent (col::text());
    addAndMakeVisible (*swingKnob);

    addAndMakeVisible (slots);
    addAndMakeVisible (viz);
}

void TopBar::resized()
{
    const int W = getWidth();
    viz.setBounds (W - 150, 4, 144, 144);
    // ---- row 1
    int x = 212; const int y = 8, h = 30;
    auto place = [&] (juce::Component& c, int w, int gap = 4) { c.setBounds (x, y, w, h); x += w + gap; };
    place (presetPrev, 22, 2); place (presetName, 232, 2); place (presetNext, 22, 6);
    place (saveBtn, 44, 2); place (loadBtn, 44, 12);
    place (playBtn, 62, 4);
    tempoBox.setBounds (x, y - 2, 56, 43); x += 60;
    barsBox.setBounds (x, y - 2, 40, 43); x += 50;
    slots.setBounds (x, y - 2, 216, 44); x += 222;
    place (songBtn, 46, 10);
    place (undoBtn, 44, 2); place (redoBtn, 44, 10);
    place (midiDrag, 42, 2); place (wavDrag, 42, 0);

    // ---- row 2
    ideaBtn.setBounds (8, 56, 196, 34);
    stylePicker.setBounds (8, 94, 124, 34);
    seedBox.setBounds (136, 94, 68, 34);
    x = 214;
    for (auto& k : macroKnobs) { k->setBounds (x, 56, 78, 90); x += 80; }
    x += 6;
    fillBtn.setBounds (x, 58, 74, 27); breakBtn.setBounds (x, 88, 74, 27); evolveBtn.setBounds (x, 118, 74, 27);
    x += 86;
    for (auto& k : modKnobs) { k->setBounds (x, 70, 58, 70); x += 58; }
    x += 8;
    swingKnob->setBounds (x, 70, 58, 70); x += 58;
    masterKnob->setBounds (x, 70, 58, 70);
}

void TopBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (col::panel());
    g.fillRect (r);
    g.setColour (col::line());
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    // identity
    drawLabel (g, "AUTOMATA", { 8, 6, 196, 26 }, col::accent(), 22.f, juce::Justification::centredLeft, true);
    drawLabel (g, "GENERATIVE RHYTHM WORKSTATION", { 9, 31, 196, 12 }, col::dim(), 8.6f, juce::Justification::centredLeft);
    // a strip of rule-30 cells seeded by the project seed: the same seed always draws the same strip
    {
        uint32_t row = hash32 (ctx.proj().seed) | 1u;
        for (int y2 = 0; y2 < 3; ++y2)
        {
            for (int i = 0; i < 32; ++i)
                if ((row >> i) & 1u) { g.setColour (col::accent().withAlpha (0.16f + 0.14f * (float) y2)); g.fillRect (8 + i * 6, 45 + y2 * 3, 5, 2); }
            uint32_t next = 0;
            for (int i = 0; i < 32; ++i)
            {
                const uint32_t l = (row >> ((i + 31) & 31)) & 1u, c = (row >> i) & 1u, rr = (row >> ((i + 1) & 31)) & 1u;
                next |= (l ^ (c | rr)) << i;
            }
            row = next;
        }
    }
    // section captions
    drawLabel (g, "GENERATE", { 214, 44, 100, 10 }, col::faint(), 8.6f, juce::Justification::centredLeft, true);
    const int modX = modKnobs.empty() ? 0 : modKnobs.front()->getX();
    drawLabel (g, "MACROS", { modX + 4, 58, 100, 10 }, col::faint(), 8.6f, juce::Justification::centredLeft, true);

    // meters: output L/R, limiter gain reduction, CPU
    const int mx = getWidth() - 150 - 62, my = 62;
    auto bar = [&] (int x, float v, juce::Colour c, bool fromTop)
    {
        const juce::Rectangle<float> b ((float) x, (float) my, 6.f, 64.f);
        g.setColour (col::well()); g.fillRect (b);
        g.setColour (c);
        const float hh = b.getHeight() * juce::jlimit (0.f, 1.f, v);
        g.fillRect (fromTop ? b.withHeight (hh) : b.withTop (b.getBottom() - hh));
    };
    bar (mx, (gainToDb (peakL) + 48.f) / 48.f, peakL > 0.98f ? col::warn() : col::text(), false);
    bar (mx + 8, (gainToDb (peakR) + 48.f) / 48.f, peakR > 0.98f ? col::warn() : col::text(), false);
    bar (mx + 20, -gainToDb (grShown) / 18.f, col::lock(), true);
    drawLabel (g, "OUT", { mx - 4, my + 66, 22, 10 }, col::dim(), 8.5f);
    drawLabel (g, "GR", { mx + 15, my + 66, 16, 10 }, col::dim(), 8.5f);
    drawLabel (g, juce::String ((int) std::lround (cpuShown * 100.f)) + "%", { mx + 28, my + 22, 34, 12 }, cpuShown > 0.6f ? col::warn() : col::text(), 10.f, juce::Justification::centred, true);
    drawLabel (g, "CPU", { mx + 28, my + 36, 34, 10 }, col::dim(), 8.5f);
}

void TopBar::tick (bool projectChanged)
{
    slots.tick();
    viz.tick();
    for (auto& k : macroKnobs) k->sync();
    for (auto& k : modKnobs) k->sync();
    masterKnob->sync(); swingKnob->sync();
    tempoBox.sync(); barsBox.sync(); seedBox.sync(); stylePicker.sync();

    const bool playing = ctx.eng.shared.playing.load (std::memory_order_relaxed);
    playBtn.setLit (playing);
    playBtn.setText (playing ? "STOP" : "PLAY");
    fillBtn.setLit (ctx.eng.shared.fill.load (std::memory_order_relaxed));
    breakBtn.setLit (ctx.eng.shared.brk.load (std::memory_order_relaxed));
    evolveBtn.setLit (ctx.proj().evolveOn != 0);
    songBtn.setLit (ctx.proj().song.songMode != 0);
    undoBtn.enabled = ctx.sess.canUndo(); redoBtn.enabled = ctx.sess.canRedo();
    if (projectChanged)
    {
        presetName.setText (juce::String (ctx.proj().name).toUpperCase());
        undoBtn.repaint(); redoBtn.repaint();
    }
    // meters
    const float cpu = ctx.proc.cpuLoad();
    const float gr = ctx.eng.shared.gainReduction.load (std::memory_order_relaxed);
    const float pl = juce::jmax (ctx.eng.shared.mainPeak[0].load (std::memory_order_relaxed), peakL * 0.85f);
    const float pr = juce::jmax (ctx.eng.shared.mainPeak[1].load (std::memory_order_relaxed), peakR * 0.85f);
    if (std::abs (cpu - cpuShown) > 0.004f || std::abs (gr - grShown) > 0.004f || std::abs (pl - peakL) > 0.002f || std::abs (pr - peakR) > 0.002f)
    {
        cpuShown = cpu; grShown = gr; peakL = pl; peakR = pr;
        repaint (getWidth() - 150 - 70, 56, 70, 92);
    }
}

// ============================================================================ actions
void TopBar::togglePlay()
{
    ctx.sess.setPlay (! ctx.eng.shared.playing.load (std::memory_order_relaxed));
}

void TopBar::newIdea (bool newSeed)
{
    auto p = std::make_unique<Project> (ctx.proj());
    IdeaOptions o;
    o.style = p->style;
    o.seed = newSeed ? (uint32_t) (1 + juce::Random::getSystemRandom().nextInt (99998)) : p->seed;
    styleTempoRange (o.style, o.tempoMin, o.tempoMax);
    const float bpm = generateIdea (*p, o, &ctx.sess.samples());
    ctx.sess.replaceProject (*p);
    ctx.clearStepSelection();
    presetIndex = -1;
    ctx.status = "New idea  seed " + juce::String ((int) (o.seed % 100000u)).paddedLeft ('0', 5) + "  " + styleName (o.style) + "  " + juce::String ((int) std::lround (bpm)) + " bpm";
}

void TopBar::loadFactoryPreset (int index)
{
    if (index < 0 || index >= numKitPresets()) return;
    auto p = std::make_unique<Project>();
    if (! buildKitPreset (index, *p, &ctx.sess.samples())) return;
    // keep track locks out of presets: a preset replaces everything
    ctx.sess.replaceProject (*p);
    ctx.clearStepSelection();
    presetIndex = index;
    ctx.status = juce::String ("Loaded ") + kitPresetInfo (index).name + "  (" + styleName (kitPresetInfo (index).style) + ", " + juce::String ((int) kitPresetInfo (index).bpm) + " bpm)";
}

void TopBar::stepPreset (int delta)
{
    const int n = numKitPresets();
    if (n <= 0) return;
    loadFactoryPreset (((presetIndex < 0 ? (delta > 0 ? -1 : 0) : presetIndex) + delta + n) % n);
}

void TopBar::showPresetMenu()
{
    juce::PopupMenu m;
    for (int s = 0; s < Style::count; ++s)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < numKitPresets(); ++i)
            if (kitPresetInfo (i).style == s)
                sub.addItem (i + 1, juce::String (kitPresetInfo (i).name) + "   " + juce::String ((int) kitPresetInfo (i).bpm) + " bpm", true, i == presetIndex);
        if (sub.getNumItems() > 0) m.addSubMenu (styleName (s), sub);
    }
    m.addSeparator();
    m.addItem (10001, "Init project");
    juce::Component::SafePointer<TopBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetName), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 10001)
        {
            auto p = std::make_unique<Project>(); initProject (*p);
            safe->ctx.sess.replaceProject (*p); safe->presetIndex = -1;
        }
        else safe->loadFactoryPreset (r - 1);
    });
}

void TopBar::saveProject()
{
    chooser = std::make_unique<juce::FileChooser> ("Save Automata project", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                       .getChildFile ("Automata").getChildFile (juce::String (ctx.proj().name) + ".automata"), "*.automata");
    juce::Component::SafePointer<TopBar> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const juce::File f = fc.getResult();
        if (f == juce::File()) return;
        f.getParentDirectory().createDirectory();
        Ctx& c = safe->ctx;
        c.sess.setProjectMeta (f.getFileNameWithoutExtension().toRawUTF8(), c.proj().seed, c.proj().style, c.proj().midiPatternCh);
        c.status = c.proc.saveProjectFile (f.withFileExtension ("automata")) ? "Saved " + f.getFileName() : "Could not write " + f.getFullPathName();
    });
}

void TopBar::loadProject()
{
    chooser = std::make_unique<juce::FileChooser> ("Load Automata project", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Automata"), "*.automata");
    juce::Component::SafePointer<TopBar> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const juce::File f = fc.getResult();
        if (f == juce::File()) return;
        Ctx& c = safe->ctx;
        c.status = c.proc.loadProjectFile (f) ? "Loaded " + f.getFileName() : "Could not read " + f.getFileName();
        c.clearStepSelection();
        safe->presetIndex = -1;
    });
}

void TopBar::startMidiDrag()
{
    const juce::File f = exporter::writePatternMidi (ctx.proc, ctx.pattern());
    if (f == juce::File()) { ctx.status = "MIDI export failed"; return; }
    ctx.status = "Dragging " + f.getFileName();
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void TopBar::startAudioDrag()
{
    const bool solo = juce::ModifierKeys::getCurrentModifiers().isShiftDown();
    ctx.status = "Rendering...";
    const juce::File f = exporter::renderPatternAudio (ctx.proc, ctx.pattern(), solo ? ctx.track : -1);
    if (f == juce::File()) { ctx.status = "Audio export failed"; return; }
    ctx.status = "Dragging " + f.getFileName();
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

} // namespace ui
