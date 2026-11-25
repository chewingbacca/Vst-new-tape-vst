// Placeholder page.
#include "DetailPanel.h"

namespace ui {

namespace {
class SongPage : public Page
{
public:
    using Page::Page;
    void tick (bool) override {}
    void paint (juce::Graphics& g) override { drawLabel (g, "Song", getLocalBounds(), col::faint(), 20.f); }
};
}

std::unique_ptr<Page> makeSongPage (Ctx& c) { return std::make_unique<SongPage> (c); }

} // namespace ui
