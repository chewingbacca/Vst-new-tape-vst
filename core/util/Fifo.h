// Single-producer / single-consumer lock-free FIFO of trivially copyable items.
#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace atm {

template <class T>
class SpscFifo
{
public:
    explicit SpscFifo (size_t capacityPow2 = 1024)
        : mask (capacityPow2 - 1), buf (new T[capacityPow2]) {}

    bool push (const T& v)
    {
        const size_t w = wr.load (std::memory_order_relaxed);
        const size_t r = rd.load (std::memory_order_acquire);
        if (w - r > mask) return false;
        buf[w & mask] = v;
        wr.store (w + 1, std::memory_order_release);
        return true;
    }

    bool pop (T& out)
    {
        const size_t r = rd.load (std::memory_order_relaxed);
        const size_t w = wr.load (std::memory_order_acquire);
        if (r == w) return false;
        out = buf[r & mask];
        rd.store (r + 1, std::memory_order_release);
        return true;
    }

    size_t size() const { return wr.load (std::memory_order_acquire) - rd.load (std::memory_order_acquire); }
    size_t capacity() const { return mask + 1; }

private:
    size_t mask;
    std::unique_ptr<T[]> buf;
    alignas (64) std::atomic<size_t> wr { 0 };
    alignas (64) std::atomic<size_t> rd { 0 };
};

} // namespace atm
