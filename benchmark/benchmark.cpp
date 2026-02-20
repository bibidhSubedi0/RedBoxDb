#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <numeric>
#include <string>
#include "redboxdb/engine.hpp"

// ==========================================
// CONFIGURATION
// ==========================================
const int    NUM_VECTORS = 100'000;
const int    DIMENSIONS = 128;
const int    NUM_QUERIES = 1'000;    // 1k queries — enough for stable percentiles
const int    NUM_UPDATES = 1'000;
const int    TOP_K = 10;
const std::string DB_BASE = "bench";

// ==========================================
// HELPERS
// ==========================================

using Clock = std::chrono::high_resolution_clock;
using Ms = std::chrono::duration<double, std::milli>;

std::mt19937 gen(42); // fixed seed — reproducible results across runs
std::uniform_real_distribution<float> dis(0.0f, 1.0f);

std::vector<float> rand_vec(size_t dim) {
    std::vector<float> v(dim);
    for (auto& x : v) x = dis(gen);
    return v;
}

struct Stats {
    double min, avg, p50, p95, p99, max;
};

Stats compute_stats(std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    size_t n = samples.size();
    return {
        samples.front(),
        sum / n,
        samples[n * 50 / 100],
        samples[n * 95 / 100],
        samples[n * 99 / 100],
        samples.back()
    };
}

void print_stats(const Stats& s) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "   Min  : " << s.min << " ms\n";
    std::cout << "   Avg  : " << s.avg << " ms\n";
    std::cout << "   P50  : " << s.p50 << " ms\n";
    std::cout << "   P95  : " << s.p95 << " ms\n";
    std::cout << "   P99  : " << s.p99 << " ms  <-- the one that matters\n";
    std::cout << "   Max  : " << s.max << " ms\n";
}

void print_separator() {
    std::cout << "-----------------------------------------------\n";
}

void cleanup(const std::string& name) {
    std::string db = name + ".db";
    std::string del = name + ".db.del";
    if (std::filesystem::exists(db))  std::filesystem::remove(db);
    if (std::filesystem::exists(del)) std::filesystem::remove(del);
}

// ==========================================
// MAIN
// ==========================================
int main() {
    std::cout << "===============================================\n";
    std::cout << "         RedBoxDb BENCHMARK SUITE\n";
    std::cout << "===============================================\n";
    std::cout << "Vectors   : " << NUM_VECTORS << "\n";
    std::cout << "Dimensions: " << DIMENSIONS << "\n";
    std::cout << "Queries   : " << NUM_QUERIES << " per search test\n";
    std::cout << "RNG Seed  : 42 (fixed <-- results are reproducible)\n";
    std::cout << "===============================================\n";

    // ==========================================
    // BENCH 1: INSERT THROUGHPUT (insert_auto)
    // ==========================================
    {
        std::string db_name = DB_BASE + "_insert";
        cleanup(db_name);

        std::cout << "\n[1/5] INSERT THROUGHPUT\n";
        print_separator();

        CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, NUM_VECTORS);

        auto t0 = Clock::now();
        for (int i = 0; i < NUM_VECTORS; ++i)
            db.insert_auto(rand_vec(DIMENSIONS));
        auto t1 = Clock::now();

        double secs = std::chrono::duration<double>(t1 - t0).count();
        double rate = NUM_VECTORS / secs;
        double bytes_mb = (double)(NUM_VECTORS * DIMENSIONS * sizeof(float)) / (1024.0 * 1024.0);

        std::cout << "   Vectors    : " << NUM_VECTORS << "\n";
        std::cout << "   Time       : " << std::fixed << std::setprecision(3) << secs << " s\n";
        std::cout << "   Throughput : " << (long long)rate << " vectors/sec\n";
        std::cout << "   Data Size  : " << std::setprecision(1) << bytes_mb << " MB written\n";
    }

    // ==========================================
    // BENCH 2: SEARCH LATENCY (single nearest)
    // ==========================================
    {
        std::string db_name = DB_BASE + "_insert";  // reuse the populated DB

        std::cout << "\n[2/5] SEARCH LATENCY  (single nearest neighbor)\n";
        print_separator();
        std::cout << "   Note: DB already in OS page cache from Bench 1.\n";
        std::cout << "         These numbers reflect hot-cache performance.\n";
        print_separator();

        CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, NUM_VECTORS);

        // Warmup — 10 queries to settle branch predictor and cache
        for (int i = 0; i < 10; ++i)
            db.search(rand_vec(DIMENSIONS));

        std::vector<double> latencies;
        latencies.reserve(NUM_QUERIES);

        auto total_start = Clock::now();
        for (int i = 0; i < NUM_QUERIES; ++i) {
            auto q = rand_vec(DIMENSIONS);
            auto t0 = Clock::now();
            (void)db.search(q);
            auto t1 = Clock::now();
            latencies.push_back(Ms(t1 - t0).count());
        }
        auto total_end = Clock::now();

        double total_secs = std::chrono::duration<double>(total_end - total_start).count();
        Stats s = compute_stats(latencies);

        std::cout << "   QPS  : " << std::fixed << std::setprecision(1)
            << (NUM_QUERIES / total_secs) << " queries/sec\n";
        print_stats(s);
    }

    // ==========================================
    // BENCH 3: SEARCH_N LATENCY (top-K)
    // ==========================================
    {
        std::string db_name = DB_BASE + "_insert";

        std::cout << "\n[3/5] SEARCH_N LATENCY  (top-" << TOP_K << " nearest neighbors)\n";
        print_separator();

        CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, NUM_VECTORS);

        // Warmup
        for (int i = 0; i < 10; ++i)
            db.search_N(rand_vec(DIMENSIONS), TOP_K);

        std::vector<double> latencies;
        latencies.reserve(NUM_QUERIES);

        auto total_start = Clock::now();
        for (int i = 0; i < NUM_QUERIES; ++i) {
            auto q = rand_vec(DIMENSIONS);
            auto t0 = Clock::now();
            (void)db.search_N(q, TOP_K);
            auto t1 = Clock::now();
            latencies.push_back(Ms(t1 - t0).count());
        }
        auto total_end = Clock::now();

        double total_secs = std::chrono::duration<double>(total_end - total_start).count();
        Stats s = compute_stats(latencies);

        std::cout << "   K    : " << TOP_K << "\n";
        std::cout << "   QPS  : " << std::fixed << std::setprecision(1)
            << (NUM_QUERIES / total_secs) << " queries/sec\n";
        print_stats(s);
    }

    // ==========================================
    // BENCH 4: UPDATE THROUGHPUT (O(1) via index)
    // ==========================================
    {
        std::string db_name = DB_BASE + "_insert";

        std::cout << "\n[4/5] UPDATE THROUGHPUT  (in-place via id_to_index)\n";
        print_separator();

        CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, NUM_VECTORS);

        // Update NUM_UPDATES random IDs (IDs are 1..NUM_VECTORS from insert_auto)
        std::uniform_int_distribution<uint64_t> id_dis(1, NUM_VECTORS);
        std::vector<uint64_t> ids_to_update(NUM_UPDATES);
        for (auto& id : ids_to_update) id = id_dis(gen);

        std::vector<double> latencies;
        latencies.reserve(NUM_UPDATES);

        auto total_start = Clock::now();
        for (uint64_t id : ids_to_update) {
            auto vec = rand_vec(DIMENSIONS);
            auto t0 = Clock::now();
            (void)db.update(id, vec);
            auto t1 = Clock::now();
            latencies.push_back(Ms(t1 - t0).count());
        }
        auto total_end = Clock::now();

        double total_secs = std::chrono::duration<double>(total_end - total_start).count();
        Stats s = compute_stats(latencies);

        std::cout << "   Updates    : " << NUM_UPDATES << "\n";
        std::cout << "   Throughput : " << std::fixed << std::setprecision(0)
            << (NUM_UPDATES / total_secs) << " updates/sec\n";
        print_stats(s);
        std::cout << "   (O(1) lookup via id_to_index <-- no linear scan)\n";
    }

    // ==========================================
    // BENCH 5: MIXED WORKLOAD
    // Simulates a real app: 70% search, 20% insert, 10% delete
    // ==========================================
    {
        std::string db_name = DB_BASE + "_mixed";
        cleanup(db_name);

        std::cout << "\n[5/5] MIXED WORKLOAD  (70% search | 20% insert | 10% delete)\n";
        print_separator();

        const int MIXED_OPS = 10'000;
        const int INITIAL_SIZE = 10'000;

        CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, MIXED_OPS + INITIAL_SIZE);

        // Seed with initial data
        for (int i = 0; i < INITIAL_SIZE; ++i)
            db.insert_auto(rand_vec(DIMENSIONS));

        std::uniform_int_distribution<int>      op_dis(1, 10);
        std::uniform_int_distribution<uint64_t> id_dis(1, INITIAL_SIZE);

        int searches = 0, inserts = 0, deletes = 0;

        auto t0 = Clock::now();

        for (int i = 0; i < MIXED_OPS; ++i) {
            int op = op_dis(gen);

            if (op <= 7) {
                // 70% — search
                (void)db.search(rand_vec(DIMENSIONS));
                ++searches;
            }
            else if (op <= 9) {
                // 20% — insert
                db.insert_auto(rand_vec(DIMENSIONS));
                ++inserts;
            }
            else {
                // 10% — delete (random existing ID)
                db.remove(id_dis(gen));
                ++deletes;
            }
        }

        auto t1 = Clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();

        std::cout << "   Total Ops  : " << MIXED_OPS << "\n";
        std::cout << "   Breakdown  : "
            << searches << " searches | "
            << inserts << " inserts | "
            << deletes << " deletes\n";
        std::cout << "   Total Time : " << std::fixed << std::setprecision(3) << secs << " s\n";
        std::cout << "   Throughput : " << std::setprecision(0)
            << (MIXED_OPS / secs) << " ops/sec\n";

        cleanup(db_name);
    }

    // Cleanup insert bench DB
    cleanup(DB_BASE + "_insert");

    std::cout << "\n===============================================\n";
    std::cout << "   BENCHMARK COMPLETE\n";
    std::cout << "===============================================\n";

    return 0;
}