#include "ParamControl.h"

#include "../PluginProcessor.h"

namespace verdant::ui
{

ParamControl::ParamControl (UiContext& c, PID pid)
    : ctx (c),
      def (kParams[pid]),
      param (*c.proc.getParam (pid)),
      attachment (param, [this] (float v)
                  {
                      plain = v;
                      norm = param.convertTo0to1 (v);
                      parameterValueChanged();
                      if (onValueChange) onValueChange();
                      repaint();
                  }),
      accent (accentForGroup (def.group)),
      caption (def.name)
{
    norm = param.getValue();
    plain = param.convertFrom0to1 (norm);
    setTitle (def.name);
    setHelpText (def.help);
    setRepaintsOnMouseActivity (false);
}

ParamControl::~ParamControl()
{
    ctx.clearHover (this);
}

ParamControl::Cell ParamControl::cellLayout (bool macro) const
{
    const auto b = getLocalBounds().toFloat();
    const float capH = ctx.font (macro ? Type::macroName : Type::label).getHeight() * 1.2f;
    const float valH = ctx.font (macro ? Type::macroValue : Type::value).getHeight() * 1.2f;
    const float gap = ctx.px (macro ? 7.0f : 4.0f);
    const float bodyH = juce::jmax (4.0f, juce::jmin (ctx.px (macro ? 88.0f : 46.0f), b.getHeight() - capH - valH - gap * 2.0f));
    const float total = capH + valH + bodyH + gap * 2.0f;
    const float y = b.getY() + juce::jmax (0.0f, (b.getHeight() - total) * 0.5f);

    Cell cell;
    cell.caption = { b.getX(), y, b.getWidth(), capH };
    cell.body    = { b.getX(), y + capH + gap, b.getWidth(), bodyH };
    cell.value   = { b.getX(), y + capH + gap + bodyH + gap, b.getWidth(), valH };
    return cell;
}

void ParamControl::showHelp()
{
    ctx.setHover (this, def.name, &param, def.help);
}

void ParamControl::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    showHelp();
    repaint();
}

void ParamControl::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    ctx.clearHover (this);
    repaint();
}

} // namespace verdant::ui
