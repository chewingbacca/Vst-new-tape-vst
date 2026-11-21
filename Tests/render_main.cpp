#include "dsp/Engine.h"
#include <cstdio>
int main() { swoon::Engine e; e.prepare (48000.0, 512); std::puts ("ok"); return 0; }
