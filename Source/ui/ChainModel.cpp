#include "ChainModel.h"

#include "../PluginProcessor.h"

#include <cstring>

namespace swoon::ui
{

ChainModel::ChainModel (SwoonAudioProcessor& processor)
{
    for (size_t i = 0; i < attachments.size(); ++i)
    {
        auto& param = processor.getParam (kOrderParams[i]);
        positions[i] = param.convertFrom0to1 (param.getValue());

        attachments[i] = std::make_unique<juce::ParameterAttachment> (param, [this, i] (float v)
        {
            if (! juce::exactlyEqual (positions[i], v))
            {
                positions[i] = v;
                sendChangeMessage();   // coalesces the seven writes of a reorder
            }
        });
    }
}

ChainModel::~ChainModel()
{
    removeAllChangeListeners();
}

Module ChainModel::moduleForSlot (int slot) noexcept
{
    const char* name = slotName (static_cast<Slot> (slot));

    for (auto m : { Module::Glide, Module::Fuzz, Module::Amp, Module::Mod, Module::Focus, Module::Reverse, Module::Wash })
        if (std::strcmp (moduleName (m), name) == 0)
            return m;

    return static_cast<Module> (slot + 1);
}

ChainModel::Order ChainModel::getOrder() const
{
    ParamValues pv;
    for (size_t i = 0; i < positions.size(); ++i)
        pv[kOrderParams[i]] = positions[i];

    return resolveOrder (pv);
}

int ChainModel::positionOf (int slot) const
{
    const auto order = getOrder();
    for (int i = 0; i < kNumSlots; ++i)
        if (order[(size_t) i] == slot)
            return i;

    return 0;
}

void ChainModel::setOrder (const Order& order)
{
    for (auto& a : attachments)
        a->beginGesture();

    for (int position = 0; position < kNumSlots; ++position)
        attachments[(size_t) order[(size_t) position]]->setValueAsPartOfGesture ((float) (position + 1));

    for (auto& a : attachments)
        a->endGesture();
}

bool ChainModel::isWashAfterFuzz() const
{
    return positionOf ((int) Slot::Wash) > positionOf ((int) Slot::Fuzz);
}

void ChainModel::flipFuzzWash()
{
    const auto order = getOrder();
    Order next {};
    size_t n = 0;

    if (isWashAfterFuzz())
    {
        for (int slot : order)
        {
            if (slot == (int) Slot::Wash)
                continue;
            if (slot == (int) Slot::Fuzz)
                next[n++] = (int) Slot::Wash;
            next[n++] = slot;
        }
    }
    else
    {
        for (int slot : order)
            if (slot != (int) Slot::Wash)
                next[n++] = slot;
        next[n++] = (int) Slot::Wash;
    }

    setOrder (next);
}

} // namespace swoon::ui
