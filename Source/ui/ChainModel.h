// Message-thread view of the module order: follows the seven `ordX` parameters
// through attachments and writes clean 1..7 permutations back to them.
#pragma once

#include "Theme.h"

class SwoonAudioProcessor;

namespace swoon::ui
{

class ChainModel final : public juce::ChangeBroadcaster
{
public:
    using Order = std::array<int, kNumSlots>;   // slot indices in processing order

    explicit ChainModel (SwoonAudioProcessor&);
    ~ChainModel() override;

    Order getOrder() const;

    /** 0-based position of a slot in the chain. */
    int positionOf (int slot) const;

    /** Writes the order to all seven parameters inside change gestures. */
    void setOrder (const Order&);

    bool isWashAfterFuzz() const;

    /** Wash after Fuzz -> Wash moves to just before Fuzz; otherwise Wash moves to the end. */
    void flipFuzzWash();

    /** The module that lives in a reorderable slot (matched by name). */
    static Module moduleForSlot (int slot) noexcept;

private:
    std::array<float, kNumSlots> positions {};
    std::array<std::unique_ptr<juce::ParameterAttachment>, kNumSlots> attachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainModel)
};

} // namespace swoon::ui
