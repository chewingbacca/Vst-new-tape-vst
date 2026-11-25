// The whole editor surface at its base size (the editor scales it).
#pragma once
#include "Widgets.h"
#include "StepGrid.h"

namespace ui {

class TopBar;
class DetailPanel;

class MainView : public juce::Component
{
public:
    explicit MainView (Ctx& c);
    ~MainView() override;
    void tick (bool projectChanged);
    void paint (juce::Graphics&) override;
    void resized() override;

    Ctx& ctx;
    std::unique_ptr<TopBar> top;
    std::unique_ptr<StepGrid> grid;
    std::unique_ptr<DetailPanel> detail;
private:
    juce::String shownStatus;
    uint64_t lastStructRev = 0;
};

} // namespace ui
