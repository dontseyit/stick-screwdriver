#pragma once
// -----------------------------------------------------------------------------
//  Psram - one way to take a big scratch buffer, and one way to give it back.
//
//  Several tools need a block larger than the stack will hold: two 2048-point
//  microphone blocks and their FFT scratch, a waterfall, seconds of PCM. All of
//  it wants PSRAM and none of it outlives the app.
//
//  The fallback is malloc rather than `new (std::nothrow) T[n]`, which is the
//  point of the pair. Callers free with heap_caps_free(), correct for
//  heap_caps_malloc and undefined for an array from new[]. One allocator makes
//  one free function right for either, and these are arrays of scalars, so
//  there is no constructor for new[] to have run.
// -----------------------------------------------------------------------------
#include <esp_heap_caps.h>

#include <cstddef>

namespace sd {

/// `n` elements in PSRAM, or in the default heap if PSRAM is full or absent.
/// Null when neither could, which every caller checks. Uninitialised, as malloc.
template <typename T>
T* bigAlloc(size_t n) {
    const size_t bytes = sizeof(T) * n;
    void*        p     = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!p) p = heap_caps_malloc(bytes, MALLOC_CAP_DEFAULT);
    return static_cast<T*>(p);
}

/// Gives back anything from bigAlloc. Null is fine, as with free().
inline void bigFree(void* p) { heap_caps_free(p); }

}  // namespace sd
