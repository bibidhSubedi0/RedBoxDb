#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <numeric>
#include <string>
#include <cstring>
#include "redboxdb/engine.hpp"

// ==========================================
// CONFIGURATION
// ==========================================
const int    NUM_VECTORS = 100'000;
const int    DIMENSIONS = 128;
const int    NUM_QUERIES = 1'000;    // 1k queries — enough for stable percentiles
const int    NUM_UPDATES = 1'000;
const int    TOP_K = 10;
const uint8_t HNSW_M = 16;
const uint16_t HNSW_EF_C = 200;
const uint16_t HNSW_EF_S = 256;
std::string DB_BASE = "bench";

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
int main(int argc, char* argv[]) {
    enum class IndexMode { IVF, HNSW, BOTH };
    IndexMode mode = IndexMode::BOTH;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--index") == 0 && i + 1 < argc) {
            ++i;
            if (std::strcmp(argv[i], "ivf") == 0)      mode = IndexMode::IVF;
            else if (std::strcmp(argv[i], "hnsw") == 0) mode = IndexMode::HNSW;
            else if (std::strcmp(argv[i], "both") == 0) mode = IndexMode::BOTH;
            else { std::cerr << "Unknown index: " << argv[i] << "\n"; return 1; }
        } else {
            std::cerr << "Usage: " << argv[0] << " [--index ivf|hnsw|both]\n";
            return 1;
        }
    }

    const bool run_ivf  = (mode == IndexMode::IVF  || mode == IndexMode::BOTH);
    const bool run_hnsw = (mode == IndexMode::HNSW || mode == IndexMode::BOTH);

    std::cout << "===============================================\n";
    std::cout << "         RedBoxDb BENCHMARK SUITE\n";
    std::cout << "===============================================\n";
    std::cout << "Vectors   : " << NUM_VECTORS << "\n";
    std::cout << "Dimensions: " << DIMENSIONS << "\n";
    std::cout << "Queries   : " << NUM_QUERIES << " per search test\n";
    std::cout << "Index     : " << (run_ivf ? "IVF " : "") << (run_hnsw ? "HNSW " : "") << "\n";
    if (run_hnsw)
        std::cout << "HNSW      : M=" << (int)HNSW_M << " ef_c=" << HNSW_EF_C << " ef_s=" << HNSW_EF_S << "\n";
    std::cout << "RNG Seed  : 42 (fixed <-- results are reproducible)\n";
    std::cout << "===============================================\n";

    int bench_num = 0;

    // Helper lambda: run a single benchmark section
    auto run_ivf_benchmarks = [&]() {
        // ==========================================
        // BENCH: INSERT THROUGHPUT (IVF)
        // ==========================================
        {
            std::string db_name = DB_BASE + "_insert_ivf";
            cleanup(db_name);

            std::cout << "\n[" << ++bench_num << "] INSERT THROUGHPUT (IVF)\n";
            print_separator();

            std::cout << "   Pre-generating " << NUM_VECTORS << " vectors...\n";
            std::vector<std::vector<float>> vectors(NUM_VECTORS);
            for (auto& v : vectors) v = rand_vec(DIMENSIONS);

            CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, NUM_VECTORS);

            auto t0 = Clock::now();
            for (int i = 0; i < NUM_VECTORS; ++i)
                db.insert_auto(vectors[i]);
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
        // BENCH: SEARCH LATENCY (IVF, single nearest)
        // ==========================================
        {
            std::string db_name = DB_BASE + "_insert_ivf";

            std::cout << "\n[" << ++bench_num << "] SEARCH LATENCY  (single nearest, IVF)\n";
            print_separator();

            CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, NUM_VECTORS);

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
        // BENCH: SEARCH_N LATENCY (IVF, top-K)
        // ==========================================
        {
            std::string db_name = DB_BASE + "_insert_ivf";

            std::cout << "\n[" << ++bench_num << "] SEARCH_N LATENCY  (top-" << TOP_K << ", IVF)\n";
            print_separator();

            CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, NUM_VECTORS);

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
        // BENCH: UPDATE THROUGHPUT (IVF)
        // ==========================================
        {
            std::string db_name = DB_BASE + "_insert_ivf";

            std::cout << "\n[" << ++bench_num << "] UPDATE THROUGHPUT  (IVF)\n";
            print_separator();

            CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, NUM_VECTORS);

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
        }

        cleanup(DB_BASE + "_insert_ivf");
    };

    auto run_hnsw_benchmarks = [&]() {
        // ==========================================
        // BENCH: INSERT THROUGHPUT (HNSW)
        // ==========================================
        {
            std::string db_name = DB_BASE + "_insert_hnsw";
            cleanup(db_name);

            std::cout << "\n[" << ++bench_num << "] INSERT THROUGHPUT (HNSW M=" << (int)HNSW_M << " ef_c=" << HNSW_EF_C << ")\n";
            print_separator();

            std::cout << "   Pre-generating " << NUM_VECTORS << " vectors...\n";
            std::vector<std::vector<float>> vectors(NUM_VECTORS);
            for (auto& v : vectors) v = rand_vec(DIMENSIONS);

            CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, NUM_VECTORS, HNSW_M, HNSW_EF_C);

            auto t0 = Clock::now();
            for (int i = 0; i < NUM_VECTORS; ++i)
                db.insert_auto(vectors[i]);
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
        // BENCH: SEARCH LATENCY (HNSW, single nearest)
        // ==========================================
        {
            std::string db_name = DB_BASE + "_insert_hnsw";

            std::cout << "\n[" << ++bench_num << "] SEARCH LATENCY  (single nearest, HNSW ef_s=" << HNSW_EF_S << ")\n";
            print_separator();

            CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, NUM_VECTORS, HNSW_M, HNSW_EF_C);
            db.set_hnsw_ef_search(HNSW_EF_S);

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
        // BENCH: SEARCH_N LATENCY (HNSW, top-K)
        // ==========================================
        {
            std::string db_name = DB_BASE + "_insert_hnsw";

            std::cout << "\n[" << ++bench_num << "] SEARCH_N LATENCY  (top-" << TOP_K << ", HNSW)\n";
            print_separator();

            CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, NUM_VECTORS, HNSW_M, HNSW_EF_C);
            db.set_hnsw_ef_search(HNSW_EF_S);

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
        // BENCH: UPDATE THROUGHPUT (HNSW)
        // ==========================================
        {
            std::string db_name = DB_BASE + "_insert_hnsw";

            std::cout << "\n[" << ++bench_num << "] UPDATE THROUGHPUT  (HNSW)\n";
            print_separator();

            CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, NUM_VECTORS, HNSW_M, HNSW_EF_C);
            db.set_hnsw_ef_search(HNSW_EF_S);

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
        }

        cleanup(DB_BASE + "_insert_hnsw");
    };

    if (run_ivf)  run_ivf_benchmarks();
    if (run_hnsw) run_hnsw_benchmarks();

    // ==========================================
    // MIXED WORKLOAD (IVF only — exercises delete/compact)
    // ==========================================
    {
        std::string db_name = DB_BASE + "_mixed";
        cleanup(db_name);

        std::cout << "\n[" << ++bench_num << "] MIXED WORKLOAD  (70% search | 20% insert | 10% delete, IVF)\n";
        print_separator();

        const int MIXED_OPS = 10'000;
        const int INITIAL_SIZE = 10'000;

        {
        CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, MIXED_OPS + INITIAL_SIZE);

        for (int i = 0; i < INITIAL_SIZE; ++i)
            db.insert_auto(rand_vec(DIMENSIONS));

        std::uniform_int_distribution<int>      op_dis(1, 10);
        std::uniform_int_distribution<uint64_t> id_dis(1, INITIAL_SIZE);

        int searches = 0, inserts = 0, deletes = 0;

        auto t0 = Clock::now();

        for (int i = 0; i < MIXED_OPS; ++i) {
            int op = op_dis(gen);

            if (op <= 7) {
                (void)db.search(rand_vec(DIMENSIONS));
                ++searches;
            }
            else if (op <= 9) {
                db.insert_auto(rand_vec(DIMENSIONS));
                ++inserts;
            }
            else {
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
        }
        cleanup(db_name);
    }

    // ==========================================
    // SEARCH UNDER HEAVY DELETION (IVF)
    // ==========================================
    {
        std::string db_name = DB_BASE + "_deletion";
        cleanup(db_name);

        std::cout << "\n[" << ++bench_num << "] SEARCH UNDER HEAVY DELETION  (40% deleted, IVF)\n";
        print_separator();

        const int TOTAL     = 100'000;
        const int DEL_COUNT = TOTAL * 40 / 100;

        Stats  s;
        double total_secs;

        {
            CoreEngine::RedBoxVector db(db_name + ".db", DIMENSIONS, TOTAL);

            for (int i = 0; i < TOTAL; ++i)
                db.insert_auto(rand_vec(DIMENSIONS));

            for (int i = 1; i <= DEL_COUNT; ++i)
                db.remove((uint64_t)i);

            std::cout << "   Inserted   : " << TOTAL     << " vectors\n";
            std::cout << "   Deleted    : " << DEL_COUNT << " vectors (40%)\n";
            std::cout << "   Live rows  : " << (TOTAL - DEL_COUNT) << "\n";
            print_separator();

            for (int i = 0; i < 10; ++i)
                db.search(rand_vec(DIMENSIONS));

            std::vector<double> latencies;
            latencies.reserve(NUM_QUERIES);

            auto total_start = Clock::now();
            for (int i = 0; i < NUM_QUERIES; ++i) {
                auto q  = rand_vec(DIMENSIONS);
                auto t0 = Clock::now();
                (void)db.search(q);
                auto t1 = Clock::now();
                latencies.push_back(Ms(t1 - t0).count());
            }
            auto total_end = Clock::now();

            total_secs = std::chrono::duration<double>(total_end - total_start).count();
            s = compute_stats(latencies);
        }

        std::cout << "   QPS  : " << std::fixed << std::setprecision(1)
                  << (NUM_QUERIES / total_secs) << " queries/sec\n";
        print_stats(s);

        cleanup(db_name);
    }

    std::cout << "\n===============================================\n";
    std::cout << "   BENCHMARK COMPLETE\n";
    std::cout << "===============================================\n";

    return 0;
}