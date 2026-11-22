// Scoped flush-to-zero / denormals-are-zero for the calling thread.
#pragma once
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
 #include <xmmintrin.h>
 #define ATM_HAS_SSE 1
#endif
#include <cstdint>

namespace atm {

struct ScopedFlushDenormals
{
#if defined(ATM_HAS_SSE)
    unsigned int old;
    ScopedFlushDenormals() : old (_mm_getcsr()) { _mm_setcsr (old | 0x8040u); }
    ~ScopedFlushDenormals() { _mm_setcsr (old); }
#elif defined(__aarch64__)
    uint64_t old;
    ScopedFlushDenormals() { __asm__ volatile ("mrs %0, fpcr" : "=r" (old)); uint64_t v = old | (1u << 24); __asm__ volatile ("msr fpcr, %0" : : "r" (v)); }
    ~ScopedFlushDenormals() { __asm__ volatile ("msr fpcr, %0" : : "r" (old)); }
#else
    ScopedFlushDenormals() {}
#endif
};

} // namespace atm
