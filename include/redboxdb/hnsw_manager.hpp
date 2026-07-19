#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>
#include <random>
#include <algorithm>
#include <queue>

#include "redboxdb/distance.hpp"
#include "redboxdb/SpecificMetadata.hpp"

#ifdef _MSC_VER
#include <intrin.h>
#define HNSW_PREFETCH(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#else
#define HNSW_PREFETCH(addr) __builtin_prefetch(addr)
#endif

namespace HnswManager {

    static constexpr uint32_t EMPTY = CoreEngine::SpecificMetadata::UINT32_MAX_SENTINEL;
    static constexpr int MAX_LEVEL = 16;

    inline int compute_level(uint8_t M, std::mt19937& rng) {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        float ml = 1.0f / std::log((float)M);
        float r = 1.0f - dist(rng);
        if (r <= 0.0f) r = 1e-10f;
        return (int)std::floor(-std::log(r) * ml);
    }

    inline int m_max(int level, int M) {
        return (level == 0) ? M * 2 : M;
    }

    inline int edges_per_node(int M) {
        return M * 2 + MAX_LEVEL * M;
    }

    inline uint32_t* node_edges(uint32_t* edge_base, int slot, int M) {
        return edge_base + (size_t)slot * edges_per_node(M);
    }

    inline const uint32_t* node_edges(const uint32_t* edge_base, int slot, int M) {
        return edge_base + (size_t)slot * edges_per_node(M);
    }

    inline uint32_t* level_edges(uint32_t* node_ed, int level, int M) {
        if (level == 0) return node_ed;
        return node_ed + M * 2 + (level - 1) * M;
    }

    inline const uint32_t* level_edges(const uint32_t* node_ed, int level, int M) {
        if (level == 0) return node_ed;
        return node_ed + M * 2 + (level - 1) * M;
    }

    inline int level_edge_count(const uint32_t* lev_ed, int m_max_val) {
        int count = 0;
        for (int i = 0; i < m_max_val; ++i)
            if (lev_ed[i] != EMPTY) ++count;
        return count;
    }

    struct SearchResult {
        float dist;
        uint32_t slot;
        bool operator<(const SearchResult& o) const { return dist < o.dist; }
        bool operator>(const SearchResult& o) const { return dist > o.dist; }
    };

    // Standard HNSW beam search at a single level.
    // Returns the ef closest non-deleted slots.
    // Uses a generation counter for O(1) visited reset.
    inline std::vector<SearchResult> search_layer(
        const float* query,
        uint32_t entry_slot,
        int ef,
        int level,
        const float* float_block,
        const uint32_t* edge_base,
        size_t dim,
        int M,
        bool use_avx2,
        const uint8_t* deleted_flags,
        std::vector<uint8_t>& visited_buf,
        uint32_t& visit_gen,
        int capacity)
    {
        using MinPQ = std::priority_queue<SearchResult, std::vector<SearchResult>, std::greater<SearchResult>>;
        MinPQ candidates_pq;
        std::priority_queue<SearchResult> results_pq;

        ++visit_gen;
        if (visit_gen >= 255 || (int)visited_buf.size() != capacity) {
            visited_buf.assign(capacity, 0);
            visit_gen = 1;
        }

        float entry_dist = Distance::l2(query, float_block + (size_t)entry_slot * dim, dim, use_avx2);
        candidates_pq.push({entry_dist, entry_slot});
        if (!(deleted_flags && deleted_flags[entry_slot])) {
            results_pq.push({entry_dist, entry_slot});
        }
        visited_buf[entry_slot] = visit_gen;

        while (!candidates_pq.empty()) {
            SearchResult f = candidates_pq.top();
            candidates_pq.pop();

            // If f is worse than the current ef-th best, all remaining are too
            if ((int)results_pq.size() >= ef && f.dist > results_pq.top().dist) {
                break;
            }

            // Expand neighbors of f at this level
            const uint32_t* neighb = level_edges(node_edges(edge_base, f.slot, M), level, M);
            int mm = m_max(level, M);

            // Prefetch first few neighbor vectors to hide DRAM latency
            for (int pi = 0; pi < mm && pi < 4; ++pi) {
                if (neighb[pi] != EMPTY) {
                    HNSW_PREFETCH(float_block + (size_t)neighb[pi] * dim);
                }
            }

            for (int i = 0; i < mm; ++i) {
                uint32_t nb = neighb[i];
                if (nb == EMPTY) continue;
                if (deleted_flags && deleted_flags[nb]) continue;
                if (visited_buf[nb] == visit_gen) continue;
                visited_buf[nb] = visit_gen;

                // Prefetch next neighbor's vector while computing current
                if (i + 4 < mm && neighb[i + 4] != EMPTY) {
                    HNSW_PREFETCH(float_block + (size_t)neighb[i + 4] * dim);
                }

                float nb_dist = Distance::l2(query, float_block + (size_t)nb * dim, dim, use_avx2);

                if ((int)results_pq.size() < ef || nb_dist < results_pq.top().dist) {
                    candidates_pq.push({nb_dist, nb});
                    results_pq.push({nb_dist, nb});
                    if ((int)results_pq.size() > ef) {
                        results_pq.pop();
                    }
                }
            }
        }

        // Extract results sorted ascending
        std::vector<SearchResult> results;
        results.reserve(results_pq.size());
        while (!results_pq.empty()) {
            results.push_back(results_pq.top());
            results_pq.pop();
        }
        std::reverse(results.begin(), results.end());
        return results;
    }

    // Ultra-optimized single-NN search_layer: flat sorted arrays, zero heap allocation,
    // 4-ahead prefetch pipeline, inline best tracking.
    inline std::pair<uint32_t, float> search_layer_1(
        const float* query,
        uint32_t entry_slot,
        int ef,
        int level,
        const float* float_block,
        const uint32_t* edge_base,
        size_t dim,
        int M,
        bool use_avx2,
        const uint8_t* deleted_flags,
        std::vector<uint8_t>& visited_buf,
        uint32_t& visit_gen,
        int capacity)
    {
        struct FlatEntry { float dist; uint32_t slot; };

        static constexpr int MAX_CAND = 256;
        FlatEntry cand[MAX_CAND];
        int n_cand = 0;

        FlatEntry res[MAX_CAND];
        int n_res = 0;

        uint32_t best_slot = EMPTY;
        float best_dist = std::numeric_limits<float>::max();

        ++visit_gen;
        if (visit_gen >= 255 || (int)visited_buf.size() != capacity) {
            visited_buf.assign(capacity, 0);
            visit_gen = 1;
        }

        float entry_dist = Distance::l2(query, float_block + (size_t)entry_slot * dim, dim, use_avx2);
        cand[n_cand++] = {entry_dist, entry_slot};
        if (!(deleted_flags && deleted_flags[entry_slot])) {
            res[n_res++] = {entry_dist, entry_slot};
            best_slot = entry_slot;
            best_dist = entry_dist;
        }
        visited_buf[entry_slot] = visit_gen;

        int mm = m_max(level, M);

        while (n_cand > 0) {
            int min_idx = 0;
            for (int i = 1; i < n_cand; ++i) {
                if (cand[i].dist < cand[min_idx].dist)
                    min_idx = i;
            }
            FlatEntry f = cand[min_idx];
            cand[min_idx] = cand[--n_cand];

            if (n_res >= ef && f.dist > res[n_res - 1].dist) {
                break;
            }

            const uint32_t* neighb = level_edges(node_edges(edge_base, f.slot, M), level, M);

            for (int pi = 0; pi < mm && pi < 4; ++pi) {
                if (neighb[pi] != EMPTY) {
                    HNSW_PREFETCH(float_block + (size_t)neighb[pi] * dim);
                }
            }
            if (n_cand > 0) {
                int next_idx = 0;
                for (int i = 1; i < n_cand; ++i) {
                    if (cand[i].dist < cand[next_idx].dist)
                        next_idx = i;
                }
                HNSW_PREFETCH(node_edges(edge_base, cand[next_idx].slot, M));
            }

            for (int i = 0; i < mm; ++i) {
                uint32_t nb = neighb[i];
                if (nb == EMPTY) continue;
                if (deleted_flags && deleted_flags[nb]) continue;
                if (visited_buf[nb] == visit_gen) continue;
                visited_buf[nb] = visit_gen;

                if (i + 4 < mm && neighb[i + 4] != EMPTY) {
                    HNSW_PREFETCH(float_block + (size_t)neighb[i + 4] * dim);
                }

                float nb_dist = Distance::l2(query, float_block + (size_t)nb * dim, dim, use_avx2);

                if (n_res < ef || nb_dist < res[n_res - 1].dist) {
                    if (n_cand < MAX_CAND) {
                        cand[n_cand++] = {nb_dist, nb};
                    }

                    int pos = n_res;
                    if (n_res >= ef) {
                        pos = n_res - 1;
                        for (int j = pos; j > 0 && res[j - 1].dist > nb_dist; --j) {
                            res[j] = res[j - 1];
                            --pos;
                        }
                        res[pos] = {nb_dist, nb};
                    } else {
                        for (pos = n_res; pos > 0 && res[pos - 1].dist > nb_dist; --pos) {
                            res[pos] = res[pos - 1];
                        }
                        res[pos] = {nb_dist, nb};
                        ++n_res;
                    }

                    if (nb_dist < best_dist) {
                        best_dist = nb_dist;
                        best_slot = nb;
                    }
                }
            }
        }

        return {best_slot, best_dist};
    }

    // Select M neighbors from candidates using the pruning heuristic
    // (keep neighbors whose edge is shorter than the connection to any already selected)
    inline std::vector<uint32_t> select_neighbors_heuristic(
        const std::vector<SearchResult>& candidates,
        int M,
        const float* float_block,
        size_t dim,
        bool use_avx2)
    {
        std::vector<SearchResult> sorted = candidates;
        std::sort(sorted.begin(), sorted.end(),
            [](const SearchResult& a, const SearchResult& b) { return a.dist < b.dist; });

        std::vector<uint32_t> selected;
        selected.reserve(M);

        for (const auto& cand : sorted) {
            if ((int)selected.size() >= M) break;

            bool good = true;
            for (uint32_t s : selected) {
                float d = Distance::l2(
                    float_block + (size_t)cand.slot * dim,
                    float_block + (size_t)s * dim,
                    dim, use_avx2);
                if (d < cand.dist) {
                    good = false;
                    break;
                }
            }
            if (good) {
                selected.push_back(cand.slot);
            }
        }

        // Fallback: if heuristic didn't fill M, take closest
        if ((int)selected.size() < M) {
            selected.clear();
            for (const auto& cand : sorted) {
                if ((int)selected.size() >= M) break;
                selected.push_back(cand.slot);
            }
        }

        return selected;
    }

    // Select M closest from candidates — simple sort+trim, no heuristic pruning.
    inline void select_closest(
        std::vector<SearchResult>& candidates,
        int M)
    {
        int n = (int)candidates.size();
        if (n <= M) return;
        std::partial_sort(candidates.begin(), candidates.begin() + M, candidates.end(),
            [](const SearchResult& a, const SearchResult& b) { return a.dist < b.dist; });
        candidates.resize(M);
    }

    // Select M closest neighbors from candidates — returns slot indices.
    // Used at level 0 where diversity heuristic is too aggressive and hurts recall.
    inline std::vector<uint32_t> select_closest_neighbors(
        const std::vector<SearchResult>& candidates,
        int M)
    {
        int n = std::min(M, (int)candidates.size());
        std::vector<uint32_t> selected;
        selected.reserve(n);

        if (n == (int)candidates.size()) {
            for (const auto& c : candidates)
                selected.push_back(c.slot);
            return selected;
        }

        std::vector<const SearchResult*> ptrs(candidates.size());
        for (size_t i = 0; i < candidates.size(); ++i)
            ptrs[i] = &candidates[i];
        std::nth_element(ptrs.begin(), ptrs.begin() + n, ptrs.end(),
            [](const SearchResult* a, const SearchResult* b) { return a->dist < b->dist; });

        for (int i = 0; i < n; ++i)
            selected.push_back(ptrs[i]->slot);
        return selected;
    }

    inline void set_neighbors(
        uint32_t* edge_base,
        int slot,
        int level,
        int M,
        const std::vector<uint32_t>& neighbors)
    {
        uint32_t* lev = level_edges(node_edges(edge_base, slot, M), level, M);
        int mm = m_max(level, M);
        for (int i = 0; i < mm; ++i) {
            lev[i] = (i < (int)neighbors.size()) ? neighbors[i] : EMPTY;
        }
    }

    inline void append_neighbor(
        uint32_t* edge_base,
        int slot,
        int level,
        int M,
        uint32_t neighbor)
    {
        uint32_t* lev = level_edges(node_edges(edge_base, slot, M), level, M);
        int mm = m_max(level, M);
        for (int i = 0; i < mm; ++i) {
            if (lev[i] == EMPTY) {
                lev[i] = neighbor;
                return;
            }
        }
    }

    inline bool hnsw_insert(
        uint32_t slot,
        const float* vec,
        CoreEngine::SpecificMetadata* header,
        float* float_block,
        uint32_t* edge_base,
        uint8_t* level_block,
        size_t dim,
        bool use_avx2,
        const uint8_t* deleted_flags,
        std::mt19937& rng,
        std::vector<uint8_t>& visited_buf,
        uint32_t& visit_gen,
        std::vector<SearchResult>& nb_cands)
    {
        int M = header->hnsw_M;
        int ef_construction = header->hnsw_ef_construction;

        // First node becomes entry point
        if (header->is_initialized == 0) {
            level_block[slot] = 0;
            header->hnsw_entry_point = slot;
            header->hnsw_max_level = 0;
            header->is_initialized = 1;
            return true;
        }

        int level = std::min(compute_level(M, rng), MAX_LEVEL);
        level_block[slot] = (uint8_t)level;

        uint32_t entry = (uint32_t)header->hnsw_entry_point;
        int cur_max_level = header->hnsw_max_level;

        // Phase 1: greedy descent from top level to level+1
        uint32_t curr = entry;
        for (int l = cur_max_level; l > level; --l) {
            float curr_dist = Distance::l2(vec, float_block + (size_t)curr * dim, dim, use_avx2);
            bool improved = true;
            while (improved) {
                improved = false;
                const uint32_t* neighb = level_edges(node_edges(edge_base, curr, M), l, M);
                int mm = m_max(l, M);
                uint32_t best_nb = EMPTY;
                float best_dist = curr_dist;
                for (int i = 0; i < mm; ++i) {
                    uint32_t nb = neighb[i];
                    if (nb == EMPTY) continue;
                    if (deleted_flags && deleted_flags[nb]) continue;
                    // Prefetch next neighbor's vector
                    if (i + 1 < mm && neighb[i + 1] != EMPTY) {
                        HNSW_PREFETCH(float_block + (size_t)neighb[i + 1] * dim);
                    }
                    float nb_dist = Distance::l2(vec, float_block + (size_t)nb * dim, dim, use_avx2);
                    if (nb_dist < best_dist) {
                        best_nb = nb;
                        best_dist = nb_dist;
                    }
                }
                if (best_nb != EMPTY) {
                    curr = best_nb;
                    curr_dist = best_dist;
                    improved = true;
                }
            }
        }

        // Phase 2: for each level from min(level, cur_max_level) down to 0
        int lower_bound = std::min(level, cur_max_level);

        for (int l = lower_bound; l >= 0; --l) {
            int ef = ef_construction;
            auto results = search_layer(vec, curr, ef, l, float_block, edge_base, dim, M, use_avx2, deleted_flags, visited_buf, visit_gen, (int)header->max_capacity);

            // Diversity heuristic at all levels for well-connected graph
            std::vector<uint32_t> selected;
            selected = select_neighbors_heuristic(results, m_max(l, M), float_block, dim, use_avx2);

            // Set outgoing edges from new node
            set_neighbors(edge_base, slot, l, M, selected);

            // Add bidirectional connections
            int mm = m_max(l, M);
            for (uint32_t nb : selected) {
                // Prefetch next neighbor's edge data
                HNSW_PREFETCH(node_edges(edge_base, nb, M));

                int nb_count = level_edge_count(
                    level_edges(node_edges(edge_base, nb, M), l, M), mm);

                if (nb_count < mm) {
                    append_neighbor(edge_base, nb, l, M, slot);
                } else {
                    nb_cands.clear();
                    nb_cands.push_back({Distance::l2(
                        float_block + (size_t)nb * dim,
                        float_block + (size_t)slot * dim, dim, use_avx2), slot});
                    const uint32_t* nb_lev = level_edges(node_edges(edge_base, nb, M), l, M);
                    for (int i = 0; i < mm; ++i) {
                        if (nb_lev[i] != EMPTY) {
                            nb_cands.push_back({Distance::l2(
                                float_block + (size_t)nb * dim,
                                float_block + (size_t)nb_lev[i] * dim, dim, use_avx2), nb_lev[i]});
                        }
                    }
                    // Diversity heuristic at all levels for well-connected graph
                    std::vector<uint32_t> pruned;
                    pruned = select_neighbors_heuristic(nb_cands, mm, float_block, dim, use_avx2);
                    set_neighbors(edge_base, nb, l, M, pruned);
                }
            }

            // Continue descent from best neighbor at this level
            curr = selected.empty() ? curr : selected[0];
        }

        // If new node is above current max level, update entry point
        if (level > cur_max_level) {
            header->hnsw_entry_point = slot;
            header->hnsw_max_level = (uint8_t)level;
        }

        return true;
    }

    inline void hnsw_search(
        const float* query,
        int N,
        const CoreEngine::SpecificMetadata* header,
        const float* float_block,
        const uint32_t* edge_base,
        size_t dim,
        bool use_avx2,
        const uint8_t* deleted_flags,
        std::vector<std::pair<float, uint32_t>>& results_out,
        std::vector<uint8_t>& visited_buf,
        uint32_t& visit_gen)
    {
        int M = header->hnsw_M;
        int ef_search = header->hnsw_ef_search;
        uint32_t entry = (uint32_t)header->hnsw_entry_point;
        int cur_max_level = header->hnsw_max_level;

        uint32_t curr = entry;
        for (int l = cur_max_level; l >= 1; --l) {
            float curr_dist = Distance::l2(query, float_block + (size_t)curr * dim, dim, use_avx2);
            bool improved = true;
            while (improved) {
                improved = false;
                const uint32_t* neighb = level_edges(node_edges(edge_base, curr, M), l, M);
                int mm = m_max(l, M);
                uint32_t best_nb = EMPTY;
                float best_dist = curr_dist;
                for (int i = 0; i < mm; ++i) {
                    uint32_t nb = neighb[i];
                    if (nb == EMPTY) continue;
                    if (deleted_flags && deleted_flags[nb]) continue;
                    if (i + 4 < mm && neighb[i + 4] != EMPTY) {
                        HNSW_PREFETCH(float_block + (size_t)neighb[i + 4] * dim);
                    }
                    float nb_dist = Distance::l2(query, float_block + (size_t)nb * dim, dim, use_avx2);
                    if (nb_dist < best_dist) {
                        best_nb = nb;
                        best_dist = nb_dist;
                    }
                }
                if (best_nb != EMPTY) {
                    curr = best_nb;
                    curr_dist = best_dist;
                    improved = true;
                }
            }
        }

        int ef = std::max(ef_search, N) * 4;
        auto results = search_layer(query, curr, ef, 0, float_block, edge_base, dim, M, use_avx2, deleted_flags, visited_buf, visit_gen, (int)header->max_capacity);

        results_out.clear();
        results_out.reserve(results.size());
        for (const auto& r : results) {
            results_out.push_back({r.dist, r.slot});
        }
    }

    inline uint32_t hnsw_search_1(
        const float* query,
        const CoreEngine::SpecificMetadata* header,
        const float* float_block,
        const uint32_t* edge_base,
        size_t dim,
        bool use_avx2,
        const uint8_t* deleted_flags,
        std::vector<uint8_t>& visited_buf,
        uint32_t& visit_gen,
        int ef_override = 0)
    {
        int M = header->hnsw_M;
        int ef_search = (ef_override > 0) ? ef_override : header->hnsw_ef_search;
        uint32_t entry = (uint32_t)header->hnsw_entry_point;
        int cur_max_level = header->hnsw_max_level;

        uint32_t curr = entry;
        for (int l = cur_max_level; l >= 1; --l) {
            float curr_dist = Distance::l2(query, float_block + (size_t)curr * dim, dim, use_avx2);
            bool improved = true;
            while (improved) {
                improved = false;
                const uint32_t* neighb = level_edges(node_edges(edge_base, curr, M), l, M);
                int mm = m_max(l, M);
                uint32_t best_nb = EMPTY;
                float best_dist = curr_dist;

                // Batch prefetch first 4 neighbors
            for (int pi = 0; pi < mm && pi < 4; ++pi) {
                if (neighb[pi] != EMPTY) {
                    HNSW_PREFETCH(float_block + (size_t)neighb[pi] * dim);
                }
            }

                for (int i = 0; i < mm; ++i) {
                    uint32_t nb = neighb[i];
                    if (nb == EMPTY) continue;
                    if (deleted_flags && deleted_flags[nb]) continue;
                    if (i + 4 < mm && neighb[i + 4] != EMPTY) {
                        HNSW_PREFETCH(float_block + (size_t)neighb[i + 4] * dim);
                    }
                    float nb_dist = Distance::l2(query, float_block + (size_t)nb * dim, dim, use_avx2);
                    if (nb_dist < best_dist) {
                        best_nb = nb;
                        best_dist = nb_dist;
                    }
                }
                if (best_nb != EMPTY) {
                    curr = best_nb;
                    curr_dist = best_dist;
                    improved = true;
                }
            }
        }

        auto best = search_layer_1(query, curr, std::max(ef_search, 1), 0, float_block, edge_base, dim, M, use_avx2, deleted_flags, visited_buf, visit_gen, (int)header->max_capacity);

        return best.first;
    }

} // namespace HnswManager
