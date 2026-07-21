#pragma once

// One might ask, but Bibidh why AVX2? why not AVX-512?
// I might answer
// I am poor and my ryzen 5 5500U doesn't support AVX-512, but it does in fact support AVX2.

#if defined(__x86_64__) || defined(_M_X64)
    #ifdef _MSC_VER
        #include <intrin.h>
    #else
        #include <cpuid.h>
    #endif
#endif

namespace Platform {
    inline bool has_avx2() {
#if defined(__x86_64__) || defined(_M_X64)
#ifdef _MSC_VER
        int info[4] = { 0, 0, 0, 0 };
        __cpuid(info, 0);           // First ask: what's the max input level?
        if (info[0] < 7) return false; // If CPU doesn't support level 7, no AVX2

        __cpuidex(info, 7, 0);      // Now ask level 7 — extended features
        return (info[1] & (1 << 5)) != 0; // Bit 5 of EBX = AVX2
#else
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(0, &eax, &ebx, &ecx, &edx) == 0) return false;
        if (eax < 7) return false;

        __cpuid_count(7, 0, eax, ebx, ecx, edx);
        return (ebx & (1 << 5)) != 0; // Bit 5 of EBX = AVX2
#endif
#else
        // No x86 AVX2 support on this architecture (e.g. ARM/NEON).
        return false;
#endif
    }
}
