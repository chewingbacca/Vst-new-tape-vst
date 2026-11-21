// Splinter DSP — royalty-free demo loops, synthesised from scratch at run time.
#pragma once
#include "Analysis.h"

namespace splinter
{
constexpr int kNumDemoLoops = 6;
struct DemoLoopInfo { const char* name; double bpm; double beats; int key; int scale; bool drums; };
const DemoLoopInfo& demoLoopInfo (int index);
// Generates the loop at its native tempo. Background thread only.
void generateDemoLoop (int index, double sampleRate, AudioData& out);
} // namespace splinter
