#include "DetailPanel.h"

namespace ui {

using namespace atm;

DetailPanel::DetailPanel (Ctx& c, StepGrid& grid) : ctx (c)
{
    static const char* const names[numTabs] = { "SOUND", "STEPS", "SEQ / GEN", "MOD", "FX", "SONG / SETUP" };
    static const char* const helps[numTabs] = {
        "Sound: engine, filter, envelopes and mix of the selected track (F1)",
        "Steps: velocity, pitch, probability, ratchets, micro-timing and parameter-lock lanes (F2)",
        "Sequencer and generators: length, speed, direction, swing, euclid / cellular / density / Markov / mutate (F3)",
        "Modulation: LFOs, step modulation, macros, chaos and the mod matrix (F4)",
        "Effects: track inserts, send buses and the master chain (F5)",
        "Song chain, tuning and project setup (F6)" };
    pages[TabSound] = makeSoundPage (ctx);
    pages[TabSteps] = makeStepsPage (ctx, grid);
    pages[TabSeq]   = makeSeqPage (ctx);
    pages[TabMod]   = makeModPage (ctx);
    pages[TabFx]    = makeFxPage (ctx);
    pages[TabSong]  = makeSongPage (ctx);
    for (int i = 0; i < numTabs; ++i)
    {
        tabs[i].text = names[i]; tabs[i].help = helps[i]; tabs[i].ctx = &ctx;
        tabs[i].onClick = [this, i] { ctx.setTab (i); };
        addAndMakeVisible (tabs[i]);
        addChildComponent (*pages[i]);
    }
    exitLock.colour = col::lock(); exitLock.ctx = &ctx;
    exitLock.help = "Leave lock mode: controls edit the track's base values again (Esc)";
    exitLock.onClick = [this] { ctx.lockMode = false; ++ctx.uiRev; };
    addChildComponent (exitLock);
    showTab();
}

void DetailPanel::showTab()
{
    const bool trackChanged = shownTrack != ctx.track;
    shownTab = ctx.tab; shownTrack = ctx.track;
    for (int i = 0; i < numTabs; ++i)
    {
        pages[i]->setVisible (i == shownTab);
        tabs[i].colour = col::track (ctx.track);
        tabs[i].setLit (i == shownTab);
        tabs[i].repaint();
    }
    juce::ignoreUnused (trackChanged);
    pages[shownTab]->rebuild();
    pages[shownTab]->tick (true);
    repaint();
}

void DetailPanel::tick (bool projectChanged)
{
    const uint64_t sr = ctx.sess.structureRevision();
    if (ctx.tab != shownTab || ctx.track != shownTrack || sr != structRev)
    {
        structRev = sr;
        showTab();
    }
    if (ctx.uiRev != uiRev)
    {
        uiRev = ctx.uiRev;
        exitLock.setVisible (ctx.lockMode);
        repaint (0, 0, kSideW, getHeight());
        projectChanged = true;
    }
    if (projectChanged) repaint (0, 0, kSideW, 60);
    pages[shownTab]->tick (projectChanged);
}

void DetailPanel::resized()
{
    auto r = getLocalBounds();
    auto side = r.removeFromLeft (kSideW).reduced (0, 0);
    int y = 62;
    for (int i = 0; i < numTabs; ++i) { tabs[i].setBounds (side.getX() + 8, y, kSideW - 20, 26); y += 29; }
    exitLock.setBounds (side.getX() + 8, getHeight() - 30, kSideW - 20, 24);
    for (auto& p : pages) p->setBounds (r);
}

void DetailPanel::paint (juce::Graphics& g)
{
    g.fillAll (col::bg());
    const juce::Colour tc = col::track (ctx.track);
    const TrackSound& ts = ctx.ts();
    juce::Rectangle<int> badge (8, 6, kSideW - 20, 50);
    fillPanel (g, badge, col::panel(), 3.f);
    g.setColour (tc); g.fillRect (badge.getX(), badge.getY(), 3, badge.getHeight());
    drawLabel (g, juce::String (ctx.track + 1), { badge.getX() + 8, badge.getY(), 34, badge.getHeight() }, tc, 30.f, juce::Justification::centred, true);
    drawLabel (g, juce::String (ts.name).toUpperCase(), { badge.getX() + 46, badge.getY() + 8, badge.getWidth() - 50, 16 }, col::text(), 12.f, juce::Justification::centredLeft, true);
    drawLabel (g, juce::String (engineName (ts.engine)).toUpperCase() + " / " + juce::String (roleName (ts.role)).toUpperCase(),
               { badge.getX() + 46, badge.getY() + 27, badge.getWidth() - 50, 14 }, col::dim(), 9.5f, juce::Justification::centredLeft);
    if (ctx.lockMode && ctx.hasStepSelection())
    {
        juce::String s = "LOCK EDIT  STEP " + juce::String (ctx.selLo() + 1);
        if (ctx.selHi() != ctx.selLo()) s << "-" << juce::String (ctx.selHi() + 1);
        drawLabel (g, s, { 8, getHeight() - 48, kSideW - 20, 14 }, col::lock(), 10.f, juce::Justification::centred, true);
    }
    else if (ctx.hasStepSelection())
    {
        juce::String s = "STEP " + juce::String (ctx.selLo() + 1);
        if (ctx.selHi() != ctx.selLo()) s << "-" << juce::String (ctx.selHi() + 1);
        drawLabel (g, s + " SELECTED", { 8, getHeight() - 24, kSideW - 20, 14 }, col::dim(), 9.5f, juce::Justification::centred);
    }
}

} // namespace ui
