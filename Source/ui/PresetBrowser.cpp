#include "PresetBrowser.h"

#include "../PresetManager.h"

namespace swoon::ui
{

namespace
{
constexpr float kRowH = 46.0f;
constexpr float kCategoryH = 30.0f;
constexpr float kCorner = 18.0f;
constexpr const char* kAll = "All";
constexpr const char* kUser = "User";
constexpr float kTitleH = 62.0f;
constexpr float kFadeH = 22.0f;
constexpr int kFadeSteps = 32;

void styleField (juce::TextEditor& field, int maxChars)
{
    field.setMultiLine (false);
    field.setReturnKeyStartsNewLine (false);
    field.setPopupMenuEnabled (false);
    field.setSelectAllWhenFocused (true);
    field.setInputRestrictions (maxChars);
    field.setFont (sansFont (13.2f, 0, 0.02f));
    field.setIndents (0, 0);
    field.setJustification (juce::Justification::centredLeft);
}

void drawCross (juce::Graphics& g, juce::Point<float> c, float size, float thickness)
{
    g.drawLine (c.x - size, c.y - size, c.x + size, c.y + size, thickness);
    g.drawLine (c.x - size, c.y + size, c.x + size, c.y - size, thickness);
}
} // namespace

//==============================================================================
PresetBrowser::PresetBrowser (PresetManager& pm) : presets (pm)
{
    setWantsKeyboardFocus (true);
    setTitle ("Preset browser");

    styleField (nameField, 48);
    nameField.setTextToShowWhenEmpty (juce::String::fromUTF8 ("Save the current sound as\xe2\x80\xa6"), col::dusk);
    nameField.setTitle ("New preset name");
    nameField.addListener (this);
    addAndMakeVisible (nameField);

    styleField (searchField, 32);
    searchField.setFont (sansFont (12.6f, 0, 0.02f));
    searchField.setTextToShowWhenEmpty ("Search", col::dusk);
    searchField.setTitle ("Search presets");
    searchField.addListener (this);
    searchField.onKey = [this] (const juce::KeyPress& key)
    {
        // the arrows keep walking the list while the search field has the focus
        if (key == juce::KeyPress::upKey)   { stepSelection (-1); return true; }
        if (key == juce::KeyPress::downKey) { stepSelection (1);  return true; }
        return false;
    };
    addAndMakeVisible (searchField);

    // alpha ramp, opaque in the first row and clear in the last
    fadeMask = juce::Image (juce::Image::SingleChannel, 1, kFadeSteps, true, juce::SoftwareImageType());
    {
        juce::Image::BitmapData data (fadeMask, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < kFadeSteps; ++y)
        {
            const float t = 1.0f - (float) y / (float) (kFadeSteps - 1);
            *data.getPixelPointer (0, y) = (juce::uint8) juce::roundToInt (255.0f * t * t * (3.0f - 2.0f * t));
        }
    }

    presets.addChangeListener (this);
    refresh();
}

PresetBrowser::~PresetBrowser()
{
    presets.removeChangeListener (this);
    nameField.removeListener (this);
    searchField.removeListener (this);
}

void PresetBrowser::open()
{
    armedDeleteRow = -1;
    status.clear();
    searchField.setText ({}, false);
    searchText.clear();
    refresh();

    for (int i = 0; i < (int) rows.size(); ++i)
        if (rows[(size_t) i] == presets.getCurrentIndex())
            ensureRowVisible (i);

    setVisible (true);
    toFront (false);

    if (isShowing())
        grabKeyboardFocus();
}

void PresetBrowser::close()
{
    if (onClose != nullptr)
        onClose();
}

void PresetBrowser::setStatus (const juce::String& text, juce::Colour colour)
{
    status = text;
    statusColour = colour;
    repaint();
}

//==============================================================================
void PresetBrowser::refresh()
{
    const auto& entries = presets.getEntries();

    bool anyUser = false;
    for (const auto& e : entries)
        anyUser = anyUser || ! e.isFactory;

    categories.clear();
    categories.add (kAll);
    for (const auto& c : presets.getCategories())
        if (c != kUser)
            categories.add (c);
    if (anyUser)
        categories.add (kUser);      // only listed once there is something in it

    if (! categories.contains (selectedCategory))
        selectedCategory = kAll;

    categoryCounts.assign ((size_t) categories.size(), 0);
    rows.clear();
    totalEntries = (int) entries.size();

    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];

        // the search looks at name and note, and narrows whatever category is selected
        if (searchText.isNotEmpty() && ! e.name.containsIgnoreCase (searchText) && ! e.note.containsIgnoreCase (searchText))
            continue;

        const auto category = e.isFactory ? e.category : juce::String (kUser);

        ++categoryCounts[0];
        const int ci = categories.indexOf (category);
        if (ci > 0)
            ++categoryCounts[(size_t) ci];

        if (selectedCategory == kAll || selectedCategory == category)
            rows.push_back (i);
    }

    if (armedDeleteRow >= (int) rows.size())
        armedDeleteRow = -1;

    setScroll (scroll);
    repaint();
}

void PresetBrowser::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

float PresetBrowser::maxScroll() const
{
    return juce::jmax (0.0f, (float) rows.size() * kRowH - listArea.getHeight());
}

void PresetBrowser::setScroll (float newScroll)
{
    newScroll = juce::jlimit (0.0f, maxScroll(), newScroll);
    if (! juce::approximatelyEqual (newScroll, scroll))
    {
        scroll = newScroll;
        repaint();
    }
}

void PresetBrowser::ensureRowVisible (int row)
{
    const float top = (float) row * kRowH, bottom = top + kRowH;
    if (top < scroll)
        setScroll (top);
    else if (bottom > scroll + listArea.getHeight())
        setScroll (bottom - listArea.getHeight());
}

juce::Rectangle<float> PresetBrowser::rowBounds (int row) const
{
    return { listArea.getX(), listArea.getY() + (float) row * kRowH - scroll, listArea.getWidth() - 10.0f, kRowH };
}

juce::Rectangle<float> PresetBrowser::thumbBounds() const
{
    const float content = (float) rows.size() * kRowH;
    if (content <= listArea.getHeight() || content <= 0.0f)
        return {};

    const float h = juce::jmax (24.0f, listArea.getHeight() * listArea.getHeight() / content);
    const float y = listArea.getY() + (listArea.getHeight() - h) * (scroll / maxScroll());
    return { listArea.getRight() - 4.0f, y, 4.0f, h };
}

bool PresetBrowser::saveWouldReplace() const
{
    const auto name = juce::File::createLegalFileName (nameField.getText().trim());
    if (name.isEmpty())
        return false;

    for (const auto& e : presets.getEntries())
        if (! e.isFactory && e.name.equalsIgnoreCase (name))
            return true;

    return false;
}

//==============================================================================
void PresetBrowser::resized()
{
    const auto b = getLocalBounds().toFloat();
    panel = juce::Rectangle<float> (juce::jmin (680.0f, b.getWidth() - 40.0f), juce::jmin (524.0f, b.getHeight() - 36.0f))
                .withCentre (b.getCentre());

    auto inner = panel;
    auto titleRow = inner.removeFromTop (kTitleH);
    auto footer = inner.removeFromBottom (64.0f);

    closeBounds = juce::Rectangle<float> (28.0f, 28.0f).withCentre ({ titleRow.getRight() - 32.0f, titleRow.getCentreY() + 2.0f });
    searchBounds = juce::Rectangle<float> (196.0f, 30.0f).withCentre ({ closeBounds.getX() - 14.0f - 98.0f, closeBounds.getCentreY() });
    searchField.setBounds (searchBounds.withTrimmedLeft (31.0f).withTrimmedRight (26.0f).reduced (0.0f, 4.0f).toNearestInt());

    inner.removeFromTop (6.0f);
    inner.removeFromBottom (6.0f);
    categoryArea = inner.removeFromLeft (178.0f).withTrimmedLeft (16.0f).withTrimmedRight (10.0f);
    listArea = inner.withTrimmedLeft (10.0f).withTrimmedRight (14.0f);

    footer = footer.reduced (20.0f, 14.0f).translated (0.0f, -2.0f);
    saveBounds = footer.removeFromRight (96.0f);
    footer.removeFromRight (10.0f);
    fieldBounds = footer;
    nameField.setBounds (fieldBounds.reduced (16.0f, 5.0f).toNearestInt());

    setScroll (scroll);
}

PresetBrowser::Hit PresetBrowser::hitAt (juce::Point<float> p) const
{
    if (! panel.contains (p))
        return { Zone::outside, -1 };

    if (closeBounds.expanded (4.0f).contains (p))
        return { Zone::close, -1 };

    if (saveBounds.contains (p))
        return { Zone::save, -1 };

    if (searchText.isNotEmpty() && searchBounds.withTrimmedLeft (searchBounds.getWidth() - 28.0f).contains (p))
        return { Zone::searchClear, -1 };

    if (categoryArea.contains (p))
    {
        const int index = (int) ((p.y - categoryArea.getY()) / kCategoryH);
        if (index >= 0 && index < categories.size())
            return { Zone::category, index };
    }

    if (listArea.contains (p))
    {
        if (! thumbBounds().isEmpty() && p.x > listArea.getRight() - 9.0f)
            return { Zone::scrollbar, -1 };

        const int row = (int) ((p.y - listArea.getY() + scroll) / kRowH);
        if (row >= 0 && row < (int) rows.size() && rows[(size_t) row] < (int) presets.getEntries().size())
        {
            const auto& entry = presets.getEntries()[(size_t) rows[(size_t) row]];
            const auto rb = rowBounds (row);
            const float deleteW = armedDeleteRow == row ? 70.0f : 34.0f;

            if (! entry.isFactory && p.x > rb.getRight() - deleteW)
                return { Zone::rowDelete, row };

            return { Zone::row, row };
        }
    }

    return { Zone::panel, -1 };
}

void PresetBrowser::mouseMove (const juce::MouseEvent& e)
{
    const auto hit = hitAt (e.position);
    if (hit != hover)
    {
        hover = hit;

        if (armedDeleteRow >= 0 && ! (hit.zone == Zone::rowDelete && hit.index == armedDeleteRow))
            armedDeleteRow = -1;

        const bool clickable = hit.zone == Zone::close || hit.zone == Zone::category || hit.zone == Zone::row
                               || hit.zone == Zone::rowDelete || hit.zone == Zone::save || hit.zone == Zone::searchClear;
        setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void PresetBrowser::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    armedDeleteRow = -1;
    repaint();
}

void PresetBrowser::mouseDown (const juce::MouseEvent& e)
{
    pressed = e.mods.isPopupMenu() ? Hit {} : hitAt (e.position);

    if (isShowing())
        grabKeyboardFocus();

    if (pressed.zone == Zone::scrollbar)
    {
        const auto thumb = thumbBounds();
        if (! thumb.contains (thumb.getCentreX(), e.position.y))
            setScroll ((e.position.y - listArea.getY() - thumb.getHeight() * 0.5f) / juce::jmax (1.0f, listArea.getHeight() - thumb.getHeight()) * maxScroll());

        thumbGrabOffset = e.position.y - thumbBounds().getY();
    }

    repaint();
}

void PresetBrowser::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed.zone == Zone::scrollbar)
    {
        const auto thumb = thumbBounds();
        const float travel = juce::jmax (1.0f, listArea.getHeight() - thumb.getHeight());
        setScroll ((e.position.y - thumbGrabOffset - listArea.getY()) / travel * maxScroll());
    }
}

void PresetBrowser::mouseUp (const juce::MouseEvent& e)
{
    const auto hit = pressed;
    pressed = {};
    repaint();

    if (hit.zone == Zone::none || hit != hitAt (e.position))
        return;

    // the list may have been rebuilt between press and release
    if ((hit.zone == Zone::row || hit.zone == Zone::rowDelete) && hit.index >= (int) rows.size())
        return;

    switch (hit.zone)
    {
        case Zone::outside:
        case Zone::close:
            close();
            break;

        case Zone::category:
            selectedCategory = categories[hit.index];
            armedDeleteRow = -1;
            scroll = 0.0f;
            refresh();
            break;

        case Zone::row:
            armedDeleteRow = -1;
            status.clear();
            presets.loadPreset (rows[(size_t) hit.index]);
            refresh();
            break;

        case Zone::rowDelete:
            if (armedDeleteRow == hit.index)
            {
                const auto name = presets.getEntries()[(size_t) rows[(size_t) hit.index]].name;
                armedDeleteRow = -1;

                if (presets.deleteUserPreset (rows[(size_t) hit.index]))
                    setStatus (juce::String::fromUTF8 ("Deleted \xe2\x80\x9c") + name + juce::String::fromUTF8 ("\xe2\x80\x9d"), col::mist);
                else
                    setStatus (juce::String::fromUTF8 ("Couldn\xe2\x80\x99t delete that preset"), col::rose);

                refresh();
            }
            else
            {
                armedDeleteRow = hit.index;
            }
            break;

        case Zone::save:
            saveCurrent();
            break;

        case Zone::searchClear:
            searchField.setText ({}, false);
            searchChanged();
            break;

        case Zone::scrollbar:
        case Zone::panel:
        case Zone::none:
            break;
    }
}

void PresetBrowser::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    setScroll (scroll - wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f) * 220.0f);
}

bool PresetBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::returnKey)
    {
        close();
        return true;
    }

    if (key == juce::KeyPress::upKey)   { stepSelection (-1); return true; }
    if (key == juce::KeyPress::downKey) { stepSelection (1);  return true; }

    // start typing anywhere in the browser: it goes to the search field
    const auto character = key.getTextCharacter();
    if (character > ' ' && ! key.getModifiers().isCommandDown() && ! key.getModifiers().isCtrlDown() && ! key.getModifiers().isAltDown())
    {
        searchField.setText (searchField.getText() + juce::String::charToString (character), false);
        searchField.moveCaretToEnd();
        searchChanged();

        if (isShowing())
            searchField.grabKeyboardFocus();

        return true;
    }

    return false;
}

void PresetBrowser::searchChanged()
{
    const auto text = searchField.getText().trim();
    if (text != searchText)
    {
        searchText = text;
        armedDeleteRow = -1;
        scroll = 0.0f;
        refresh();

        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(size_t) i] == presets.getCurrentIndex())
                ensureRowVisible (i);
    }

    repaint();
}

void PresetBrowser::stepSelection (int delta)
{
    if (rows.empty())
        return;

    int position = delta > 0 ? -1 : (int) rows.size();
    for (int i = 0; i < (int) rows.size(); ++i)
        if (rows[(size_t) i] == presets.getCurrentIndex())
            position = i;

    position = juce::jlimit (0, (int) rows.size() - 1, position + delta);
    presets.loadPreset (rows[(size_t) position]);
    refresh();
    ensureRowVisible (position);
}

void PresetBrowser::saveCurrent()
{
    const auto name = nameField.getText().trim();

    if (name.isEmpty())
    {
        setStatus ("Type a name first", col::rose);
        nameField.grabKeyboardFocus();
        return;
    }

    if (presets.saveUserPreset (name))
    {
        nameField.setText ({}, false);
        if (selectedCategory != kAll)
            selectedCategory = kUser;

        refresh();
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(size_t) i] == presets.getCurrentIndex())
                ensureRowVisible (i);

        setStatus (juce::String::fromUTF8 ("Saved \xe2\x80\x9c") + presets.getCurrentName() + juce::String::fromUTF8 ("\xe2\x80\x9d"), col::seaglass);

        if (isShowing())
            grabKeyboardFocus();
    }
    else
    {
        setStatus (juce::String::fromUTF8 ("Couldn\xe2\x80\x99t save \xe2\x80\x94 check the name"), col::rose);
    }
}

void PresetBrowser::focusOfChildComponentChanged (FocusChangeType)
{
    repaint();   // the name field's outline follows its focus
}

void PresetBrowser::textEditorTextChanged (juce::TextEditor& field)
{
    if (&field == &searchField)
    {
        searchChanged();
        return;
    }

    status.clear();
    repaint();
}

void PresetBrowser::textEditorReturnKeyPressed (juce::TextEditor& field)
{
    if (&field != &searchField)
    {
        saveCurrent();
        return;
    }

    // Return in the search field takes the top match
    if (! rows.empty() && rows.front() != presets.getCurrentIndex())
    {
        presets.loadPreset (rows.front());
        refresh();
        ensureRowVisible (0);
    }
}

void PresetBrowser::textEditorEscapeKeyPressed (juce::TextEditor& field)
{
    if (field.isEmpty())
    {
        close();
        return;
    }

    field.setText ({}, false);

    if (&field == &searchField)
        searchChanged();

    status.clear();
    repaint();

    if (isShowing())
        grabKeyboardFocus();
}

//==============================================================================
void PresetBrowser::paintPanelBackground (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff211c40), panel.getX(), panel.getY(),
                                             juce::Colour (0xff151229), panel.getX(), panel.getBottom(), false));
    g.fillRoundedRectangle (panel, kCorner);

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle (panel, kCorner);
    g.reduceClipRegion (clip);
    fillGlow (g, { panel.getX() + 60.0f, panel.getY() - 10.0f }, 300.0f, col::lavender.withAlpha (0.15f));
    fillGlow (g, { panel.getRight() - 40.0f, panel.getBottom() + 20.0f }, 300.0f, col::rose.withAlpha (0.10f));
    g.restoreState();
}

void PresetBrowser::paint (juce::Graphics& g)
{
    // scrim
    g.fillAll (col::ink.withAlpha (0.72f));

    // panel
    fillGlow (g, panel.getCentre().translated (0.0f, 30.0f), panel.getWidth() * 0.75f, juce::Colours::black.withAlpha (0.55f));
    paintPanelBackground (g);

    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.2f), 0.0f, panel.getY(),
                                             juce::Colours::white.withAlpha (0.05f), 0.0f, panel.getBottom(), false));
    g.drawRoundedRectangle (panel.reduced (0.5f), kCorner, 1.0f);

    paintTitleRow (g);
    paintCategories (g);
    paintList (g);
    paintFooter (g);
}

void PresetBrowser::paintTitleRow (juce::Graphics& g)
{
    const auto titleRow = panel.withHeight (kTitleH);
    const auto titleFont = serifFont (32.0f);
    g.setColour (col::fog);
    g.setFont (titleFont);
    g.drawText ("Presets", titleRow.withTrimmedLeft (28.0f).translated (0.0f, 3.0f), juce::Justification::centredLeft, false);

    const float tx = titleRow.getX() + 28.0f + textWidth (titleFont, "Presets") + 16.0f;
    const juce::Rectangle<float> info (tx, titleRow.getY() + 9.0f, searchBounds.getX() - tx - 14.0f, titleRow.getHeight() - 6.0f);

    if (status.isNotEmpty())
    {
        g.setColour (statusColour);
        g.setFont (sansFont (11.6f, 1, 0.02f));
        g.drawText (status, info, juce::Justification::centredLeft, true);
    }
    else
    {
        // where the current preset sits in the whole library, and how much of it the filters show
        const int current = presets.getCurrentIndex();
        auto text = current >= 0 && current < totalEntries
                        ? juce::String (current + 1) + " / " + juce::String (totalEntries)
                        : juce::String (totalEntries) + (totalEntries == 1 ? " sound" : " sounds");

        if ((int) rows.size() != totalEntries)
            text << juce::String::fromUTF8 ("   \xc2\xb7   ") << (int) rows.size() << " shown";

        g.setColour (col::dusk.interpolatedWith (col::mist, 0.25f));
        g.setFont (sansFont (11.4f, 0, 0.04f));
        g.drawText (text, info, juce::Justification::centredLeft, true);
    }

    // search field: magnifier, text, and a clear cross while there is text
    {
        const float r = searchBounds.getHeight() * 0.5f;
        const bool focused = searchField.hasKeyboardFocus (true);

        g.setColour (col::ink.withAlpha (0.6f));
        g.fillRoundedRectangle (searchBounds, r);
        g.setColour (focused || searchText.isNotEmpty() ? col::lavender.withAlpha (0.7f) : juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (searchBounds.reduced (0.5f), r, 1.0f);

        const juce::Point<float> lens (searchBounds.getX() + 16.0f, searchBounds.getCentreY() - 0.8f);
        g.setColour (focused || searchText.isNotEmpty() ? col::fog : col::mist);
        g.drawEllipse (lens.x - 4.2f, lens.y - 4.2f, 8.4f, 8.4f, 1.3f);
        g.drawLine (lens.x + 3.2f, lens.y + 3.2f, lens.x + 6.6f, lens.y + 6.6f, 1.5f);

        if (searchText.isNotEmpty())
        {
            g.setColour (hover.zone == Zone::searchClear ? col::fog : col::dusk);
            drawCross (g, { searchBounds.getRight() - 15.0f, searchBounds.getCentreY() }, 3.2f, 1.3f);
        }
    }

    const bool over = hover.zone == Zone::close;
    g.setColour (juce::Colours::white.withAlpha (over ? 0.13f : 0.06f));
    g.fillEllipse (closeBounds);
    g.setColour (over ? col::fog : col::mist);
    drawCross (g, closeBounds.getCentre(), 4.2f, 1.4f);

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRect (panel.getX() + 20.0f, titleRow.getBottom(), panel.getWidth() - 40.0f, 1.0f);
}

void PresetBrowser::paintListFades (juce::Graphics& g)
{
    // Where the list is cut off, the panel is painted again through an alpha ramp, so rows
    // melt into exactly the background they scroll under. The fade shrinks to nothing at
    // either end of the list.
    const float top = kFadeH * juce::jlimit (0.0f, 1.0f, scroll / kFadeH);
    const float bottom = kFadeH * juce::jlimit (0.0f, 1.0f, (maxScroll() - scroll) / kFadeH);
    const auto area = listArea.withTrimmedRight (8.0f);      // the scrollbar keeps its full length

    const auto band = [&] (float height, bool atTop)
    {
        if (height < 1.0f)
            return;

        const auto transform = atTop
            ? juce::AffineTransform::scale (area.getWidth(), height / (float) kFadeSteps).translated (area.getX(), area.getY())
            : juce::AffineTransform::scale (area.getWidth(), -height / (float) kFadeSteps).translated (area.getX(), area.getBottom());

        g.saveState();
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.reduceClipRegion (atTop ? area.withHeight (height).getSmallestIntegerContainer()
                                  : area.withTop (area.getBottom() - height).getSmallestIntegerContainer());
        g.reduceClipRegion (fadeMask, transform);
        paintPanelBackground (g);
        g.restoreState();
    };

    band (top, true);
    band (bottom, false);
}

void PresetBrowser::paintCategories (juce::Graphics& g)
{
    for (int i = 0; i < categories.size(); ++i)
    {
        const juce::Rectangle<float> r (categoryArea.getX(), categoryArea.getY() + (float) i * kCategoryH, categoryArea.getWidth(), kCategoryH);
        if (r.getBottom() > categoryArea.getBottom() + 1.0f)
            break;

        const bool selected = categories[i] == selectedCategory;
        const bool hovered = hover.zone == Zone::category && hover.index == i;
        const auto pill = r.reduced (0.0f, 2.0f);

        if (selected)
        {
            g.setGradientFill (juce::ColourGradient (col::lavender.withAlpha (0.22f), pill.getX(), 0.0f,
                                                     col::rose.withAlpha (0.10f), pill.getRight(), 0.0f, false));
            g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        }
        else if (hovered)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        }

        const int count = categoryCounts[(size_t) i];
        auto text = pill.reduced (13.0f, 0.0f);

        g.setColour (selected ? col::fog.withAlpha (0.7f) : col::dusk);
        g.setFont (sansFont (10.4f, 0, 0.04f));
        g.drawText (juce::String (count), text.removeFromRight (24.0f), juce::Justification::centredRight, false);

        g.setColour (selected ? col::fog : (count == 0 ? col::dusk : hovered ? col::fog : col::mist));
        drawFitted (g, categories[i], text, juce::Justification::centredLeft, sansFont (12.6f, selected ? 1 : 0, 0.02f), 0.8f);
    }

    // divider between the columns
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (categoryArea.getRight() + 9.0f, categoryArea.getY() + 2.0f, 1.0f, categoryArea.getHeight() - 4.0f);
}

void PresetBrowser::paintList (juce::Graphics& g)
{
    const auto& entries = presets.getEntries();

    if (rows.empty())
    {
        const bool searching = searchText.isNotEmpty();
        g.setColour (col::dusk);
        g.setFont (serifFont (20.0f));
        g.drawText (searching ? "No matches" : selectedCategory == kUser ? "Nothing saved yet" : "Nothing here yet",
                    listArea.withTrimmedBottom (30.0f), juce::Justification::centred, false);

        if (searching || selectedCategory == kUser)
        {
            g.setFont (sansFont (11.4f, 0, 0.02f));
            g.drawText (searching ? (selectedCategory == kAll ? juce::String ("Nothing has that in its name or note.")
                                                              : "Nothing in " + selectedCategory + " has that in its name or note.")
                                  : juce::String ("Name the current sound below to keep it."),
                        listArea.withTrimmedTop (34.0f), juce::Justification::centred, false);
        }
        return;
    }

    g.saveState();
    g.reduceClipRegion (listArea.getSmallestIntegerContainer());

    const auto nameFont = sansFont (13.6f, 1, 0.02f);
    const auto noteFont = sansFont (11.3f, 0, 0.02f);
    const auto tagFont = sansFont (9.2f, 1, 0.2f);

    const int first = juce::jmax (0, (int) (scroll / kRowH));
    const int last = juce::jmin ((int) rows.size() - 1, (int) ((scroll + listArea.getHeight()) / kRowH));

    for (int row = first; row <= last; ++row)
    {
        const int entryIndex = rows[(size_t) row];
        if (entryIndex >= (int) entries.size())
            continue;   // stale row: a refresh is already on its way

        const auto& entry = entries[(size_t) entryIndex];
        const auto rb = rowBounds (row).reduced (0.0f, 2.0f);
        const bool current = entryIndex == presets.getCurrentIndex();
        const bool hovered = (hover.zone == Zone::row || hover.zone == Zone::rowDelete) && hover.index == row;

        if (current)
        {
            g.setGradientFill (juce::ColourGradient (col::rose.withAlpha (0.20f), rb.getX(), 0.0f,
                                                     col::lavender.withAlpha (0.07f), rb.getRight(), 0.0f, false));
            g.fillRoundedRectangle (rb, 10.0f);
            g.setColour (col::rose);
            g.fillRoundedRectangle (rb.getX() + 5.0f, rb.getCentreY() - 9.0f, 3.0f, 18.0f, 1.5f);
        }
        else if (hovered)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (rb, 10.0f);
        }

        auto content = rb.withTrimmedLeft (18.0f).withTrimmedRight (12.0f);

        // right side: delete affordance for user presets, category / USER tag
        if (! entry.isFactory && armedDeleteRow == row)
        {
            g.setColour (col::rose);
            g.setFont (sansFont (11.4f, 1, 0.02f));
            g.drawText ("Delete?", content.removeFromRight (58.0f), juce::Justification::centredRight, false);
        }
        else
        {
            if (! entry.isFactory && hovered)
            {
                const auto cross = content.removeFromRight (22.0f);
                const bool over = hover.zone == Zone::rowDelete;
                g.setColour (over ? col::rose : col::dusk);
                drawCross (g, cross.getCentre().translated (3.0f, 0.0f), 3.6f, 1.3f);
            }

            if (! entry.isFactory)
            {
                const float tw = textWidth (tagFont, "USER") + 14.0f;
                const auto tag = content.removeFromRight (tw).withSizeKeepingCentre (tw, 16.0f);
                g.setColour (col::lavender.withAlpha (0.75f));
                g.drawRoundedRectangle (tag.reduced (0.5f), 8.0f, 1.0f);
                g.setFont (tagFont);
                g.drawText ("USER", tag.translated (0.9f, 0.0f), juce::Justification::centred, false);
            }
            else if (selectedCategory == kAll && entry.category.isNotEmpty())
            {
                const auto text = entry.category.toUpperCase();
                g.setColour (col::dusk);
                g.setFont (tagFont);
                g.drawText (text, content.removeFromRight (textWidth (tagFont, text) + 6.0f), juce::Justification::centredRight, false);
            }
        }

        content.removeFromRight (8.0f);
        const auto note = entry.note.isNotEmpty() ? entry.note : (entry.isFactory ? juce::String() : juce::String ("Your preset"));

        g.setColour (current ? col::fog : col::fog.withAlpha (0.9f));
        g.setFont (nameFont);

        if (note.isEmpty())
        {
            g.drawText (entry.name, content, juce::Justification::centredLeft, true);
        }
        else
        {
            g.drawText (entry.name, content.withTrimmedTop (4.0f).withHeight (19.0f), juce::Justification::centredLeft, true);
            g.setColour (current ? col::mist : col::dusk.interpolatedWith (col::mist, 0.45f));
            g.setFont (noteFont);
            g.drawText (note, content.withTrimmedTop (22.0f).withHeight (16.0f), juce::Justification::centredLeft, true);
        }
    }

    g.restoreState();

    paintListFades (g);

    const auto thumb = thumbBounds();
    if (! thumb.isEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (thumb.withY (listArea.getY()).withHeight (listArea.getHeight()), 2.0f);
        g.setColour (juce::Colours::white.withAlpha (hover.zone == Zone::scrollbar || pressed.zone == Zone::scrollbar ? 0.4f : 0.2f));
        g.fillRoundedRectangle (thumb, 2.0f);
    }
}

void PresetBrowser::paintFooter (juce::Graphics& g)
{
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRect (panel.getX() + 20.0f, fieldBounds.getY() - 13.0f, panel.getWidth() - 40.0f, 1.0f);

    const float r = fieldBounds.getHeight() * 0.5f;
    const bool focused = nameField.hasKeyboardFocus (true);

    g.setColour (col::ink.withAlpha (0.6f));
    g.fillRoundedRectangle (fieldBounds, r);
    g.setColour (focused ? col::lavender.withAlpha (0.7f) : juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (fieldBounds.reduced (0.5f), r, 1.0f);

    const bool enabled = nameField.getText().trim().isNotEmpty();
    const bool over = hover.zone == Zone::save;

    g.setGradientFill (juce::ColourGradient (col::rose.withAlpha (enabled ? (over ? 0.95f : 0.8f) : 0.16f), saveBounds.getX(), 0.0f,
                                             col::lavender.withAlpha (enabled ? (over ? 0.95f : 0.8f) : 0.16f), saveBounds.getRight(), 0.0f, false));
    g.fillRoundedRectangle (saveBounds, r);

    g.setColour (enabled ? col::ink : col::mist);
    g.setFont (sansFont (12.2f, 2, 0.1f));
    g.drawText (saveWouldReplace() ? "REPLACE" : "SAVE", saveBounds.translated (0.6f, 0.0f), juce::Justification::centred, false);
}

} // namespace swoon::ui
