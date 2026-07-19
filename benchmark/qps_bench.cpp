#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <numeric>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <cstring>
#include "redboxdb/engine.hpp"

const int         NUM_VECTORS   = 100'000;
const int         DIMENSIONS    = 128;
const int         TIMED_QUERIES = 5'000;
const uint8_t     HNSW_M       = 16;
const uint16_t    HNSW_EF_C    = 160;
const uint16_t    HNSW_EF_S    = 256;

using Clock = std::chrono::high_resolution_clock;
using Ms    = std::chrono::duration<double, std::milli>;

void cleanup(const std::string& db) {
    if (std::filesystem::exists(db))          std::filesystem::remove(db);
    if (std::filesystem::exists(db + ".del")) std::filesystem::remove(db + ".del");
}

struct BenchResult {
    long long total_queries;
    double wall_secs;
    double qps;
    double avg_ms, p50_ms, p95_ms, p99_ms, max_ms;
};

BenchResult run_bench(CoreEngine::RedBoxVector* db,
                      const std::vector<std::vector<float>>& queries,
                      int num_threads, int timed_queries)
{
    std::vector<double> all_latencies(num_threads * timed_queries);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    auto wall_start = Clock::now();

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            int base = t * timed_queries;
            for (int i = 0; i < timed_queries; ++i) {
                auto t0 = Clock::now();
                (void)db->search(queries[base + i]);
                auto t1 = Clock::now();
                all_latencies[base + i] = Ms(t1 - t0).count();
            }
        });
    }

    for (auto& t : threads) t.join();
    auto wall_end = Clock::now();

    double wall_secs = std::chrono::duration<double>(wall_end - wall_start).count();
    long long total_q = (long long)num_threads * timed_queries;

    std::sort(all_latencies.begin(), all_latencies.end());
    size_t n = all_latencies.size();
    double avg = std::accumulate(all_latencies.begin(), all_latencies.end(), 0.0) / n;

    return {
        total_q, wall_secs, total_q / wall_secs,
        avg,
        all_latencies[n * 50 / 100],
        all_latencies[n * 95 / 100],
        all_latencies[n * 99 / 100],
        all_latencies.back()
    };
}

void print_result(const std::string& label, const BenchResult& r) {
    std::cout << "\n--- " << label << " ---\n";
    std::cout << "Total queries : " << r.total_queries << "\n";
    std::cout << "Wall time     : " << std::fixed << std::setprecision(3) << r.wall_secs << " s\n";
    std::cout << "QPS           : " << std::setprecision(1) << r.qps << "\n";
    std::cout << "Avg  : " << std::setprecision(3) << r.avg_ms  << " ms\n";
    std::cout << "P50  : " << r.p50_ms  << " ms\n";
    std::cout << "P95  : " << r.p95_ms  << " ms\n";
    std::cout << "P99  : " << r.p99_ms  << " ms\n";
    std::cout << "Max  : " << r.max_ms  << " ms\n";
}

int main(int argc, char* argv[]) {
    enum class IndexMode { IVF, HNSW, BOTH };
    IndexMode mode = IndexMode::IVF;

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

    const int num_threads = std::max(1u, std::thread::hardware_concurrency());
    std::cout << "Threads: " << num_threads << "\n";
    std::cout << "Index  : "
              << (mode == IndexMode::IVF || mode == IndexMode::BOTH ? "IVF " : "")
              << (mode == IndexMode::HNSW || mode == IndexMode::BOTH ? "HNSW" : "") << "\n";

    // Pre-generate all vectors
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    std::vector<std::vector<float>> corpus(NUM_VECTORS);
    for (auto& v : corpus) {
        v.resize(DIMENSIONS);
        for (auto& x : v) x = dis(rng);
    }

    // Pre-generate query sets
    std::vector<std::vector<float>> queries(num_threads * TIMED_QUERIES);
    {
        std::mt19937 qrng(99);
        std::uniform_real_distribution<float> qdis(0.0f, 1.0f);
        for (auto& q : queries) {
            q.resize(DIMENSIONS);
            for (auto& x : q) x = qdis(qrng);
        }
    }

    auto run_single = [&](const std::string& label, CoreEngine::RedBoxVector* db) {
        // Warmup
        {
            std::cout << "  Warming up " << label << "...\n" << std::flush;
            std::mt19937 wrng(1);
            std::uniform_real_distribution<float> wdis(0.0f, 1.0f);
            std::vector<float> warmup(DIMENSIONS);
            for (auto& x : warmup) x = wdis(wrng);
            for (int i = 0; i < 500; ++i)
                (void)db->search(warmup);
            std::cout << "  Warmup done.\n" << std::flush;
        }

        auto result = run_bench(db, queries, num_threads, TIMED_QUERIES);
        print_result(label, result);
    };

    if (mode == IndexMode::IVF || mode == IndexMode::BOTH) {
        std::string db_file = "qps_bench_ivf.db";
        cleanup(db_file);

        std::cout << "\nInserting " << NUM_VECTORS << " vectors (IVF)...\n" << std::flush;
        auto* db = new CoreEngine::RedBoxVector(db_file, DIMENSIONS, NUM_VECTORS);
        for (int i = 0; i < NUM_VECTORS; ++i)
            db->insert_auto(corpus[i]);
        std::cout << "Insert done.\n";

        run_single("IVF QPS", db);

        delete db;
        cleanup(db_file);
    }

    if (mode == IndexMode::HNSW || mode == IndexMode::BOTH) {
        std::string db_file = "qps_bench_hnsw.db";
        cleanup(db_file);

        std::cout << "\nInserting " << NUM_VECTORS << " vectors (HNSW M="
                  << (int)HNSW_M << " ef_c=" << HNSW_EF_C << ")...\n" << std::flush;
        auto* db = new CoreEngine::RedBoxVector(db_file, DIMENSIONS, NUM_VECTORS, HNSW_M, HNSW_EF_C);
        db->set_hnsw_ef_search(HNSW_EF_S);
        for (int i = 0; i < NUM_VECTORS; ++i)
            db->insert_auto(corpus[i]);
        std::cout << "Insert done.\n";

        run_single("HNSW QPS (ef_s=" + std::to_string(HNSW_EF_S) + ")", db);

        delete db;
        cleanup(db_file);
    }

    return 0;
}
