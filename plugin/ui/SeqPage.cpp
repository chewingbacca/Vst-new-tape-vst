// Placeholder page.
#include "DetailPanel.h"

namespace ui {

namespace {
class SeqPage : public Page
{
public:
    using Page::Page;
    void tick (bool) override {}
    void paint (juce::Graphics& g) override { drawLabel (g, "Seq", getLocalBounds(), col::faint(), 20.f); }
};
}

std::unique_ptr<Page> makeSeqPage (Ctx& c) { return std::make_unique<SeqPage> (c); }

} // namespace ui
