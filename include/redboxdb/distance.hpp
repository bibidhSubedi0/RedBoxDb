#pragma once
#include <immintrin.h>
#include <cstddef>

namespace Distance {

    // FALLBACK : when AVX2 is not available
    inline float l2_scalar(const float* a, const float* b, size_t dim) {
        float sum = 0.0f;
        for (size_t d = 0; d < dim; ++d) {
            float diff = a[d] - b[d];
            sum += diff * diff;
        }
        return sum;
    }

    // l2 with avx2
    inline float l2_avx2(const float* a, const float* b, size_t dim) {
        __m256 sum = _mm256_setzero_ps();   // 8-lane accumulator, starts at 0
        size_t d = 0;


        for (; d + 8 <= dim; d += 8) {
            __m256 va = _mm256_loadu_ps(a + d);          
            __m256 vb = _mm256_loadu_ps(b + d);          
            __m256 diff = _mm256_sub_ps(va, vb);           
            sum = _mm256_fmadd_ps(diff, diff, sum);       
        }

        // Horizontal sum
        __m128 lo = _mm256_castps256_ps128(sum);          
        __m128 hi = _mm256_extractf128_ps(sum, 1);        
        __m128 acc = _mm_add_ps(lo, hi);                  
        acc = _mm_hadd_ps(acc, acc);                      
        acc = _mm_hadd_ps(acc, acc);                      
        float result = _mm_cvtss_f32(acc);                

        // Scalar tail: handle leftover dims not divisible by 8
        for (; d < dim; ++d) {
            float diff = a[d] - b[d];
            result += diff * diff;
        }

        return result;
    }

    // Call this everywhere — it picks the right path at runtime
    inline float l2(const float* a, const float* b, size_t dim, bool use_avx2) {
        if (use_avx2) return l2_avx2(a, b, dim);
        return l2_scalar(a, b, dim);
    }
}