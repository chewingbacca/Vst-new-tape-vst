// Placeholder: replaced by the real engine.
#include "EngineBase.h"
namespace atm {
std::unique_ptr<EngineBase> createDrumEngine();
std::unique_ptr<EngineBase> createResonatorEngine() { return createDrumEngine(); }
}
