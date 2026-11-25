// The lower part of the editor: tabbed detail pages for the selected track.
#pragma once
#include "Widgets.h"
#include "StepGrid.h"

namespace ui {

class Page : public juce::Component
{
public:
    explicit Page (Ctx& c) : ctx (c) {}
    // Called ~30 times a second. projectChanged = the session revision moved since the last tick.
    virtual void tick (bool projectChanged) = 0;
    // Called when the selected track, the track's engine or an effect type changed: recreate controls.
    virtual void rebuild() {}
protected:
    Ctx& ctx;
};

std::unique_ptr<Page> makeSoundPage (Ctx&);
std::unique_ptr<Page> makeStepsPage (Ctx&, StepGrid&);
std::unique_ptr<Page> makeSeqPage (Ctx&);
std::unique_ptr<Page> makeModPage (Ctx&);
std::unique_ptr<Page> makeFxPage (Ctx&);
std::unique_ptr<Page> makeSongPage (Ctx&);

class DetailPanel : public juce::Component
{
public:
    DetailPanel (Ctx& c, StepGrid& grid);
    void tick (bool projectChanged);
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int kSideW = 204;
private:
    void showTab();
    Ctx& ctx;
    Btn tabs[numTabs];
    Btn exitLock { "EXIT LOCK MODE" };
    std::unique_ptr<Page> pages[numTabs];
    int shownTab = -1, shownTrack = -1;
    uint64_t structRev = 0;
    uint32_t uiRev = 0;
};

} // namespace ui
