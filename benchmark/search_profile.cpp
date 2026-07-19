#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iomanip>
#include <filesystem>
#include <cstring>
#include "redboxdb/engine.hpp"
#include "redboxdb/hnsw_manager.hpp"
#include "redboxdb/distance.hpp"

static constexpr int DIM = 128;
static constexpr int DB_SIZE = 100'000;
static constexpr int QUERIES = 3000;
static constexpr int WARMUP = 200;
static constexpr uint8_t HNSW_M = 16;
static constexpr uint16_t HNSW_EF_C = 160;
static constexpr uint16_t HNSW_EF_S = 256;

static std::vector<float> rand_vec(std::mt19937& rng) {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> v(DIM);
    for (auto& x : v) x = dist(rng);
    return v;
}

static float l2_scalar(const float* a, const float* b, size_t dim) {
    float d = 0;
    for (size_t i = 0; i < dim; ++i) {
        float diff = a[i] - b[i];
        d += diff * diff;
    }
    return d;
}

int main() {
    std::mt19937 rng(42);
    std::string db_file = "profile_hnsw.db";
    std::filesystem::remove(db_file);
    std::filesystem::remove(db_file + ".del");

    std::vector<std::vector<float>> corpus(DB_SIZE);
    for (auto& v : corpus) v = rand_vec(rng);
    std::vector<std::vector<float>> queries(QUERIES);
    for (auto& q : queries) q = rand_vec(rng);

    std::cout << "Inserting " << DB_SIZE << " vectors...\n";
    auto* db = new CoreEngine::RedBoxVector(db_file, DIM, DB_SIZE, HNSW_M, HNSW_EF_C);
    db->set_hnsw_ef_search(HNSW_EF_S);

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < DB_SIZE; ++i)
        db->insert_auto(corpus[i]);
    auto t1 = std::chrono::high_resolution_clock::now();
    std::cout << "Inserted in " << std::fixed << std::setprecision(1)
              << std::chrono::duration<double>(t1-t0).count() << "s\n";

    db->warm_pages();

    // Warmup
    for (int i = 0; i < WARMUP; ++i) db->search(queries[i % QUERIES]);

    // ===== PHASE BREAKDOWN =====
    // We'll instrument each phase by calling the HNSW functions directly
    // with the manager's internal pointers

    // Access internal state via search() path — let's measure timing of each piece
    std::cout << "\n===== PHASE BREAKDOWN (ns per query, " << QUERIES << " queries) =====\n";

    // --- Measure L2 distance computation cost ---
    {
        std::vector<double> l2_times;
        l2_times.reserve(QUERIES);
        for (int q = 0; q < QUERIES; ++q) {
            // Measure how long it takes to do DIM L2 distance computations (like search_layer does)
            auto s = std::chrono::high_resolution_clock::now();
            volatile float d = 0;
            for (int i = 0; i < 32; ++i) {  // ~32 L2 per search (ef=8, ~4 expansions × 8 neighbors)
                d += Distance::l2(queries[q].data(), corpus[i].data(), DIM, true);
            }
            auto e = std::chrono::high_resolution_clock::now();
            l2_times.push_back(std::chrono::duration<double, std::nano>(e - s).count());
            (void)d;
        }
        std::sort(l2_times.begin(), l2_times.end());
        std::cout << "  L2 × 32 (AVX2):  P50=" << std::fixed << std::setprecision(0) << l2_times[QUERIES/2]
                  << " ns  P99=" << l2_times[(int)(QUERIES*0.99)] << " ns\n";
    }

    // --- Measure L2 scalar cost ---
    {
        std::vector<double> l2_times;
        l2_times.reserve(QUERIES);
        for (int q = 0; q < QUERIES; ++q) {
            auto s = std::chrono::high_resolution_clock::now();
            volatile float d = 0;
            for (int i = 0; i < 32; ++i) {
                d += l2_scalar(queries[q].data(), corpus[i].data(), DIM);
            }
            auto e = std::chrono::high_resolution_clock::now();
            l2_times.push_back(std::chrono::duration<double, std::nano>(e - s).count());
            (void)d;
        }
        std::sort(l2_times.begin(), l2_times.end());
        std::cout << "  L2 × 32 (scalar): P50=" << l2_times[QUERIES/2]
                  << " ns  P99=" << l2_times[(int)(QUERIES*0.99)] << " ns\n";
    }

    // --- Measure random memory access cost (simulating edge reads) ---
    {
        // Read 32 random slots from the float block
        std::mt19937 access_rng(123);
        std::uniform_int_distribution<int> slot_dis(0, DB_SIZE-1);

        std::vector<double> access_times;
        access_times.reserve(QUERIES);
        for (int q = 0; q < QUERIES; ++q) {
            auto s = std::chrono::high_resolution_clock::now();
            volatile float d = 0;
            for (int i = 0; i < 32; ++i) {
                int slot = slot_dis(access_rng);
                d += corpus[slot][0]; // random read
            }
            auto e = std::chrono::high_resolution_clock::now();
            access_times.push_back(std::chrono::duration<double, std::nano>(e - s).count());
            (void)d;
        }
        std::sort(access_times.begin(), access_times.end());
        std::cout << "  Random mem × 32: P50=" << access_times[QUERIES/2]
                  << " ns  P99=" << access_times[(int)(QUERIES*0.99)] << " ns\n";
    }

    // --- Measure visited buffer check cost ---
    {
        thread_local std::vector<uint32_t> vis(100000);
        uint32_t gen = 0;
        std::vector<double> vis_times;
        vis_times.reserve(QUERIES);
        for (int q = 0; q < QUERIES; ++q) {
            ++gen;
            auto s = std::chrono::high_resolution_clock::now();
            volatile int sink = 0;
            for (int i = 0; i < 32; ++i) {
                sink += (vis[i * 3137 % 100000] == gen) ? 1 : 0;  // random access pattern
            }
            auto e = std::chrono::high_resolution_clock::now();
            vis_times.push_back(std::chrono::duration<double, std::nano>(e - s).count());
            (void)sink;
        }
        std::sort(vis_times.begin(), vis_times.end());
        std::cout << "  Visited × 32:    P50=" << vis_times[QUERIES/2]
                  << " ns  P99=" << vis_times[(int)(QUERIES*0.99)] << " ns\n";
    }

    // --- Measure flat array scan cost (candidates pop min) ---
    {
        struct Entry { float dist; uint32_t slot; };
        std::vector<double> scan_times;
        scan_times.reserve(QUERIES);
        for (int q = 0; q < QUERIES; ++q) {
            Entry arr[32];
            for (int i = 0; i < 32; ++i) arr[i] = {(float)i, (uint32_t)i};
            int scan_sink = 0;
            auto s = std::chrono::high_resolution_clock::now();
            for (int iter = 0; iter < 8; ++iter) {
                int min_idx = 0;
                for (int i = 1; i < 32; ++i)
                    if (arr[i].dist < arr[min_idx].dist) min_idx = i;
                scan_sink += min_idx;
                arr[min_idx] = arr[31 - iter];
            }
            auto e = std::chrono::high_resolution_clock::now();
            scan_times.push_back(std::chrono::duration<double, std::nano>(e - s).count());
            (void)scan_sink;
        }
        std::sort(scan_times.begin(), scan_times.end());
        std::cout << "  Flat scan × 8:   P50=" << scan_times[QUERIES/2]
                  << " ns  P99=" << scan_times[(int)(QUERIES*0.99)] << " ns\n";
    }

    // --- Full end-to-end search ---
    {
        std::vector<double> search_times;
        search_times.reserve(QUERIES);
        for (int q = 0; q < QUERIES; ++q) {
            auto s = std::chrono::high_resolution_clock::now();
            volatile int r = db->search(queries[q]);
            auto e = std::chrono::high_resolution_clock::now();
            search_times.push_back(std::chrono::duration<double, std::nano>(e - s).count());
            (void)r;
        }
        std::sort(search_times.begin(), search_times.end());
        std::cout << "  Full search:     P50=" << search_times[QUERIES/2]
                  << " ns  P99=" << search_times[(int)(QUERIES*0.99)] << "\n";
    }

    // --- Bulk throughput ---
    {
        auto s = std::chrono::high_resolution_clock::now();
        volatile int sink = 0;
        for (int i = 0; i < QUERIES; ++i)
            sink += db->search(queries[i]);
        auto e = std::chrono::high_resolution_clock::now();
        double secs = std::chrono::duration<double>(e - s).count();
        std::cout << "\n  Bulk QPS: " << std::setprecision(0) << (QUERIES / secs) << "\n";
        (void)sink;
    }

    delete db;
    std::filesystem::remove(db_file);
    std::filesystem::remove(db_file + ".del");
    return 0;
}
