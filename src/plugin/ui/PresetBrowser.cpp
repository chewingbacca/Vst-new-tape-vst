#include "PresetBrowser.h"

namespace verdant::ui
{

using PresetInfo = VerdantProcessor::PresetInfo;

//==============================================================================
PresetBar::PresetBar (UiContext& c) : ctx (c)
{
    name = ctx.proc.getCurrentPresetName();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("Preset");
}

PresetBar::~PresetBar()
{
    ctx.clearHover (this);
}

void PresetBar::refreshName()
{
    auto n = ctx.proc.getCurrentPresetName();
    if (n != name)
    {
        name = std::move (n);
        repaint();
    }
}

void PresetBar::step (int direction)
{
    const auto presets = ctx.proc.getPresetList();
    if (presets.isEmpty())
        return;

    int current = -1;
    for (int i = 0; i < presets.size(); ++i)
        if (presets.getReference (i).name == name)
        {
            current = i;
            break;
        }

    const int n = presets.size();
    const int next = current < 0 ? (direction > 0 ? 0 : n - 1) : (current + direction + n) % n;

    ctx.proc.loadPreset (presets.getReference (next));
    refreshName();

    if (onPresetChanged)
        onPresetChanged();
}

int PresetBar::zoneAt (juce::Point<float> p) const
{
    const auto b = getLocalBounds().toFloat();
    if (! b.contains (p))
        return 2;

    const float edge = b.getHeight() * 1.15f;
    if (p.x < b.getX() + edge)       return -1;
    if (p.x > b.getRight() - edge)   return 1;
    return 0;
}

void PresetBar::mouseMove (const juce::MouseEvent& e)
{
    const int z = zoneAt (e.position);
    if (z == hoverZone)
        return;

    hoverZone = z;

    switch (z)
    {
        case -1: ctx.setHover (this, "Previous preset", nullptr, "Load the previous preset in the list"); break;
        case 1:  ctx.setHover (this, "Next preset", nullptr, "Load the next preset in the list"); break;
        case 0:  ctx.setHover (this, "Presets", nullptr, "Open the preset browser: factory presets by category, plus your own"); break;
        default: ctx.clearHover (this); break;
    }

    repaint();
}

void PresetBar::mouseExit (const juce::MouseEvent&)
{
    hoverZone = 2;
    ctx.clearHover (this);
    repaint();
}

void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    switch (zoneAt (e.position))
    {
        case -1: step (-1); break;
        case 1:  step (1);  break;
        case 0:
            if (onToggleBrowser)
                onToggleBrowser();
            break;
        default: break;
    }
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float radius = b.getHeight() * 0.5f;

    drawWell (g, b, radius, ctx.scale, browserOpen || hoverZone == 0, col::leaf);

    const float edge = b.getHeight() * 1.15f;
    const float chev = b.getHeight() * 0.36f;
    const float thick = juce::jmax (1.4f, ctx.px (1.6f));

    drawChevron (g, b.withWidth (edge).withSizeKeepingCentre (chev, chev), false,
                 hoverZone == -1 ? col::leaf : col::muted, thick);
    drawChevron (g, b.withLeft (b.getRight() - edge).withSizeKeepingCentre (chev, chev), true,
                 hoverZone == 1 ? col::leaf : col::muted, thick);

    // hairline separators
    const float line = juce::jmax (1.0f, ctx.scale);
    g.setColour (col::hairline.withAlpha (0.10f));
    g.fillRect (juce::Rectangle<float> (b.getX() + edge, b.getY() + b.getHeight() * 0.25f, line, b.getHeight() * 0.5f));
    g.fillRect (juce::Rectangle<float> (b.getRight() - edge - line, b.getY() + b.getHeight() * 0.25f, line, b.getHeight() * 0.5f));

    auto textArea = b.withTrimmedLeft (edge + ctx.px (6.0f)).withTrimmedRight (edge + ctx.px (6.0f));
    const auto font = ctx.font (Type::title);
    const float textW = juce::jmin (textArea.getWidth() - ctx.px (20.0f), juce::GlyphArrangement::getStringWidth (font, name));
    const float glyph = ctx.px (11.0f);
    const float groupW = textW + glyph + ctx.px (8.0f);
    float x = textArea.getCentreX() - groupW * 0.5f;

    drawLeafGlyph (g, { x, textArea.getCentreY() - glyph * 0.5f, glyph, glyph }, col::leaf.withAlpha (browserOpen ? 1.0f : 0.8f));
    x += glyph + ctx.px (8.0f);

    g.setFont (font);
    g.setColour (hoverZone == 0 || browserOpen ? col::text.brighter (0.2f) : col::text);
    g.drawText (name, juce::Rectangle<float> (x, textArea.getY(), textW + ctx.px (2.0f), textArea.getHeight()),
                juce::Justification::centredLeft, true);
}

//==============================================================================
class PresetBrowser::List final : public juce::Component
{
public:
    List (UiContext& c, PresetBrowser& o) : ctx (c), owner (o) {}

    ~List() override
    {
        ctx.clearHover (this);
    }

    void setPresets (juce::Array<PresetInfo> newPresets, const juce::String& currentName)
    {
        presets = std::move (newPresets);
        current = currentName;
        hoverItem = armedDelete = -1;
        rebuild();
    }

    void setCurrent (const juce::String& currentName)
    {
        if (current != currentName)
        {
            current = currentName;
            repaint();
        }
    }

    int getNumPresets() const noexcept  { return presets.size(); }

    const PresetInfo* findUser (const juce::String& name) const
    {
        for (const auto& p : presets)
            if (! p.factory && p.name == name)
                return &p;
        return nullptr;
    }

    juce::Rectangle<int> boundsOf (const juce::String& name, bool preferUser) const
    {
        juce::Rectangle<int> found;
        for (const auto& item : items)
            if (presets.getReference (item.index).name == name)
            {
                found = item.bounds.toNearestInt();
                if (presets.getReference (item.index).factory != preferUser)
                    break;
            }
        return found;
    }

    // Lays the rows out for the given width and resizes this component to fit.
    void rebuild()
    {
        items.clear();
        headers.clear();

        const float width = float (juce::jmax (40, getWidth()));
        const float headerH = ctx.px (32.0f);
        const float cellH = ctx.px (47.0f);
        const float gap = ctx.px (6.0f);
        const int columns = width > ctx.px (420.0f) ? 2 : 1;
        const float cellW = (width - gap * float (columns - 1)) / float (columns);

        juce::StringArray categories;
        for (const auto& p : presets)
            categories.addIfNotAlreadyThere (p.category);

        float y = 0.0f;

        for (const auto& cat : categories)
        {
            int count = 0;
            for (const auto& p : presets)
                count += p.category == cat ? 1 : 0;

            headers.add ({ cat, count, { 0.0f, y, width, headerH } });
            y += headerH;

            int col = 0;
            for (int i = 0; i < presets.size(); ++i)
            {
                if (presets.getReference (i).category != cat)
                    continue;

                items.add ({ i, { float (col) * (cellW + gap), y, cellW, cellH } });

                if (++col == columns)
                {
                    col = 0;
                    y += cellH + gap;
                }
            }

            if (col != 0)
                y += cellH + gap;

            y += ctx.px (4.0f);
        }

        setSize (getWidth(), juce::jmax (1, juce::roundToInt (y)));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto clip = g.getClipBounds().toFloat();
        const float line = juce::jmax (1.0f, ctx.scale);
        const auto sectionFont = ctx.font (Type::section);
        const auto smallFont = ctx.font (Type::small);
        const auto nameFont = ctx.font (Type::title);
        const auto descFont = ctx.font (Type::label);

        for (const auto& h : headers)
        {
            if (! h.bounds.intersects (clip))
                continue;

            const auto accent = h.category == "User" ? col::sun : col::leaf;
            auto r = h.bounds.withTrimmedLeft (ctx.px (4.0f));
            const auto label = h.category.toUpperCase();
            const float w = juce::GlyphArrangement::getStringWidth (sectionFont, label);

            g.setFont (sectionFont);
            g.setColour (accent.withAlpha (0.9f));
            g.drawText (label, r.withTrimmedTop (ctx.px (6.0f)), juce::Justification::centredLeft, false);

            g.setFont (smallFont);
            g.setColour (col::faint);
            g.drawText (juce::String (h.count), r.withTrimmedLeft (w + ctx.px (8.0f)).withTrimmedTop (ctx.px (6.0f)),
                        juce::Justification::centredLeft, false);

            const float lx = r.getX() + w + ctx.px (30.0f);
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.22f), lx, 0.0f, accent.withAlpha (0.0f), r.getRight(), 0.0f, false));
            g.fillRect (juce::Rectangle<float> (lx, r.getCentreY() + ctx.px (3.0f), r.getRight() - lx, line));
        }

        for (int n = 0; n < items.size(); ++n)
        {
            const auto& item = items.getReference (n);
            if (! item.bounds.intersects (clip))
                continue;

            const auto& p = presets.getReference (item.index);
            const bool isCurrent = p.name == current;
            const bool hover = n == hoverItem;
            const auto r = item.bounds.reduced (line * 0.5f);
            const float radius = ctx.px (8.0f);

            if (isCurrent)
            {
                g.setGradientFill (juce::ColourGradient (col::leaf.withAlpha (0.17f), r.getX(), r.getY(),
                                                         col::leaf.withAlpha (0.08f), r.getX(), r.getBottom(), false));
                g.fillRoundedRectangle (r, radius);
                g.setColour (col::leaf.withAlpha (0.5f));
                g.drawRoundedRectangle (r, radius, line);
            }
            else
            {
                g.setColour (hover ? col::hairline.withAlpha (0.09f) : col::well.withAlpha (0.45f));
                g.fillRoundedRectangle (r, radius);
                g.setColour (col::hairline.withAlpha (hover ? 0.26f : 0.08f));
                g.drawRoundedRectangle (r, radius, line);
            }

            auto textArea = r.withTrimmedLeft (ctx.px (12.0f)).withTrimmedRight (ctx.px (10.0f));

            if (! p.factory)
            {
                const auto del = deleteZone (item.bounds);
                textArea.setRight (del.getX() - ctx.px (4.0f));

                if (n == armedDelete)
                {
                    g.setColour (col::blossom.withAlpha (0.2f));
                    g.fillRoundedRectangle (del, del.getHeight() * 0.5f);
                    g.setColour (col::blossom.withAlpha (0.8f));
                    g.drawRoundedRectangle (del, del.getHeight() * 0.5f, line);
                    g.setFont (smallFont);
                    g.setColour (col::blossom.brighter (0.2f));
                    g.drawText ("DELETE?", del, juce::Justification::centred, false);
                }
                else if (hover)
                {
                    const float s = del.getHeight() * 0.32f;
                    const auto x = del.withSizeKeepingCentre (s, s);
                    juce::Path cross;
                    cross.startNewSubPath (x.getTopLeft());   cross.lineTo (x.getBottomRight());
                    cross.startNewSubPath (x.getTopRight());  cross.lineTo (x.getBottomLeft());
                    g.setColour (hoverDelete ? col::blossom : col::muted);
                    g.strokePath (cross, juce::PathStrokeType (juce::jmax (1.3f, ctx.px (1.5f)), juce::PathStrokeType::curved,
                                                               juce::PathStrokeType::rounded));
                }
            }

            const float nameH = nameFont.getHeight() * 1.2f;
            const float descH = descFont.getHeight() * 1.2f;
            const float top = r.getCentreY() - (nameH + descH) * 0.5f;

            g.setFont (nameFont);
            g.setColour (isCurrent ? col::leaf.brighter (0.25f) : col::text);
            g.drawText (p.name, textArea.withY (top).withHeight (nameH), juce::Justification::centredLeft, true);

            g.setFont (descFont);
            g.setColour (col::muted.withAlpha (isCurrent ? 1.0f : 0.85f));
            g.drawText (p.description, textArea.withY (top + nameH).withHeight (descH), juce::Justification::centredLeft, true);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int n = itemAt (e.position);
        const bool overDelete = n >= 0 && ! presets.getReference (items.getReference (n).index).factory
                                && deleteZone (items.getReference (n).bounds).contains (e.position);

        if (n != hoverItem || overDelete != hoverDelete)
        {
            if (n != hoverItem || ! overDelete)
                armedDelete = -1;

            hoverItem = n;
            hoverDelete = overDelete;

            if (n >= 0)
            {
                const auto& p = presets.getReference (items.getReference (n).index);
                ctx.setHover (this, p.name, nullptr, overDelete ? juce::String ("Delete this user preset (click twice to confirm)")
                                                                : p.description);
            }
            else
            {
                ctx.clearHover (this);
            }

            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverItem = armedDelete = -1;
        hoverDelete = false;
        ctx.clearHover (this);
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int n = itemAt (e.position);
        if (n < 0 || e.mods.isPopupMenu())
            return;

        // Copy: loading or deleting may rebuild `presets`.
        const PresetInfo p = presets.getReference (items.getReference (n).index);

        if (! p.factory && deleteZone (items.getReference (n).bounds).contains (e.position))
        {
            if (armedDelete == n)
            {
                armedDelete = -1;
                owner.message = ctx.proc.deleteUserPreset (p) ? "Deleted \"" + p.name + "\"" : juce::String ("Could not delete the preset file");
                owner.refresh();
            }
            else
            {
                armedDelete = n;
                repaint();
            }
            return;
        }

        armedDelete = -1;
        ctx.proc.loadPreset (p);
        current = ctx.proc.getCurrentPresetName();
        repaint();

        if (owner.onPresetChanged)
            owner.onPresetChanged();
    }

private:
    struct Item   { int index; juce::Rectangle<float> bounds; };
    struct Header { juce::String category; int count; juce::Rectangle<float> bounds; };

    int itemAt (juce::Point<float> p) const
    {
        for (int i = 0; i < items.size(); ++i)
            if (items.getReference (i).bounds.contains (p))
                return i;
        return -1;
    }

    juce::Rectangle<float> deleteZone (juce::Rectangle<float> cell) const
    {
        const float h = ctx.px (22.0f);
        const float w = ctx.px (64.0f);
        return { cell.getRight() - w - ctx.px (8.0f), cell.getCentreY() - h * 0.5f, w, h };
    }

    UiContext& ctx;
    PresetBrowser& owner;
    juce::Array<PresetInfo> presets;
    juce::Array<Item> items;
    juce::Array<Header> headers;
    juce::String current;
    int hoverItem = -1, armedDelete = -1;
    bool hoverDelete = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (List)
};

//==============================================================================
PresetBrowser::PresetBrowser (UiContext& c)
    : ctx (c),
      saveButton (c, "Save"),
      closeButton (c, "Close", PillButton::Icon::close)
{
    setVisible (false);
    setWantsKeyboardFocus (true);

    list = std::make_unique<List> (ctx, *this);
    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setWantsKeyboardFocus (false);
    addAndMakeVisible (viewport);

    nameEditor.setMultiLine (false);
    nameEditor.setReturnKeyStartsNewLine (false);
    nameEditor.setInputRestrictions (48);
    nameEditor.setSelectAllWhenFocused (true);
    nameEditor.setPopupMenuEnabled (false);
    nameEditor.setTextToShowWhenEmpty ("Name your preset...", col::faint);
    nameEditor.onReturnKey = [this] { saveCurrent(); };
    nameEditor.onEscapeKey = [this] { close(); };
    nameEditor.onTextChange = [this]
    {
        const bool exists = list->findUser (juce::File::createLegalFileName (nameEditor.getText().trim())) != nullptr;
        saveButton.setText (exists ? "Replace" : "Save");
    };
    addAndMakeVisible (nameEditor);

    saveButton.setHelp ("Save preset", "Store the current settings as a user preset with the name on the left");
    saveButton.onClick = [this] { saveCurrent(); };
    addAndMakeVisible (saveButton);

    closeButton.setHelp ("Close", "Close the preset browser");
    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (closeButton);
}

PresetBrowser::~PresetBrowser()
{
    viewport.setViewedComponent (nullptr, false);
}

void PresetBrowser::setDebugExtraPresets (const juce::Array<PresetInfo>& extras)
{
    debugExtras = extras;
    if (isOpen())
        refresh();
}

void PresetBrowser::debugSaveAs (const juce::String& name)
{
    nameEditor.setText (name, true);
    saveCurrent();
}

void PresetBrowser::refresh()
{
    auto presets = ctx.proc.getPresetList();

    // Debug extras go before the user presets so "User" stays the last category.
    int insertAt = presets.size();
    for (int i = 0; i < presets.size(); ++i)
        if (! presets.getReference (i).factory)
        {
            insertAt = i;
            break;
        }

    for (int i = 0; i < debugExtras.size(); ++i)
        presets.insert (insertAt + i, debugExtras.getReference (i));

    list->setPresets (std::move (presets), ctx.proc.getCurrentPresetName());
    repaint();
}

void PresetBrowser::refreshCurrent()
{
    if (isOpen())
        list->setCurrent (ctx.proc.getCurrentPresetName());
}

void PresetBrowser::open()
{
    message.clear();
    refresh();
    setVisible (true);
    toFront (false);
    resized();

    const auto current = ctx.proc.getCurrentPresetName();
    nameEditor.setText (list->findUser (current) != nullptr ? current : juce::String(), true);

    const auto target = list->boundsOf (current, false);
    if (! target.isEmpty())
        viewport.setViewPosition (0, juce::jmax (0, target.getCentreY() - viewport.getMaximumVisibleHeight() / 2));

    if (isShowing())
        grabKeyboardFocus();
}

void PresetBrowser::close()
{
    if (! isVisible())
        return;

    setVisible (false);

    if (onClose)
        onClose();
}

void PresetBrowser::saveCurrent()
{
    const auto name = nameEditor.getText().trim();

    if (name.isEmpty())
    {
        message = "Type a name first";
        repaint();
        return;
    }

    if (ctx.proc.saveUserPreset (name))
    {
        const auto saved = ctx.proc.getCurrentPresetName();
        message = "Saved \"" + saved + "\"";
        refresh();

        const auto target = list->boundsOf (saved, true);
        if (! target.isEmpty())
            viewport.setViewPosition (0, juce::jmax (0, target.getBottom() - viewport.getMaximumVisibleHeight() + ctx.pxi (8.0f)));

        saveButton.setText ("Replace");

        if (onPresetChanged)
            onPresetChanged();
    }
    else
    {
        message = "Could not write the preset file";
    }

    repaint();
}

juce::Rectangle<float> PresetBrowser::panelBounds() const
{
    const auto b = getLocalBounds().toFloat();
    const float w = juce::jmin (b.getWidth() - ctx.px (40.0f), ctx.px (740.0f));
    const float h = b.getHeight() - ctx.px (26.0f);
    return { b.getCentreX() - w * 0.5f, b.getY() + ctx.px (8.0f), w, h };
}

void PresetBrowser::resized()
{
    auto p = panelBounds();
    auto header = p.removeFromTop (ctx.px (48.0f));
    auto footer = p.removeFromBottom (ctx.px (58.0f));

    const float cb = ctx.px (24.0f);
    closeButton.setBounds (juce::Rectangle<float> (header.getRight() - cb - ctx.px (14.0f), header.getCentreY() - cb * 0.5f, cb, cb).toNearestInt());

    auto listArea = p.withTrimmedLeft (ctx.px (16.0f)).withTrimmedRight (ctx.px (6.0f)).withTrimmedTop (ctx.px (2.0f)).withTrimmedBottom (ctx.px (4.0f));
    const int bar = juce::jmax (8, ctx.pxi (10.0f));
    viewport.setScrollBarThickness (bar);
    viewport.setBounds (listArea.toNearestInt());

    const int listWidth = juce::jmax (40, viewport.getWidth() - bar - ctx.pxi (6.0f));
    if (list->getWidth() != listWidth)
        list->setSize (listWidth, juce::jmax (1, list->getHeight()));
    list->rebuild();

    auto row = footer.withTrimmedLeft (ctx.px (18.0f)).withTrimmedRight (ctx.px (18.0f)).withSizeKeepingCentre (footer.getWidth() - ctx.px (36.0f), ctx.px (30.0f));
    const auto font = ctx.font (Type::body);
    nameEditor.setFont (font);
    nameEditor.applyFontToAllText (font);
    nameEditor.setIndents (ctx.pxi (13.0f), juce::jmax (0, juce::roundToInt ((row.getHeight() - font.getHeight()) * 0.5f)));
    nameEditor.setBounds (row.removeFromLeft (ctx.px (270.0f)).toNearestInt());
    row.removeFromLeft (ctx.px (10.0f));
    saveButton.setBounds (row.removeFromLeft (ctx.px (92.0f)).toNearestInt());
}

void PresetBrowser::mouseDown (const juce::MouseEvent& e)
{
    if (! panelBounds().contains (e.position))
        close();
}

bool PresetBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }

    return false;
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff060e0a).withAlpha (0.62f));

    const auto p = panelBounds();
    const float radius = ctx.px (14.0f);
    const float line = juce::jmax (1.0f, ctx.scale);

    drawSoftShadow (g, p, radius, ctx.px (22.0f), 0.42f);
    drawPanel (g, p, radius, ctx.scale, juce::Colour (0xff12281e), 0.2f);

    auto header = p.withHeight (ctx.px (48.0f));
    const float glyph = ctx.px (14.0f);
    drawLeafGlyph (g, { header.getX() + ctx.px (20.0f), header.getCentreY() - glyph * 0.5f, glyph, glyph }, col::leaf);

    const auto sectionFont = ctx.font (Type::section);
    g.setFont (sectionFont);
    g.setColour (col::text);
    const float tx = header.getX() + ctx.px (20.0f) + glyph + ctx.px (10.0f);
    g.drawText ("PRESETS", juce::Rectangle<float> (tx, header.getY(), ctx.px (120.0f), header.getHeight()), juce::Justification::centredLeft, false);

    const float tw = juce::GlyphArrangement::getStringWidth (sectionFont, "PRESETS");
    g.setFont (ctx.font (Type::label));
    g.setColour (col::muted);
    g.drawText (juce::String (list->getNumPresets()) + " in the garden",
                juce::Rectangle<float> (tx + tw + ctx.px (14.0f), header.getY(), ctx.px (240.0f), header.getHeight()),
                juce::Justification::centredLeft, false);

    g.setColour (col::hairline.withAlpha (0.10f));
    g.fillRect (juce::Rectangle<float> (p.getX() + ctx.px (16.0f), header.getBottom() - line, p.getWidth() - ctx.px (32.0f), line));

    auto footer = p.withTop (p.getBottom() - ctx.px (58.0f));
    g.fillRect (juce::Rectangle<float> (p.getX() + ctx.px (16.0f), footer.getY(), p.getWidth() - ctx.px (32.0f), line));

    const float msgX = float (saveButton.getRight()) + ctx.px (14.0f);
    g.setFont (ctx.font (Type::label));
    g.setColour (message.isNotEmpty() ? col::sun : col::faint.brighter (0.2f));
    g.drawText (message.isNotEmpty() ? message : juce::String ("Name it and save: your presets appear under User"),
                juce::Rectangle<float> (msgX, footer.getY(), p.getRight() - msgX - ctx.px (18.0f), footer.getHeight()),
                juce::Justification::centredLeft, true);
}

} // namespace verdant::ui
