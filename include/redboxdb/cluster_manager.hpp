#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>
#include <random>
#include <algorithm>
#include "redboxdb/distance.hpp"

namespace ClusterManager {

    // Finds the nearest centroid to vec among k centroids.
    // Returns the cluster index c in [0, k).
    inline uint16_t find_nearest_centroid(
        const float* vec,
        const float* centroid_block,  // K x dim, row-major
        uint8_t      k,
        size_t       dim,
        bool         use_avx2)
    {
        float    best_dist = std::numeric_limits<float>::max();
        uint16_t best_c    = 0;
        for (uint8_t c = 0; c < k; ++c) {
            const float* centroid = centroid_block + (size_t)c * dim;
            float dist = Distance::l2(vec, centroid, dim, use_avx2);
            if (dist < best_dist) { best_dist = dist; best_c = c; }
        }
        return best_c;
    }

    // Online running-mean update for centroid c.
    // cluster_count_block[c] must already reflect all vectors assigned so far
    // (including those assigned during K-Means++ init).
    inline void update_centroid(
        float*       centroid_block,
        uint64_t*    cluster_count_block,
        uint16_t     c,
        const float* vec,
        size_t       dim)
    {
        float*   centroid  = centroid_block + (size_t)c * dim;
        uint64_t new_count = ++cluster_count_block[c];
        for (size_t d = 0; d < dim; ++d)
            centroid[d] += (vec[d] - centroid[d]) / (float)new_count;
    }

    // K-Means++ initialization.
    // Called once when vector_count reaches the init threshold.
    // Picks k spread-out centroids from the first n vectors in float_block,
    // then assigns every one of those n slots to its nearest centroid,
    // writing into cluster_block and setting cluster_count_block to the
    // true per-cluster counts so online updates afterwards are correct.
    inline void kmeans_plus_plus_init(
        float*        centroid_block,
        uint64_t*     cluster_count_block,
        uint16_t*     cluster_block,
        const float*  float_block,
        uint8_t       k,
        size_t        n,           // number of vectors to init from (>= k)
        size_t        dim,
        bool          use_avx2)
    {
        std::mt19937 rng(42);

        // Reset counts: we will recount from scratch during assignment
        for (uint8_t c = 0; c < k; ++c) cluster_count_block[c] = 0;

        // Step 1: pick first centroid randomly from the n vectors
        std::uniform_int_distribution<size_t> uniform(0, n - 1);
        size_t first = uniform(rng);
        const float* first_vec = float_block + first * dim;
        float* c0 = centroid_block;
        for (size_t d = 0; d < dim; ++d) c0[d] = first_vec[d];

        // Step 2: pick remaining k-1 centroids with D^2 weighting
        std::vector<float> min_dists(n, std::numeric_limits<float>::max());

        for (uint8_t chosen = 1; chosen < k; ++chosen) {
            // Update min distances from each point to nearest chosen centroid
            const float* last_centroid = centroid_block + (size_t)(chosen - 1) * dim;
            for (size_t i = 0; i < n; ++i) {
                const float* vec = float_block + i * dim;
                float d = Distance::l2(vec, last_centroid, dim, use_avx2);
                if (d < min_dists[i]) min_dists[i] = d;
            }

            // Sample next centroid proportional to D^2
            float total = 0.0f;
            for (float d : min_dists) total += d;

            std::uniform_real_distribution<float> dist_sample(0.0f, total);
            float  sample     = dist_sample(rng);
            float  cumulative = 0.0f;
            size_t next_idx   = 0;
            for (size_t i = 0; i < n; ++i) {
                cumulative += min_dists[i];
                if (cumulative >= sample) { next_idx = i; break; }
            }

            const float* next_vec     = float_block + next_idx * dim;
            float*       next_centroid = centroid_block + (size_t)chosen * dim;
            for (size_t d = 0; d < dim; ++d) next_centroid[d] = next_vec[d];
        }

        // Step 3: assign all n vectors to their nearest centroid.
        // Compute true per-cluster means and counts so post-init online
        // updates use the correct denominator and don't destroy centroids.
        //
        // We do two passes:
        //   Pass 1 — assign cluster_block, accumulate sums + counts
        //   Pass 2 — divide sums by counts to get true cluster means

        std::vector<std::vector<double>> sums(k, std::vector<double>(dim, 0.0));

        for (size_t i = 0; i < n; ++i) {
            const float* vec = float_block + i * dim;
            uint16_t c = find_nearest_centroid(vec, centroid_block, k, dim, use_avx2);
            cluster_block[i] = c;
            cluster_count_block[c]++;
            for (size_t d = 0; d < dim; ++d)
                sums[c][d] += (double)vec[d];
        }

        // Recompute centroids as true means of assigned vectors
        for (uint8_t c = 0; c < k; ++c) {
            if (cluster_count_block[c] == 0) continue;
            float* centroid = centroid_block + (size_t)c * dim;
            double inv = 1.0 / (double)cluster_count_block[c];
            for (size_t d = 0; d < dim; ++d)
                centroid[d] = (float)(sums[c][d] * inv);
        }
    }

} // namespace ClusterManager