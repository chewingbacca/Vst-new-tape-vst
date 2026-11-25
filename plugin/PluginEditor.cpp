#include "PluginEditor.h"
#include "ui/MainView.h"
#include "ui/TopBar.h"
#include "ui/DetailPanel.h"
#include "gen/Generators.h"

using namespace atm;

AutomataEditor::AutomataEditor (AutomataProcessor& p) : AudioProcessorEditor (p), proc (p), ctx (p)
{
    setOpaque (true);
    view = std::make_unique<ui::MainView> (ctx);
    addAndMakeVisible (*view);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (kBaseW * 7 / 10, kBaseH * 7 / 10, kBaseW * 2, kBaseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setSize (kBaseW, kBaseH);
    ctx.sess.setSelectedTrack (ctx.track);
    startTimerHz (30);
}

AutomataEditor::~AutomataEditor() { stopTimer(); }

void AutomataEditor::paint (juce::Graphics& g) { g.fillAll (ui::col::bg()); }

void AutomataEditor::resized()
{
    if (view == nullptr) return;
    const float scale = (float) getWidth() / (float) kBaseW;
    view->setTransform (juce::AffineTransform::scale (scale));
    view->setBounds (0, 0, kBaseW, kBaseH);
}

void AutomataEditor::timerCallback() { refreshNow(); }

void AutomataEditor::refreshNow()
{
    const uint64_t rev = ctx.sess.revision();
    const bool changed = rev != lastRev;
    lastRev = rev;
    view->tick (changed);
}

// ---------------------------------------------------------------------------- keyboard
bool AutomataEditor::keyStateChanged (bool)
{
    // momentary fill / break while the key is held
    const bool f = juce::KeyPress::isKeyCurrentlyDown ('f') || juce::KeyPress::isKeyCurrentlyDown ('F');
    const bool b = juce::KeyPress::isKeyCurrentlyDown ('b') || juce::KeyPress::isKeyCurrentlyDown ('B');
    const bool cmd = juce::ModifierKeys::getCurrentModifiers().isCommandDown() || juce::ModifierKeys::getCurrentModifiers().isCtrlDown();
    if (f != fillKeyDown && ! cmd) { fillKeyDown = f; ctx.sess.setFill (f); }
    if (b != breakKeyDown && ! cmd) { breakKeyDown = b; ctx.sess.setBreak (b); }
    return false;
}

bool AutomataEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const bool cmd = mods.isCommandDown() || mods.isCtrlDown();
    const int code = k.getKeyCode();
    const juce::juce_wchar ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());

    if (cmd && (code == 'z' || code == 'Z')) { if (mods.isShiftDown()) proc.redo(); else proc.undo(); return true; }
    if (cmd && (code == 'y' || code == 'Y')) { proc.redo(); return true; }
    if (cmd && (code == 'c' || code == 'C')) { ctx.clipPattern = std::make_unique<TrackPattern> (ctx.tp()); ctx.status = "Copied track " + juce::String (ctx.track + 1) + " pattern"; return true; }
    if (cmd && (code == 'v' || code == 'V')) { if (ctx.clipPattern) ctx.sess.setTrackPattern (ctx.pattern(), ctx.track, *ctx.clipPattern); return true; }
    if (cmd && (code == 's' || code == 'S')) { view->top->saveProject(); return true; }
    if (cmd && (code == 'o' || code == 'O')) { view->top->loadProject(); return true; }
    if (cmd) return false;

    if (code == juce::KeyPress::spaceKey) { view->top->togglePlay(); return true; }
    if (code == juce::KeyPress::escapeKey) { ctx.clearStepSelection(); return true; }
    if (code >= juce::KeyPress::F1Key && code < juce::KeyPress::F1Key + ui::numTabs) { ctx.setTab (code - juce::KeyPress::F1Key); return true; }
    if (ch >= '1' && ch <= '8') { ctx.selectTrack ((int) (ch - '1')); return true; }
    if (code == juce::KeyPress::upKey)   { ctx.selectTrack (ctx.track - 1); return true; }
    if (code == juce::KeyPress::downKey) { ctx.selectTrack (ctx.track + 1); return true; }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        const int len = ctx.tp().length;
        const int d = code == juce::KeyPress::rightKey ? 1 : -1;
        const int cur = ctx.hasStepSelection() ? ctx.stepB : (d > 0 ? -1 : 0);
        const int next = ((cur + d) % len + len) % len;
        if (mods.isShiftDown() && ctx.hasStepSelection()) ctx.selectSteps (ctx.stepA, next, ctx.lockMode);
        else ctx.selectSteps (next, next, ctx.lockMode);
        return true;
    }
    auto editSelected = [this] (const char* name, const std::function<void (Step&)>& fn)
    {
        if (! ctx.hasStepSelection()) return;
        TrackPattern p = ctx.tp();
        for (int i = ctx.selLo(); i <= ctx.selHi() && i < p.length; ++i) fn (p.steps[i]);
        Session::Transaction tx (ctx.sess, name);
        ctx.sess.setTrackPattern (ctx.pattern(), ctx.track, p);
    };
    if (code == juce::KeyPress::returnKey) { editSelected ("Toggle step", [] (Step& s) { s.on = ! s.on; }); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { editSelected ("Clear step", [] (Step& s) { s = Step{}; }); return true; }
    if (ch == '+' || ch == '=') { editSelected ("Velocity", [] (Step& s) { s.vel = (uint8_t) juce::jmin (127, (int) s.vel + 8); }); return true; }
    if (ch == '-' || ch == '_') { editSelected ("Velocity", [] (Step& s) { s.vel = (uint8_t) juce::jmax (1, (int) s.vel - 8); }); return true; }
    if (ch == 'r') { editSelected ("Ratchet", [] (Step& s) { static const uint8_t next[] = { 1, 2, 3, 4, 6, 8, 1 }; int i = 0; while (i < 6 && next[i] != s.ratCount) ++i; s.ratCount = next[i + 1 > 6 ? 0 : i + 1]; s.on = 1; }); return true; }
    if (ch == ',') { editSelected ("Pitch", [] (Step& s) { s.pitch = (int8_t) juce::jmax (-48, (int) s.pitch - 1); }); return true; }
    if (ch == '.') { editSelected ("Pitch", [] (Step& s) { s.pitch = (int8_t) juce::jmin (48, (int) s.pitch + 1); }); return true; }
    if (ch == 'm') { proc.setTrackMute (ctx.track, ! ctx.ts().mute); return true; }
    if (ch == 's') { ctx.sess.setTrackFlags (ctx.track, ctx.ts().mute != 0, ! ctx.ts().solo); return true; }
    if (ch == 'l') { ctx.sess.setTrackLocked (ctx.track, ! ctx.ts().locked); return true; }
    if (ch == 'e') { proc.setEvolve (! ctx.proj().evolveOn); return true; }
    if (ch == 'n') { view->top->newIdea (true); return true; }
    if (ch == 'a') { ctx.sess.previewNote (ctx.track, 0, 0.85f, true); juce::Timer::callAfterDelay (180, [this, t = ctx.track, safe = juce::Component::SafePointer<AutomataEditor> (this)] { if (safe != nullptr) ctx.sess.previewNote (t, 0, 0.f, false); }); return true; }
    if (ch == 'g')
    {
        TrackPattern p = ctx.tp();
        if (! p.gen.seedLocked) p.gen.seed = (uint32_t) juce::Random::getSystemRandom().nextInt (0x7fffffff);
        runGenerator (p, ctx.ts().role);
        Session::Transaction tx (ctx.sess, "Generate");
        ctx.sess.setTrackPattern (ctx.pattern(), ctx.track, p);
        ctx.status = juce::String ("Generated ") + genName (p.gen.type) + " on track " + juce::String (ctx.track + 1) + (p.gen.seedLocked ? "  (seed locked)" : "");
        return true;
    }
    if (ch == '[' || ch == ']')
    {
        const int np = juce::jlimit (0, kPatterns - 1, ctx.pattern() + (ch == ']' ? 1 : -1));
        ctx.sess.setEditPattern (np); ctx.sess.selectPattern (np, false); ctx.clearStepSelection();
        return true;
    }
    if (ch == 'f' || ch == 'b') return true;     // handled in keyStateChanged
    return false;
}

// ---------------------------------------------------------------------------- file drops
bool AutomataEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const juce::String e = juce::File (f).getFileExtension().toLowerCase();
        if (e == ".wav" || e == ".aif" || e == ".aiff" || e == ".flac" || e == ".ogg" || e == ".mp3" || e == ".scl" || e == ".automata") return true;
    }
    return false;
}

void AutomataEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (files.isEmpty()) return;
    const juce::File f (files[0]);
    const juce::String e = f.getFileExtension().toLowerCase();
    if (e == ".automata") { ctx.status = proc.loadProjectFile (f) ? "Loaded " + f.getFileName() : "Could not read " + f.getFileName(); ctx.clearStepSelection(); return; }
    if (e == ".scl")
    {
        Tuning t = ctx.proj().kit.tuning; std::string err;
        if (parseScala (f.loadFileAsString().toStdString(), t, err)) { ctx.sess.setTuning (t); ctx.status = "Loaded tuning " + juce::String (t.name); }
        else ctx.status = "Scala file: " + juce::String (err);
        return;
    }
    // audio: onto the track row under the mouse, else the selected track
    int track = ctx.track;
    const auto p = view->grid->getLocalPoint (this, juce::Point<int> (x, y));
    if (view->grid->getLocalBounds().contains (p)) track = juce::jlimit (0, kTracks - 1, p.y * kTracks / juce::jmax (1, view->grid->getHeight()));
    for (int i = 0; i < files.size() && track + i < kTracks; ++i) proc.loadSampleAsync (track + i, juce::File (files[i]));
    ctx.selectTrack (track);
    ctx.status = "Loading " + f.getFileName() + " on track " + juce::String (track + 1);
}

// ---------------------------------------------------------------------------- snapshots
void AutomataEditor::applySnapshotOptions (AutomataProcessor&, const juce::StringArray&) {}

void AutomataEditor::configureForSnapshot (const juce::StringArray& args)
{
    auto value = [&args] (const char* flag, int def)
    {
        const int i = args.indexOf (flag);
        return (i >= 0 && i + 1 < args.size()) ? args[i + 1].getIntValue() : def;
    };
    ctx.selectTrack (value ("--track", 1) - 1);
    ctx.setTab (value ("--tab", 0));
    if (args.contains ("--lock")) ctx.selectSteps (value ("--lock", 1) - 1, value ("--lock", 1) - 1, true);
    if (args.contains ("--size"))
    {
        const auto wh = juce::StringArray::fromTokens (args[args.indexOf ("--size") + 1], "x", "");
        if (wh.size() == 2) setSize (wh[0].getIntValue(), wh[1].getIntValue());
    }
    refreshNow(); refreshNow();
}
