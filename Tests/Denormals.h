// Flush-to-zero / denormals-are-zero for the test executables, matching what the plugin's
// juce::ScopedNoDenormals does on the audio thread. Portable across x86 and arm64.
#pragma once

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
 #include <xmmintrin.h>
 inline void enableFlushToZero() { _mm_setcsr (_mm_getcsr() | 0x8040); }
#elif defined(__aarch64__)
 #include <cstdint>
 inline void enableFlushToZero()
 {
     uint64_t fpcr;
     __asm__ __volatile__ ("mrs %0, fpcr" : "=r" (fpcr));
     fpcr |= (1ull << 24);   // FZ
     __asm__ __volatile__ ("msr fpcr, %0" : : "r" (fpcr));
 }
#else
 inline void enableFlushToZero() {}
#endif
