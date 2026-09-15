#include "tests/support/alloc_counter.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace {

std::atomic<std::size_t> g_allocation_count{0};

// std::aligned_alloc requires size to already be a multiple of alignment (glibc enforces this and
// returns null otherwise); a new-expression has no such obligation, so this rounds size up before
// calling it. The extra bytes are harmless: callers only need at least size bytes back, and
// std::free() is valid on anything std::aligned_alloc() returned.
void* checked_aligned_alloc(std::size_t size, std::align_val_t alignment) {
  const auto align = static_cast<std::size_t>(alignment);
  const std::size_t rounded_size = ((size + align - 1) / align) * align;
  return std::aligned_alloc(align, rounded_size);
}

}  // namespace

namespace astra_test {

std::size_t allocation_count() noexcept {
  return g_allocation_count.load(std::memory_order_relaxed);
}

}  // namespace astra_test

// The full global replacement set. Every allocating form counts, then defers to std::malloc (or the
// aligned helper above); every freeing form defers to std::free -- std::free() is specified to
// accept a pointer from any of std::malloc, std::calloc, std::realloc, or std::aligned_alloc.

void* operator new(std::size_t size) {
  g_allocation_count.fetch_add(1, std::memory_order_relaxed);
  void* ptr = std::malloc(size);
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  return ptr;
}

void* operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
  g_allocation_count.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(size);
}

void* operator new[](std::size_t size) {
  g_allocation_count.fetch_add(1, std::memory_order_relaxed);
  void* ptr = std::malloc(size);
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  return ptr;
}

void* operator new[](std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
  g_allocation_count.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
  g_allocation_count.fetch_add(1, std::memory_order_relaxed);
  void* ptr = checked_aligned_alloc(size, alignment);
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  return ptr;
}

void* operator new(std::size_t size, std::align_val_t alignment,
                   const std::nothrow_t& /*tag*/) noexcept {
  g_allocation_count.fetch_add(1, std::memory_order_relaxed);
  return checked_aligned_alloc(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
  g_allocation_count.fetch_add(1, std::memory_order_relaxed);
  void* ptr = checked_aligned_alloc(size, alignment);
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  return ptr;
}

void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t& /*tag*/) noexcept {
  g_allocation_count.fetch_add(1, std::memory_order_relaxed);
  return checked_aligned_alloc(size, alignment);
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t /*size*/) noexcept { std::free(ptr); }
void operator delete(void* ptr, const std::nothrow_t& /*tag*/) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t /*size*/) noexcept { std::free(ptr); }
void operator delete[](void* ptr, const std::nothrow_t& /*tag*/) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::align_val_t /*alignment*/) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
  std::free(ptr);
}
void operator delete(void* ptr, std::align_val_t /*alignment*/,
                     const std::nothrow_t& /*tag*/) noexcept {
  std::free(ptr);
}
void operator delete[](void* ptr, std::align_val_t /*alignment*/) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
  std::free(ptr);
}
void operator delete[](void* ptr, std::align_val_t /*alignment*/,
                       const std::nothrow_t& /*tag*/) noexcept {
  std::free(ptr);
}
