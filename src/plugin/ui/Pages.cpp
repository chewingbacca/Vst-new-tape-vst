#include "Pages.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

//==============================================================================
GardenPage::GardenPage (UiContext& c)
    : visual (c, false),
      ctx (c),
      key (c, pKey),
      scale (c, pScale),
      freeze (c, pRevFreeze, Toggle::Style::button)
{
    addAndMakeVisible (visual);

    // The macro row is whatever the table lists in the "Macro" group.
    for (int i = 0; i < kNumParams; ++i)
        if (juce::String (kParams[i].group) == "Macro" && kParams[i].type != PType::Bool && kParams[i].type != PType::Choice)
            addAndMakeVisible (macros.add (new Knob (ctx, kParams[i].pid, Knob::Style::macro)));

    // A little colour progression across the row: leaf -> dew -> sun, Mix neutral.
    const juce::Colour tints[] = { col::blossom.interpolatedWith (col::leaf, 0.35f), col::leaf, col::dew,
                                   col::sun, juce::Colour (0xffd9c7a0), col::text.interpolatedWith (col::leaf, 0.4f) };
    for (int i = 0; i < macros.size(); ++i)
        macros[i]->setAccent (tints[i % int (std::size (tints))]);

    key.setAccent (col::leaf);
    scale.setAccent (col::leaf);
    freeze.setAccent (accentForGroup ("Reverb"));
    freeze.setGlyph (Toggle::Glyph::crystal);

    addAndMakeVisible (key);
    addAndMakeVisible (scale);
    addAndMakeVisible (freeze);
}

void GardenPage::refresh()
{
    const float hz = ctx.proc.engine.meters.pitchHz.load (std::memory_order_relaxed);
    juce::String text;

    if (std::isfinite (hz) && hz > 20.0f && hz < 8000.0f)
    {
        const float note = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const int nearest = juce::roundToInt (note);
        const int cents = juce::roundToInt ((note - float (nearest)) * 100.0f);
        text = juce::String (kKeyNames[((nearest % 12) + 12) % 12]) + juce::String (nearest / 12 - 1)
             + "  " + (cents >= 0 ? "+" : "") + juce::String (cents) + " ct";
    }

    if (text != pitchText)
    {
        pitchText = text;
        repaint (pitchArea.toNearestInt());
    }
}

void GardenPage::resized()
{
    auto b = getLocalBounds().toFloat();

    const float panelH = ctx.px (168.0f);
    panel = b.removeFromBottom (panelH);
    b.removeFromBottom (ctx.px (10.0f));
    visual.setBounds (b.toNearestInt());

    auto inner = panel.reduced (ctx.px (14.0f), ctx.px (9.0f));
    const float sideW = ctx.px (150.0f);
    auto left = inner.removeFromLeft (sideW);
    auto right = inner.removeFromRight (sideW);
    inner = inner.reduced (ctx.px (8.0f), 0.0f);

    // macros
    if (! macros.isEmpty())
    {
        const float cellW = inner.getWidth() / float (macros.size());
        for (int i = 0; i < macros.size(); ++i)
            macros[i]->setBounds (juce::Rectangle<float> (inner.getX() + cellW * float (i), inner.getY(), cellW, inner.getHeight()).toNearestInt());
    }

    // key / scale, stacked
    {
        const float cellH = ctx.px (64.0f);
        auto area = left.withSizeKeepingCentre (left.getWidth(), cellH * 2.0f + ctx.px (4.0f));
        key.setBounds (area.removeFromTop (cellH).toNearestInt());
        area.removeFromTop (ctx.px (4.0f));
        scale.setBounds (area.removeFromTop (cellH).toNearestInt());
    }

    // freeze + pitch read-out
    {
        const float buttonH = ctx.px (34.0f);
        const float readH = ctx.px (44.0f);
        auto area = right.withSizeKeepingCentre (right.getWidth() - ctx.px (10.0f), buttonH + readH + ctx.px (16.0f));
        freeze.setBounds (area.removeFromTop (buttonH).toNearestInt());
        area.removeFromTop (ctx.px (16.0f));
        pitchArea = area;
    }
}

void GardenPage::paint (juce::Graphics& g)
{
    drawPanel (g, panel, ctx.px (12.0f), ctx.scale);

    // hairline separators between the side columns and the macro row
    const float line = juce::jmax (1.0f, ctx.scale);
    g.setColour (col::hairline.withAlpha (0.08f));
    const float x0 = panel.getX() + ctx.px (14.0f + 150.0f + 4.0f);
    const float x1 = panel.getRight() - ctx.px (14.0f + 150.0f + 4.0f);
    g.fillRect (juce::Rectangle<float> (x0, panel.getY() + ctx.px (22.0f), line, panel.getHeight() - ctx.px (44.0f)));
    g.fillRect (juce::Rectangle<float> (x1, panel.getY() + ctx.px (22.0f), line, panel.getHeight() - ctx.px (44.0f)));

    // detected pitch
    auto area = pitchArea;
    const auto capFont = ctx.font (Type::small);
    g.setFont (capFont);
    g.setColour (col::muted.withAlpha (0.9f));
    g.drawText ("LISTENING", area.removeFromTop (capFont.getHeight() * 1.5f), juce::Justification::centred, false);

    g.setFont (ctx.font (Type::macroValue));
    g.setColour (pitchText.isEmpty() ? col::faint : col::dew);
    g.drawText (pitchText.isEmpty() ? juce::String ("no pitch yet") : pitchText, area.removeFromTop (ctx.font (Type::macroValue).getHeight() * 1.5f),
                juce::Justification::centred, false);
}

//==============================================================================
SoilPage::SoilPage (UiContext& c)
    : strip (c, true), ctx (c)
{
    addAndMakeVisible (strip);

    // One panel per group, in table order. Macros live on the Garden page, bypass in the header.
    juce::StringArray groups;
    for (int i = 0; i < kNumParams; ++i)
        groups.addIfNotAlreadyThere (kParams[i].group);
    groups.removeString ("Macro");

    for (const auto& group : groups)
    {
        auto* panel = panels.add (new ModulePanel (ctx, group, { int (pBypass) }));
        if (panel->getNumControls() == 0)
            panels.removeObject (panel);
        else
            addAndMakeVisible (panel);
    }
}

ModulePanel* SoilPage::find (const juce::String& group) const
{
    for (auto* p : panels)
        if (p->getGroup() == group)
            return p;
    return nullptr;
}

void SoilPage::resized()
{
    auto b = getLocalBounds().toFloat();
    const float gap = ctx.px (9.0f);

    strip.setBounds (b.removeFromTop (ctx.px (46.0f)).toNearestInt());
    b.removeFromTop (gap);

    // Signal-flow order in two rows; a column may stack several short panels.
    // Groups this layout does not know about are appended as extra columns.
    using Column = juce::Array<ModulePanel*>;
    juce::Array<Column> rows[2];
    juce::Array<ModulePanel*> placed;

    const auto addColumn = [&] (int row, std::initializer_list<const char*> names)
    {
        Column column;
        for (const char* name : names)
            if (auto* p = find (name))
            {
                column.add (p);
                placed.add (p);
            }
        if (! column.isEmpty())
            rows[row].add (column);
    };

    addColumn (0, { "Tape" });
    addColumn (0, { "Grain" });
    addColumn (0, { "Arp" });
    addColumn (1, { "Chorus" });
    addColumn (1, { "Reverb" });
    addColumn (1, { "Output", "Global" });

    for (auto* p : panels)
        if (! placed.contains (p))
            rows[rows[0].size() <= rows[1].size() ? 0 : 1].add (Column { p });

    const int numRows = (rows[0].isEmpty() ? 0 : 1) + (rows[1].isEmpty() ? 0 : 1);
    if (numRows == 0)
        return;

    // Row heights follow what the tallest column in each row needs (a stacked column
    // needs more than a single two-row panel).
    float needs[2] {};
    float needTotal = 0.0f;

    for (int ri = 0; ri < 2; ++ri)
    {
        for (auto& column : rows[ri])
        {
            float need = gap * float (column.size() - 1);
            for (auto* p : column)
            {
                p->setRows (column.size() > 1 ? 1 : 2);
                need += p->getDesignHeight() * ctx.scale;
            }
            needs[ri] = juce::jmax (needs[ri], need);
        }
        needTotal += needs[ri];
    }

    const float spare = b.getHeight() - gap * float (numRows - 1) - needTotal;
    float y = b.getY();

    for (int ri = 0; ri < 2; ++ri)
    {
        auto& row = rows[ri];
        if (row.isEmpty())
            continue;

        const float rowH = needs[ri] + spare * needs[ri] / juce::jmax (1.0f, needTotal);

        // Column widths follow what the panels inside would like.
        juce::Array<float> wants;
        float total = 0.0f;

        for (auto& column : row)
        {
            float want = 0.0f;
            for (auto* p : column)
                want = juce::jmax (want, p->getDesignWidth());
            wants.add (want);
            total += want;
        }

        const float available = b.getWidth() - gap * float (row.size() - 1);
        float x = b.getX();

        for (int ci = 0; ci < row.size(); ++ci)
        {
            auto& column = row.getReference (ci);
            const float w = available * wants[ci] / juce::jmax (1.0f, total);
            const float cellH = (rowH - gap * float (column.size() - 1)) / float (column.size());

            for (int pi = 0; pi < column.size(); ++pi)
                column[pi]->setBounds (juce::Rectangle<float> (x, y + (cellH + gap) * float (pi), w, cellH).toNearestInt());

            x += w + gap;
        }

        y += rowH + gap;
    }
}

} // namespace verdant::ui
