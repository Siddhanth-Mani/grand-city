#pragma once
#include <cstddef>
#include <cstdlib>
#include <new>
namespace gc6 {
// Linear arena allocator for per-frame scratch data: fast, no per-allocation free.
class Arena {
public:
    explicit Arena(size_t capacityBytes) : capacity_(capacityBytes) {
        base_ = static_cast<char*>(std::malloc(capacity_));
        if (!base_) throw std::bad_alloc();
    }
    ~Arena() { std::free(base_); }
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    void* alloc(size_t bytes, size_t align = 16) {
        size_t aligned = (offset_ + align - 1) & ~(align - 1);
        if (aligned + bytes > capacity_) return nullptr;
        void* p = base_ + aligned;
        offset_ = aligned + bytes;
        return p;
    }
    void reset() { offset_ = 0; }
    size_t used() const { return offset_; }
private:
    char* base_ = nullptr;
    size_t capacity_, offset_ = 0;
};
} // namespace gc6
