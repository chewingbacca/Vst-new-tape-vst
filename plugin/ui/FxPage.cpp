// Placeholder page.
#include "DetailPanel.h"

namespace ui {

namespace {
class FxPage : public Page
{
public:
    using Page::Page;
    void tick (bool) override {}
    void paint (juce::Graphics& g) override { drawLabel (g, "Fx", getLocalBounds(), col::faint(), 20.f); }
};
}

std::unique_ptr<Page> makeFxPage (Ctx& c) { return std::make_unique<FxPage> (c); }

} // namespace ui
