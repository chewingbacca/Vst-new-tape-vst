#include "PluginEditor.h"

using namespace juce;
using namespace splinter;

SplinterEditor::SplinterEditor (SplinterProcessor& p) : AudioProcessorEditor (p), proc (p), canvas (*this), waveform (p)
{
    addAndMakeVisible (canvas);
    canvas.addAndMakeVisible (waveform);
    auto add = [this] (Component* c) { owned.add (c); canvas.addAndMakeVisible (c); return c; };

    presetPrev = static_cast<TextBtn*> (add (new TextBtn ("<", theme::white)));
    presetNext = static_cast<TextBtn*> (add (new TextBtn (">", theme::white)));
    presetName = static_cast<TextBtn*> (add (new TextBtn ("", theme::white)));
    presetSave = static_cast<TextBtn*> (add (new TextBtn ("SAVE", theme::white)));
    presetName->fontSize = 12.5f;
    presetPrev->onClick = [this] { stepPreset (-1); };
    presetNext->onClick = [this] { stepPreset (1); };
    presetName->onClick = [this] { showPresetMenu(); };
    presetSave->onClick = [this] { beginSave(); };
    presetSave->setTooltip ("Save the current settings and patterns as a user preset.");

    source = static_cast<ChoiceBox*> (add (new ChoiceBox (proc.apvts, pid::sourceMode, "", theme::pink)));
    source->setTooltip ("Loop: play the loaded or captured loop in sync. Live Input: glitch the incoming audio through a rolling buffer.");
    capture = static_cast<TextBtn*> (add (new TextBtn ("CAPTURE", theme::pink)));
    capture->setTooltip ("Grabs the last bars of incoming audio (see Capture Bars on the Output page) as the loop, sliced and ready to play.");
    capture->onClick = [this] { proc.captureLiveInput(); };
    key = static_cast<ChoiceBox*> (add (new ChoiceBox (proc.apvts, pid::key, "", theme::violet)));
    scale = static_cast<ChoiceBox*> (add (new ChoiceBox (proc.apvts, pid::scale, "", theme::violet)));
    key->setTooltip ("Key: harmony voices, pitched stutters, the comb and the ring modulator all stay inside this key and scale.");
    play = static_cast<TextBtn*> (add (new TextBtn ("PLAY", theme::lime)));
    play->onClick = [this] { proc.internalPlaying.store (! proc.internalPlaying.load()); };
    play->setTooltip ("Built-in transport, used when the host supplies none (standalone). In a DAW Splinter follows the host.");

    sliceBtn = static_cast<TextBtn*> (add (new TextBtn ("", theme::pink)));
    sensDown = static_cast<TextBtn*> (add (new TextBtn ("-", theme::pink)));
    sensUp = static_cast<TextBtn*> (add (new TextBtn ("+", theme::pink)));
    lenHalf = static_cast<TextBtn*> (add (new TextBtn ("/2", theme::lime)));
    lenDouble = static_cast<TextBtn*> (add (new TextBtn ("x2", theme::lime)));
    loopsBtn = static_cast<TextBtn*> (add (new TextBtn ("LOOPS", theme::lime)));
    sliceBtn->onClick = [this] { showSliceMenu(); };
    sliceBtn->setTooltip ("How the loop is sliced: at detected transients or on a grid. Drag markers in the waveform to edit; double-click adds or removes one.");
    auto nudgeSens = [this] (float d) { const PatternBank b = proc.getBank(); proc.setSliceMode (0, jlimit (0.0f, 1.0f, b.sliceSensitivity + d)); };
    sensDown->onClick = [nudgeSens] { nudgeSens (-0.1f); };
    sensUp->onClick = [nudgeSens] { nudgeSens (0.1f); };
    sensDown->setTooltip ("Fewer transient slices."); sensUp->setTooltip ("More transient slices.");
    lenHalf->setTooltip ("The loop is half as many bars as detected (doubles its tempo reading).");
    lenDouble->setTooltip ("The loop is twice as many bars as detected (halves its tempo reading).");
    lenHalf->onClick = [this] { const LoopInfo i = proc.loops.getInfo(); if (i.kind != LoopInfo::none && i.beats >= 2.0) proc.loops.setBeats (i.beats * 0.5); };
    lenDouble->onClick = [this] { const LoopInfo i = proc.loops.getInfo(); if (i.kind != LoopInfo::none && i.beats <= 64.0) proc.loops.setBeats (i.beats * 2.0); };
    loopsBtn->onClick = [this] { showLoopMenu(); };
    loopsBtn->setTooltip ("Built-in demo loops, or open an audio file. You can also drop a file on the waveform.");

    static const char* macroIds[7] = { pid::chop, pid::stutter, pid::warp, pid::harmony, pid::crush, pid::chaos, pid::mix };
    static const char* macroNames[7] = { "CHOP", "STUTTER", "WARP", "HARMONY", "CRUSH", "CHAOS", "MIX" };
    static const char* macroTips[7] = {
        "Chop: slice rearrangement. Half way plays the pattern as programmed; lower thins it out, higher adds more swaps.",
        "Stutter: repeats and freezes. Half way plays the pattern as programmed; higher adds fills, mostly towards the end of the bar.",
        "Warp: time and pitch bending. Scales the time curve up to half way, then adds reverses, tape stops and pitch dives.",
        "Harmony: level of the harmony voices.",
        "Crush: bit crushing, resonators and ring mod. Half way plays the pattern as programmed.",
        "Chaos: seeded randomness on every unlocked lane. The same seed always plays the same way.",
        "Mix: dry / wet. Dry is the unglitched loop (or the live input)." };
    const Colour macroCols[7] = { theme::pink, Colour (0xffff7a59), theme::lime, theme::violet, theme::amber, theme::red, theme::white };
    for (int i = 0; i < 7; ++i)
    {
        macros[i] = new Knob (proc.apvts, macroIds[i], macroNames[i], macroCols[i], true);
        macros[i]->setTooltip (macroTips[i]);
        add (macros[i]);
    }

    static const char* tabNames[5] = { "PERFORM", "CURVES", "SEQUENCE", "HARMONY", "OUTPUT" };
    for (int i = 0; i < 5; ++i)
    {
        tabs[i] = static_cast<TextBtn*> (add (new TextBtn (tabNames[i], theme::white)));
        tabs[i]->fontSize = 11.5f;
        tabs[i]->onClick = [this, i] { setPage (i); };
    }
    pages[0] = std::make_unique<PerformPage> (proc); pages[1] = std::make_unique<CurvesPage> (proc); pages[2] = std::make_unique<SequencePage> (proc);
    pages[3] = std::make_unique<HarmonyPage> (proc); pages[4] = std::make_unique<OutputPage> (proc);
    for (auto& pg : pages) canvas.addChildComponent (*pg);

    canvas.addChildComponent (nameEditor);
    nameEditor.setFont (theme::font (13.0f, true));
    nameEditor.setColour (TextEditor::backgroundColourId, theme::panelHi);
    nameEditor.setColour (TextEditor::textColourId, theme::text);
    nameEditor.setColour (TextEditor::outlineColourId, theme::lime);
    nameEditor.setColour (TextEditor::focusedOutlineColourId, theme::lime);
    nameEditor.onReturnKey = [this]
    {
        const String n = nameEditor.getText().trim();
        if (n.isNotEmpty()) proc.saveUserPreset (n);
        nameEditor.setVisible (false);
    };
    nameEditor.onEscapeKey = [this] { nameEditor.setVisible (false); };
    nameEditor.onFocusLost = [this] { nameEditor.setVisible (false); };

    const int wantedW = jlimit (baseW * 3 / 4, baseW * 2, proc.editorWidth.load());
    canvas.setSize (baseW, baseH);
    layout();
    setPage (jlimit (0, 4, proc.editorPage.load()));
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    setResizeLimits (baseW * 3 / 4, baseH * 3 / 4, baseW * 2, baseH * 2);
    setSize (wantedW, roundToInt (wantedW * (double) baseH / baseW));
    refreshTexts();
    startTimerHz (30);
}

SplinterEditor::~SplinterEditor() { stopTimer(); proc.uiHeldLanes.store (0); }

void SplinterEditor::paint (Graphics& g) { g.fillAll (theme::bg); }

void SplinterEditor::resized()
{
    const float s = jmin ((float) getWidth() / (float) baseW, (float) getHeight() / (float) baseH);
    canvas.setTransform (AffineTransform::scale (s));
    canvas.setBounds (0, 0, baseW, baseH);
    proc.editorWidth.store (getWidth()); proc.editorHeight.store (getHeight());
}

void SplinterEditor::layout()
{
    // top bar
    presetPrev->setBounds (168, 10, 24, 28); presetName->setBounds (196, 10, 214, 28); presetNext->setBounds (414, 10, 24, 28); presetSave->setBounds (444, 10, 48, 28);
    nameEditor.setBounds (196, 10, 214, 28);
    source->setBounds (512, 10, 104, 28); capture->setBounds (620, 10, 76, 28);
    key->setBounds (712, 10, 52, 28); scale->setBounds (768, 10, 112, 28);
    play->setBounds (892, 10, 50, 28);
    bpmArea = Rectangle<int> (946, 10, 62, 28);
    meterArea = Rectangle<int> (1014, 10, 16, 28);
    // waveform + its tool row
    waveform.setBounds (10, 48, baseW - 20, 190);
    const int ty = 48 + 190 - 19;
    loopsBtn->setBounds (baseW - 84, ty, 64, 17);
    lenDouble->setBounds (baseW - 116, ty, 28, 17); lenHalf->setBounds (baseW - 148, ty, 28, 17);
    sensUp->setBounds (baseW - 178, ty, 20, 17); sensDown->setBounds (baseW - 200, ty, 20, 17);
    sliceBtn->setBounds (baseW - 348, ty, 144, 17);
    for (auto* b : { loopsBtn, lenDouble, lenHalf, sensUp, sensDown, sliceBtn }) b->fontSize = 9.5f;
    // macros
    const int mw = (baseW - 40) / 7;
    for (int i = 0; i < 7; ++i) macros[i]->setBounds (20 + i * mw + (mw - 104) / 2, 244, 104, 112);
    // tabs + page
    for (int i = 0; i < 5; ++i) tabs[i]->setBounds (10 + i * 112, 362, 108, 24);
    for (auto& pg : pages) pg->setBounds (10, 390, baseW - 20, 284);
}

void SplinterEditor::setPage (int index)
{
    page = jlimit (0, 4, index);
    for (int i = 0; i < 5; ++i) { pages[i]->setVisible (i == page); tabs[i]->setLit (i == page); }
    proc.editorPage.store (page);
}

void SplinterEditor::paintCanvas (Graphics& g)
{
    g.fillAll (theme::bg);
    // logo: the name, split by a shard
    g.setFont (theme::font (23.0f, true));
    g.setColour (theme::text); g.drawText ("SPLINTER", Rectangle<int> (30, 8, 130, 30), Justification::centredLeft, false);
    Path shard; shard.addTriangle (10.0f, 36.0f, 22.0f, 8.0f, 26.0f, 30.0f);
    g.setColour (theme::lime); g.fillPath (shard);
    Path shard2; shard2.addTriangle (14.0f, 38.0f, 26.0f, 34.0f, 20.0f, 42.0f);
    g.setColour (theme::pink); g.fillPath (shard2);
    g.setColour (theme::bg); g.drawLine (96.0f, 8.0f, 88.0f, 40.0f, 2.0f);

    // tempo box
    theme::drawPanel (g, bpmArea.toFloat(), theme::panelHi, 5.0f);
    theme::drawLabel (g, lastHost ? "HOST" : "BPM", bpmArea.withHeight (10).translated (0, 2), theme::textFaint, 8.0f);
    theme::drawLabel (g, String (lastBpm, 1), bpmArea.withTrimmedTop (9), lastHost ? theme::textDim : theme::text, 12.0f);
    // output meter
    for (int c = 0; c < 2; ++c)
    {
        const Rectangle<float> r ((float) meterArea.getX() + (float) c * 9.0f, (float) meterArea.getY(), 7.0f, (float) meterArea.getHeight());
        g.setColour (theme::grid); g.fillRect (r);
        const float lvl = c == 0 ? peakL : peakR;
        const float frac = (jlimit (-48.0f, 0.0f, gainToDb (lvl)) + 48.0f) / 48.0f;
        g.setColour (lvl > 0.95f ? theme::red : theme::lime); g.fillRect (r.withTop (r.getBottom() - r.getHeight() * frac));
    }
    // macro strip backdrop
    theme::drawPanel (g, Rectangle<float> (10.0f, 242.0f, (float) baseW - 20.0f, 116.0f), theme::panel, 12.0f);
    g.setColour (theme::outline); g.drawHorizontalLine (388, 10.0f, (float) baseW - 10.0f);
}

void SplinterEditor::canvasMouseDown (const MouseEvent& e)
{
    if (bpmArea.contains (e.getPosition()) && ! lastHost) bpmDragStart = proc.internalBpm.load();
}

void SplinterEditor::canvasMouseDrag (const MouseEvent& e)
{
    if (bpmArea.contains (e.getMouseDownPosition()) && ! lastHost)
        proc.internalBpm.store (jlimit (40.0, 240.0, std::round ((bpmDragStart - e.getDistanceFromDragStartY() * 0.25) * 2.0) / 2.0));
}

void SplinterEditor::refreshTexts()
{
    const String pn = proc.getPresetName();
    if (pn != lastPreset) { lastPreset = pn; presetName->setText (pn); }
    const PatternBank b = proc.getBank();
    if (b.sliceMode != lastSliceMode || b.sliceSensitivity != lastSens)
    {
        lastSliceMode = b.sliceMode; lastSens = b.sliceSensitivity;
        static const char* names[6] = { "TRANSIENTS", "GRID 1/4", "GRID 1/8", "GRID 1/16", "GRID 1/32", "MANUAL" };
        sliceBtn->setText (String ("SLICE: ") + names[jlimit (0, 5, b.sliceMode)] + (b.sliceMode == 0 ? "  " + String (roundToInt (b.sliceSensitivity * 100.0f)) + "%" : String()));
        sensDown->setVisible (b.sliceMode == 0); sensUp->setVisible (b.sliceMode == 0);
    }
}

void SplinterEditor::timerCallback()
{
    auto& vis = proc.engine.visual;
    waveform.tick();
    pages[page]->tick();

    // macros glow when the effects they govern fire
    for (int l = 0; l < kNumLanes; ++l)
    {
        const float lvl = vis.laneLevel[l].load (std::memory_order_relaxed);
        glow[l] = lvl > 0.5f ? 1.0f : glow[l] * 0.82f;
    }
    const float warpNow = jmin (1.0f, std::fabs (vis.timeOffsetBeats.load()) * 2.0f);
    float harm = 0.0f; for (auto& h : vis.harmLevel) harm = jmax (harm, h.load());
    float any = 0.0f; for (float gl : glow) any = jmax (any, gl);
    macros[0]->setActivity (glow[laneChop]);
    macros[1]->setActivity (jmax (glow[laneStutter], glow[laneFreeze]));
    macros[2]->setActivity (jmax (warpNow, glow[laneReverse], jmax (glow[laneTapeStop], glow[lanePitch])));
    macros[3]->setActivity (harm);
    macros[4]->setActivity (jmax (glow[laneCrush], glow[laneComb], glow[laneRing]));
    macros[5]->setActivity (proc.getParam (pid::chaos) > 0.001f ? any : 0.0f);

    const float l = jmax (vis.peakL.load(), peakL * 0.85f), r = jmax (vis.peakR.load(), peakR * 0.85f);
    const bool host = proc.hostTransportSeen.load(), playing = vis.playing.load();
    const double bpm = vis.bpm.load();
    if (std::fabs (l - peakL) + std::fabs (r - peakR) > 1.0e-4f) canvas.repaint (meterArea.expanded (2));
    if (host != lastHost || bpm != lastBpm) canvas.repaint (bpmArea);
    if (host != lastHost || playing != lastPlaying)
    {
        play->setVisible (! host);
        play->setText (proc.internalPlaying.load() ? "STOP" : "PLAY"); play->setLit (proc.internalPlaying.load());
    }
    peakL = l; peakR = r; lastHost = host; lastPlaying = playing; lastBpm = bpm;
    capture->setLit (proc.getParam (pid::sourceMode) > 0.5f);
    refreshTexts();
}

void SplinterEditor::stepPreset (int delta)
{
    const int n = presets::numFactory();
    int i = proc.getFactoryIndex();
    i = i < 0 ? (delta > 0 ? 0 : n - 1) : (i + delta + n) % n;
    proc.loadFactoryPreset (i);
}

void SplinterEditor::showPresetMenu()
{
    PopupMenu m;
    StringArray cats;
    for (int i = 0; i < presets::numFactory(); ++i) cats.addIfNotAlreadyThere (presets::factoryCategory (i));
    for (auto& cat : cats)
    {
        PopupMenu sub;
        for (int i = 0; i < presets::numFactory(); ++i)
            if (presets::factoryCategory (i) == cat) sub.addItem (i + 1, presets::factoryName (i), true, i == proc.getFactoryIndex());
        m.addSubMenu (cat, sub);
    }
    const auto files = presets::userPresetFiles();
    PopupMenu user;
    for (int i = 0; i < files.size(); ++i) user.addItem (1000 + i, files[i].getFileNameWithoutExtension());
    m.addSeparator();
    m.addSubMenu ("User", user, files.size() > 0);
    m.addItem (9000, "Save as...");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (presetName), [safe = Component::SafePointer<SplinterEditor> (this), files] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 9000) safe->beginSave();
        else if (r >= 1000) { if (r - 1000 < files.size()) safe->proc.loadUserPreset (files[r - 1000]); }
        else safe->proc.loadFactoryPreset (r - 1);
    });
}

void SplinterEditor::beginSave()
{
    nameEditor.setText (proc.getFactoryIndex() >= 0 ? "My " + proc.getPresetName() : proc.getPresetName(), false);
    nameEditor.setVisible (true); nameEditor.toFront (true); nameEditor.grabKeyboardFocus(); nameEditor.selectAll();
}

void SplinterEditor::showLoopMenu()
{
    PopupMenu m;
    m.addSectionHeader ("Demo loops (synthesised)");
    const LoopInfo info = proc.loops.getInfo();
    for (int i = 0; i < kNumDemoLoops; ++i)
        m.addItem (i + 1, String (demoLoopInfo (i).name) + "   " + String (demoLoopInfo (i).bpm, 0) + " BPM", true, info.kind == LoopInfo::demo && info.demoIndex == i);
    m.addSeparator();
    m.addItem (100, "Open audio file...");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (loopsBtn), [safe = Component::SafePointer<SplinterEditor> (this)] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 100)
        {
            safe->chooser = std::make_unique<FileChooser> ("Open a loop", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
            safe->chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [safe] (const FileChooser& fc)
            {
                if (safe == nullptr) return;
                const File f = fc.getResult();
                if (f.existsAsFile()) { safe->proc.loops.loadFile (f); safe->proc.setParam (pid::sourceMode, 0.0f); }
            });
        }
        else { safe->proc.loops.loadDemo (r - 1); safe->proc.setParam (pid::sourceMode, 0.0f); }
    });
}

void SplinterEditor::showSliceMenu()
{
    PopupMenu m;
    const PatternBank b = proc.getBank();
    static const char* names[5] = { "At transients", "Grid: 1/4 notes", "Grid: 1/8 notes", "Grid: 1/16 notes", "Grid: 1/32 notes" };
    for (int i = 0; i < 5; ++i) m.addItem (i + 1, names[i], true, b.sliceMode == i);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (sliceBtn), [safe = Component::SafePointer<SplinterEditor> (this), sens = b.sliceSensitivity] (int r)
    { if (safe != nullptr && r > 0) safe->proc.setSliceMode (r - 1, sens); });
}
