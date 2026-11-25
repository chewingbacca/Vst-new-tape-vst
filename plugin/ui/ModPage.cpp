// Placeholder page.
#include "DetailPanel.h"

namespace ui {

namespace {
class ModPage : public Page
{
public:
    using Page::Page;
    void tick (bool) override {}
    void paint (juce::Graphics& g) override { drawLabel (g, "Mod", getLocalBounds(), col::faint(), 20.f); }
};
}

std::unique_ptr<Page> makeModPage (Ctx& c) { return std::make_unique<ModPage> (c); }

} // namespace ui
