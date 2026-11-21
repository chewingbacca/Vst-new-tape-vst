#include "Engine.h"

namespace swoon
{

struct Engine::Impl
{
    double sampleRate = 48000.0;
    EngineMeters meters;
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int) { impl->sampleRate = sampleRate; }
void Engine::reset() {}
int Engine::getLatencySamples() const { return 0; }
const EngineMeters& Engine::getMeters() const { return impl->meters; }

void Engine::process (float*, float*, int, const ParamValues&) {}

} // namespace swoon
