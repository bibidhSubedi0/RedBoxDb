#pragma once
#include <intrin.h>

// One might ask, but Bibidh why AVX2? why not AVX-512?
// I might answer
// I am poor and my ryzen 5 5500U doesn't support AVX-512, but it does in fact support AVX2.


namespace Platform {
    inline bool has_avx2() {
        int info[4] = { 0, 0, 0, 0 };
        __cpuid(info, 0);           // First ask: what's the max input level?
        if (info[0] < 7) return false; // If CPU doesn't support level 7, no AVX2

        __cpuidex(info, 7, 0);      // Now ask level 7 � extended features
        return (info[1] & (1 << 5)) != 0; // Bit 5 of EBX = AVX2
    }
}