// Custom-drawn controls. Every parameter control is bound to a ParamRef and
// reads/writes through ui::Ctx, so parameter locks, modulation display, host
// gestures and undo behave the same everywhere.
#pragma once
#include "UiContext.h"

namespace ui {

class ParamWidget : public juce::Component,
                    public juce::DragAndDropTarget
{
public:
    ParamWidget (Ctx& c, ParamRef r) : ctx (c), ref (r) {}
    // Re-read state; repaint if anything visible changed. Called from the editor timer.
    virtual void sync();
    void setAccent (juce::Colour c) { accent = c; hasAccent = true; repaint(); }
    void setCaption (const juce::String& s) { caption = s; repaint(); }
    juce::Colour accentColour() const;
    juce::String captionText() const { return caption.isNotEmpty() ? caption : juce::String (ctx.desc (ref).name); }

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("modsrc:"); }
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails& d) override;

    Ctx& ctx;
    ParamRef ref;

protected:
    void showContextMenu();
    void beginEdit();
    void endEdit();
    float shown = 0.f, live = 0.f, modDepth = 0.f;
    bool isLock = false, anyLock = false, hover = false, dragging = false, dropHover = false, hasAccent = false;
    juce::Colour accent;
    juce::String caption;
};

// Rotary control with value arc, lock/modulation display and a caption that turns into the value while in use.
class Knob : public ParamWidget
{
public:
    using ParamWidget::ParamWidget;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool big = false;
private:
    float dragStart = 0.f;
};

// Discrete parameter with named options: shows the current one, click for a menu, drag/wheel to step.
class Choice : public ParamWidget
{
public:
    using ParamWidget::ParamWidget;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool showCaption = true;
};

// Two-state parameter.
class Toggle : public ParamWidget
{
public:
    using ParamWidget::ParamWidget;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
};

// Creates the right control for a parameter (knob / choice / toggle). Returns nullptr for unused slots.
std::unique_ptr<ParamWidget> makeParamWidget (Ctx& ctx, ParamRef ref);

// ---------------------------------------------------------------------------
// Flat text button with an optional lit state.
class Btn : public juce::Component
{
public:
    explicit Btn (const juce::String& t = {}) : text (t) {}
    std::function<void()> onClick;
    std::function<void (bool)> onPressChange;       // momentary use
    std::function<void (const juce::MouseEvent&)> onRightClick;
    std::function<void()> onDragStart;              // fired once when the mouse is dragged away (for drag-out export)
    juce::String text, help;
    juce::Colour colour = col::accent();
    bool lit = false, enabled = true, bold = true, flash = false;
    float textHeight = 10.5f;
    Ctx* ctx = nullptr;
    void setLit (bool b) { if (b != lit) { lit = b; repaint(); } }
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { over = true; if (ctx) ctx->hoverHelp = help; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; if (ctx) ctx->hoverHelp = {}; repaint(); }
private:
    bool over = false, down = false, dragFired = false;
};

// A draggable chip representing a modulation source; drop it on any control.
class ModChip : public juce::Component
{
public:
    ModChip (Ctx& c, int source) : ctx (c), src (source) {}
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { ctx.hoverHelp = juce::String ("Drag ") + atm::srcName (src) + " onto any control to modulate it"; }
    void mouseExit (const juce::MouseEvent&) override { ctx.hoverHelp = {}; }
    void sync();
    Ctx& ctx;
    int src;
private:
    float value = 0.f;
};

// A number box: drag vertically or wheel; used for seeds, lengths, tempo etc.
class NumberBox : public juce::Component
{
public:
    std::function<double()> get;
    std::function<void (double)> set;
    std::function<juce::String (double)> format;
    std::function<void()> onGestureStart, onGestureEnd;
    double minV = 0, maxV = 100, step = 1, defaultV = 0;
    double pixelsPerStep = 4.0;
    juce::String caption, help;
    juce::Colour colour = col::text();
    Ctx* ctx = nullptr;
    void sync() { const double v = get ? get() : 0.0; if (v != last) { last = v; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (set) { if (onGestureStart) onGestureStart(); set (defaultV); if (onGestureEnd) onGestureEnd(); } }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { if (ctx) ctx->hoverHelp = help; }
    void mouseExit (const juce::MouseEvent&) override { if (ctx) ctx->hoverHelp = {}; }
private:
    double last = -1.0e30, dragStartV = 0;
};

// A menu-style selector not bound to a parameter (engine, effect type, scale ...).
class Picker : public juce::Component
{
public:
    std::function<int()> get;
    std::function<void (int)> set;
    std::function<juce::StringArray()> items;
    juce::String caption, help;
    juce::Colour colour = col::text();
    Ctx* ctx = nullptr;
    void sync() { const int v = get ? get() : 0; if (v != last) { last = v; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { if (ctx) ctx->hoverHelp = help; }
    void mouseExit (const juce::MouseEvent&) override { if (ctx) ctx->hoverHelp = {}; }
private:
    int last = -999;
};

// A rotary bound to arbitrary getters/setters (generator params, mod amounts, step fields).
class ValueKnob : public juce::Component
{
public:
    std::function<float()> get;                    // 0..1
    std::function<void (float)> set;               // 0..1
    std::function<juce::String (float)> format;
    std::function<void()> onGestureStart, onGestureEnd;
    juce::String caption, help;
    juce::Colour colour = col::accent();
    float defaultValue = 0.f;
    bool bipolar = false;
    int steps = 0;                                 // > 0: snap to this many positions
    Ctx* ctx = nullptr;
    void sync() { const float v = get ? get() : 0.f; if (v != last) { last = v; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { over = true; showHelp(); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; if (ctx) ctx->hoverHelp = {}; repaint(); }
private:
    void showHelp() { if (ctx) ctx->hoverHelp = caption + "  " + (format ? format (get ? get() : 0.f) : juce::String()) + "   " + help; }
    float snap (float v) const { v = juce::jlimit (0.f, 1.f, v); return steps > 1 ? std::round (v * (float) (steps - 1)) / (float) (steps - 1) : v; }
    float last = -1.f, dragStart = 0.f;
    bool over = false, dragging = false;
};

// Section header + flow layout of parameter widgets.
class ParamGroup : public juce::Component
{
public:
    ParamGroup (Ctx& c, const juce::String& title_) : ctx (c), title (title_) {}
    ParamWidget* add (ParamRef r, const juce::String& captionOverride = {});
    void addCustom (juce::Component* c, int width) { customs.push_back ({ c, width }); addAndMakeVisible (c); }
    void clear() { widgets.clear(); customs.clear(); }
    void sync() { for (auto& w : widgets) w->sync(); }
    void setAccent (juce::Colour c) { accent = c; for (auto& w : widgets) w->setAccent (c); }
    // Width needed for `rows` rows of controls.
    int idealWidth (int rows) const;
    void paint (juce::Graphics&) override;
    void resized() override;
    int cellW = 54, cellH = 62, rows = 1;
    juce::String title;
    juce::Colour accent = col::accent();
private:
    Ctx& ctx;
    std::vector<std::unique_ptr<ParamWidget>> widgets;
    std::vector<std::pair<juce::Component*, int>> customs;
};

} // namespace ui
