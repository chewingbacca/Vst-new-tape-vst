#include "ModulePanel.h"

namespace verdant::ui
{

namespace
{
    constexpr float kHeaderHeight = 30.0f;
    constexpr float kPadX = 8.0f;
    constexpr float kPadBottom = 7.0f;
    constexpr float kCellHeight = 88.0f;    // caption + knob + value with a little air

    bool isModuleSwitch (const ParamDef& d)
    {
        return d.type == PType::Bool && juce::String (d.id).endsWith ("On");
    }
}

ModulePanel::ModulePanel (UiContext& c, const juce::String& groupName, const juce::Array<int>& exclude)
    : ctx (c), group (groupName), title (groupName), accent (accentForGroup (groupName))
{
    for (int i = 0; i < kNumParams; ++i)
    {
        const ParamDef& d = kParams[i];

        if (group != d.group || exclude.contains (i))
            continue;

        if (power == nullptr && isModuleSwitch (d))
        {
            power = std::make_unique<Toggle> (ctx, d.pid, Toggle::Style::power);
            power->setAccent (accent);
            power->onValueChange = [this] { repaint(); };
            title = d.name;
            addAndMakeVisible (*power);
            continue;
        }

        ParamControl* control = nullptr;

        switch (d.type)
        {
            case PType::Float:
            case PType::Int:     control = new Knob (ctx, d.pid, Knob::Style::compact); break;
            case PType::Choice:  control = new ChoicePill (ctx, d.pid); break;
            case PType::Bool:    control = new Toggle (ctx, d.pid, Toggle::Style::labelled); break;
        }

        if (control == nullptr)
            continue;

        controls.add (control);
        control->setAccent (accent);

        // "Tape Mix" inside the Tape panel is just "Mix".
        const juce::String name (d.name);
        if (name.startsWithIgnoreCase (group + " ") && name.length() > group.length() + 1)
            control->setCaption (name.substring (group.length() + 1));

        addAndMakeVisible (control);
    }
}

juce::Array<juce::Range<int>> ModulePanel::splitRows() const
{
    juce::Array<juce::Range<int>> result;
    const int n = controls.size();

    if (n == 0)
        return result;

    const int numRows = juce::jmin (rows, n);

    if (numRows <= 1)
    {
        result.add ({ 0, n });
        return result;
    }

    // Greedy balanced split (keeps table order): each row takes controls until it reaches
    // its share of the remaining width.
    float remaining = 0.0f;
    for (auto* c : controls)
        remaining += c->getDesignWidth();

    int start = 0;
    for (int r = 0; r < numRows; ++r)
    {
        const int rowsLeft = numRows - r;
        if (rowsLeft == 1)
        {
            result.add ({ start, n });
            break;
        }

        const float share = remaining / float (rowsLeft);
        float sum = 0.0f;
        int end = start;

        while (end < n - (rowsLeft - 1))
        {
            const float w = controls[end]->getDesignWidth();
            if (end > start && sum + w * 0.5f > share)
                break;
            sum += w;
            ++end;
        }

        result.add ({ start, end });
        remaining -= sum;
        start = end;
    }

    return result;
}

float ModulePanel::getDesignWidth() const
{
    float widest = 0.0f;

    for (const auto& row : splitRows())
    {
        float sum = 0.0f;
        for (int i = row.getStart(); i < row.getEnd(); ++i)
            sum += controls[i]->getDesignWidth();
        widest = juce::jmax (widest, sum);
    }

    return widest + kPadX * 2.0f;
}

float ModulePanel::getDesignHeight() const
{
    return kHeaderHeight + kPadBottom + kCellHeight * float (juce::jmax (1, juce::jmin (rows, controls.size())));
}

juce::Rectangle<float> ModulePanel::contentArea() const
{
    auto b = getLocalBounds().toFloat();
    b.removeFromTop (ctx.px (kHeaderHeight));
    return b;
}

void ModulePanel::resized()
{
    auto b = getLocalBounds().toFloat();
    auto header = b.removeFromTop (ctx.px (kHeaderHeight));

    if (power != nullptr)
    {
        const float w = ctx.px (27.0f), h = ctx.px (14.0f);
        power->setBounds (juce::Rectangle<float> (header.getX() + ctx.px (12.0f), header.getCentreY() - h * 0.5f + ctx.px (1.0f), w, h)
                              .expanded (ctx.px (3.0f)).toNearestInt());
    }

    auto content = b.withTrimmedLeft (ctx.px (kPadX)).withTrimmedRight (ctx.px (kPadX)).withTrimmedBottom (ctx.px (kPadBottom));
    const auto rowRanges = splitRows();

    if (rowRanges.isEmpty())
        return;

    // Rows keep a comfortable pitch and the block of rows is centred in the panel.
    const float rowH = juce::jmin (content.getHeight() / float (rowRanges.size()), ctx.px (kCellHeight * 1.2f));
    const float cellH = juce::jmin (rowH, ctx.px (112.0f));
    const float blockTop = content.getY() + (content.getHeight() - rowH * float (rowRanges.size())) * 0.5f;

    // One horizontal scale for the whole panel keeps the rhythm of the rows consistent.
    float widest = 1.0f;
    for (const auto& row : rowRanges)
    {
        float sum = 0.0f;
        for (int i = row.getStart(); i < row.getEnd(); ++i)
            sum += controls[i]->getDesignWidth();
        widest = juce::jmax (widest, sum);
    }

    const float k = juce::jmin (content.getWidth() / widest, ctx.scale * 1.45f);

    for (int r = 0; r < rowRanges.size(); ++r)
    {
        const auto row = rowRanges[r];
        float sum = 0.0f;
        for (int i = row.getStart(); i < row.getEnd(); ++i)
            sum += controls[i]->getDesignWidth();

        float x = content.getCentreX() - sum * k * 0.5f;
        const float y = blockTop + rowH * float (r) + (rowH - cellH) * 0.5f;

        for (int i = row.getStart(); i < row.getEnd(); ++i)
        {
            const float w = controls[i]->getDesignWidth() * k;
            controls[i]->setBounds (juce::Rectangle<float> (x, y, w, cellH).toNearestInt());
            x += w;
        }
    }
}

void ModulePanel::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float radius = ctx.px (10.0f);
    const bool active = isActive();

    drawPanel (g, b, radius, ctx.scale, active ? col::panel : col::panel.darker (0.12f), active ? 0.13f : 0.08f);

    auto header = b.withHeight (ctx.px (kHeaderHeight));
    float textX = header.getX() + ctx.px (14.0f);

    if (power != nullptr)
        textX = float (power->getRight()) + ctx.px (6.0f);
    else
    {
        // groups without a switch get a small leaf bullet
        const float s = ctx.px (11.0f);
        drawLeafGlyph (g, { textX, header.getCentreY() - s * 0.5f + ctx.px (1.0f), s, s }, accent.withAlpha (0.9f));
        textX += s + ctx.px (7.0f);
    }

    g.setFont (ctx.font (Type::section));
    g.setColour (active ? accent.interpolatedWith (col::text, 0.35f) : col::muted.withAlpha (0.75f));
    g.drawText (title.toUpperCase(), juce::Rectangle<float> (textX, header.getY() + ctx.px (1.0f), header.getRight() - textX - ctx.px (10.0f), header.getHeight()),
                juce::Justification::centredLeft, true);

    // hairline under the header
    const float line = juce::jmax (1.0f, ctx.scale);
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (active ? 0.30f : 0.10f), header.getX(), 0.0f,
                                             accent.withAlpha (0.0f), header.getRight(), 0.0f, false));
    g.fillRect (juce::Rectangle<float> (header.getX() + ctx.px (12.0f), header.getBottom() - line, header.getWidth() - ctx.px (24.0f), line));
}

void ModulePanel::paintOverChildren (juce::Graphics& g)
{
    if (isActive())
        return;

    // A veil over the controls of a switched-off module: still readable and usable, clearly asleep.
    const float line = juce::jmax (1.0f, ctx.scale);
    const auto area = contentArea().reduced (line * 1.5f).withTrimmedTop (line);
    g.setColour (col::panel.darker (0.12f).withAlpha (0.66f));
    g.fillRoundedRectangle (area, ctx.px (8.0f));
}

} // namespace verdant::ui
