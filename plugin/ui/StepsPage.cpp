// Placeholder page.
#include "DetailPanel.h"

namespace ui {

namespace {
class StepsPage : public Page
{
public:
    using Page::Page;
    void tick (bool) override {}
    void paint (juce::Graphics& g) override { drawLabel (g, "Steps", getLocalBounds(), col::faint(), 20.f); }
};
}

std::unique_ptr<Page> makeStepsPage (Ctx& c, StepGrid&) { return std::make_unique<StepsPage> (c); }

} // namespace ui
