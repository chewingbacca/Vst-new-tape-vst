#include "MainView.h"
#include "TopBar.h"
#include "DetailPanel.h"

namespace ui {

namespace { constexpr int kGridH = 8 * 47, kStatusH = 20; }

MainView::MainView (Ctx& c) : ctx (c)
{
    top = std::make_unique<TopBar> (ctx);
    grid = std::make_unique<StepGrid> (ctx);
    detail = std::make_unique<DetailPanel> (ctx, *grid);
    addAndMakeVisible (*top);
    addAndMakeVisible (*grid);
    addAndMakeVisible (*detail);
}

MainView::~MainView() = default;

void MainView::resized()
{
    auto r = getLocalBounds();
    top->setBounds (r.removeFromTop (TopBar::kHeight));
    r.removeFromBottom (kStatusH);
    r.removeFromTop (6);
    grid->setBounds (r.removeFromTop (kGridH).withTrimmedLeft (8).withTrimmedRight (6));
    r.removeFromTop (2);
    detail->setBounds (r);
}

void MainView::tick (bool projectChanged)
{
    top->tick (projectChanged);
    grid->tick (projectChanged);
    detail->tick (projectChanged);
    const juce::String s = ctx.hoverHelp.isNotEmpty() ? ctx.hoverHelp : ctx.status;
    if (s != shownStatus) { shownStatus = s; repaint (0, getHeight() - kStatusH, getWidth(), kStatusH); }
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg());
    auto sb = getLocalBounds().removeFromBottom (kStatusH);
    g.setColour (col::panel()); g.fillRect (sb);
    g.setColour (col::line()); g.fillRect (sb.getX(), sb.getY(), sb.getWidth(), 1);
    drawLabel (g, shownStatus, sb.reduced (10, 0), ctx.hoverHelp.isNotEmpty() ? col::dim() : col::text(), 10.f, juce::Justification::centredLeft);
    drawLabel (g, "SPACE play  1-8 track  F1-F6 tabs  ARROWS step  ENTER toggle  CTRL+Z undo  F fill  B break  N new idea  G generate",
               sb.reduced (10, 0).withTrimmedRight (18), col::faint(), 9.f, juce::Justification::centredRight);
}

} // namespace ui
