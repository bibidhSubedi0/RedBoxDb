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
#include <thread>
#include <atomic>
#include <set>
#include "redboxdb/engine.hpp"

// ==========================================
// CONFIGURATION
// ==========================================
const int    NUM_QUERIES = 1'000;
const int    NUM_UPDATES = 1'000;
const int    TOP_K = 10;
const uint8_t HNSW_M = 16;
const uint16_t HNSW_EF_C = 160;
const uint16_t HNSW_EF_S = 256;
std::string DB_BASE = "bench_ext";

// ==========================================
// HELPERS
// ==========================================
using Clock = std::chrono::high_resolution_clock;
using Ms = std::chrono::duration<double, std::milli>;

std::mt19937 gen(42);
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

int bench_num = 0;

// ==========================================
// 1. HNSW MIXED WORKLOAD (70/20/10)
// ==========================================
void bench_hnsw_mixed() {
    const int NUM_VECTORS = 100'000;
    const size_t DIM = 128;
    const int MIXED_OPS = 10'000;
    const int INITIAL_SIZE = 10'000;
    std::string db_name = DB_BASE + "_hnsw_mixed";
    cleanup(db_name);

    std::cout << "\n[" << ++bench_num << "] HNSW MIXED WORKLOAD  (70% search | 20% insert | 10% delete)\n";
    print_separator();

    {
        CoreEngine::RedBoxVector db(db_name + ".db", DIM, NUM_VECTORS, HNSW_M, HNSW_EF_C);
        db.set_hnsw_ef_search(HNSW_EF_S);

        for (int i = 0; i < INITIAL_SIZE; ++i)
            db.insert_auto(rand_vec(DIM));

        std::uniform_int_distribution<int>      op_dis(1, 10);
        std::uniform_int_distribution<uint64_t> id_dis(1, INITIAL_SIZE);

        int searches = 0, inserts = 0, deletes = 0;
        std::vector<double> latencies;
        latencies.reserve(MIXED_OPS);

        auto t0 = Clock::now();

        for (int i = 0; i < MIXED_OPS; ++i) {
            int op = op_dis(gen);
            auto lt0 = Clock::now();

            if (op <= 7) {
                (void)db.search(rand_vec(DIM));
                ++searches;
            }
            else if (op <= 9) {
                db.insert_auto(rand_vec(DIM));
                ++inserts;
            }
            else {
                db.remove(id_dis(gen));
                ++deletes;
            }

            auto lt1 = Clock::now();
            latencies.push_back(Ms(lt1 - lt0).count());
        }

        auto t1 = Clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();
        Stats s = compute_stats(latencies);

        std::cout << "   Total Ops  : " << MIXED_OPS << "\n";
        std::cout << "   Breakdown  : "
            << searches << " searches | "
            << inserts << " inserts | "
            << deletes << " deletes\n";
        std::cout << "   Total Time : " << std::fixed << std::setprecision(3) << secs << " s\n";
        std::cout << "   Throughput : " << std::setprecision(0)
            << (MIXED_OPS / secs) << " ops/sec\n";
        print_stats(s);
    }
    cleanup(db_name);
}

// ==========================================
// 2. HNSW SEARCH UNDER HEAVY DELETION
// ==========================================
void bench_hnsw_heavy_deletion() {
    const int NUM_VECTORS = 100'000;
    const size_t DIM = 128;
    const int DEL_COUNT = NUM_VECTORS * 40 / 100;
    std::string db_name = DB_BASE + "_hnsw_del";
    cleanup(db_name);

    std::cout << "\n[" << ++bench_num << "] HNSW SEARCH UNDER HEAVY DELETION  (40% deleted)\n";
    print_separator();

    Stats s;
    double total_secs;

    {
        CoreEngine::RedBoxVector db(db_name + ".db", DIM, NUM_VECTORS, HNSW_M, HNSW_EF_C);
        db.set_hnsw_ef_search(HNSW_EF_S);

        for (int i = 0; i < NUM_VECTORS; ++i)
            db.insert_auto(rand_vec(DIM));

        for (int i = 1; i <= DEL_COUNT; ++i)
            db.remove((uint64_t)i);

        std::cout << "   Inserted   : " << NUM_VECTORS << " vectors\n";
        std::cout << "   Deleted    : " << DEL_COUNT << " vectors (40%)\n";
        std::cout << "   Live rows  : " << (NUM_VECTORS - DEL_COUNT) << "\n";
        print_separator();

        db.warm_pages();

        for (int i = 0; i < 10; ++i)
            db.search(rand_vec(DIM));

        std::vector<double> latencies;
        latencies.reserve(NUM_QUERIES);

        auto total_start = Clock::now();
        for (int i = 0; i < NUM_QUERIES; ++i) {
            auto q  = rand_vec(DIM);
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

// ==========================================
// 3. DIMENSION SCALING BENCHMARK
// ==========================================
void bench_dimension_scaling() {
    std::cout << "\n[" << ++bench_num << "] DIMENSION SCALING BENCHMARK\n";
    print_separator();

    struct DimConfig {
        size_t dim;
        const char* label;
        int db_size;
    };

    std::vector<DimConfig> configs = {
        {128,  "dim=128",  100'000},
        {768,  "dim=768",  100'000},
        {1536, "dim=1536", 100'000},
    };

    for (auto& cfg : configs) {
        std::string db_name = DB_BASE + "_dim_" + std::to_string(cfg.dim);
        cleanup(db_name);

        std::cout << "\n   --- " << cfg.label << " (" << cfg.db_size << " vectors) ---\n";

        {
            CoreEngine::RedBoxVector db(db_name + ".db", cfg.dim, cfg.db_size, HNSW_M, HNSW_EF_C);
            db.set_hnsw_ef_search(HNSW_EF_S);

            // Insert
            auto t0 = Clock::now();
            for (int i = 0; i < cfg.db_size; ++i)
                db.insert_auto(rand_vec(cfg.dim));
            auto t1 = Clock::now();
            double insert_secs = std::chrono::duration<double>(t1 - t0).count();
            std::cout << "   Insert: " << std::fixed << std::setprecision(2) << insert_secs << "s ("
                      << std::setprecision(0) << (cfg.db_size / insert_secs) << " vecs/sec)\n";

            db.warm_pages();

            // Search
            auto qs = rand_vec(cfg.dim);
            for (int i = 0; i < 10; ++i) db.search(qs);

            std::vector<double> latencies;
            latencies.reserve(NUM_QUERIES);
            auto total_start = Clock::now();
            for (int i = 0; i < NUM_QUERIES; ++i) {
                auto q = rand_vec(cfg.dim);
                auto t0 = Clock::now();
                (void)db.search(q);
                auto t1 = Clock::now();
                latencies.push_back(Ms(t1 - t0).count());
            }
            auto total_end = Clock::now();

            double total_secs = std::chrono::duration<double>(total_end - total_start).count();
            Stats s = compute_stats(latencies);

            std::cout << "   Search QPS: " << std::setprecision(0) << (NUM_QUERIES / total_secs) << "\n";
            std::cout << "   P50: " << std::setprecision(3) << s.p50 << " ms | P99: " << s.p99 << " ms\n";

            // Search_N
            for (int i = 0; i < 10; ++i) db.search_N(rand_vec(cfg.dim), TOP_K);

            latencies.clear();
            total_start = Clock::now();
            for (int i = 0; i < NUM_QUERIES; ++i) {
                auto q = rand_vec(cfg.dim);
                auto t0 = Clock::now();
                (void)db.search_N(q, TOP_K);
                auto t1 = Clock::now();
                latencies.push_back(Ms(t1 - t0).count());
            }
            total_end = Clock::now();

            total_secs = std::chrono::duration<double>(total_end - total_start).count();
            s = compute_stats(latencies);

            std::cout << "   search_N QPS: " << std::setprecision(0) << (NUM_QUERIES / total_secs) << "\n";
            std::cout << "   P50: " << std::setprecision(3) << s.p50 << " ms | P99: " << s.p99 << " ms\n";
        }
        cleanup(db_name);
    }
}

// ==========================================
// 4. STANDALONE DELETE THROUGHPUT
// ==========================================
void bench_delete_throughput() {
    const int NUM_VECTORS = 100'000;
    const size_t DIM = 128;
    std::string db_name = DB_BASE + "_delete";
    cleanup(db_name);

    std::cout << "\n[" << ++bench_num << "] DELETE THROUGHPUT\n";
    print_separator();

    {
        CoreEngine::RedBoxVector db(db_name + ".db", DIM, NUM_VECTORS, HNSW_M, HNSW_EF_C);

        for (int i = 0; i < NUM_VECTORS; ++i)
            db.insert_auto(rand_vec(DIM));

        // Delete first 50k vectors
        const int DEL_COUNT = 50'000;
        std::vector<uint64_t> ids_to_delete;
        for (int i = 1; i <= DEL_COUNT; ++i)
            ids_to_delete.push_back(i);

        std::vector<double> latencies;
        latencies.reserve(DEL_COUNT);

        auto total_start = Clock::now();
        for (uint64_t id : ids_to_delete) {
            auto t0 = Clock::now();
            (void)db.remove(id);
            auto t1 = Clock::now();
            latencies.push_back(Ms(t1 - t0).count());
        }
        auto total_end = Clock::now();

        double total_secs = std::chrono::duration<double>(total_end - total_start).count();
        Stats s = compute_stats(latencies);

        std::cout << "   Deleted    : " << DEL_COUNT << " vectors\n";
        std::cout << "   Throughput : " << std::setprecision(0) << (DEL_COUNT / total_secs) << " deletes/sec\n";
        std::cout << "   Time       : " << std::fixed << std::setprecision(3) << total_secs << " s\n";
        print_stats(s);
    }
    cleanup(db_name);
}

// ==========================================
// 5. HNSW PARAMETER SWEEP
// ==========================================
void bench_hnsw_param_sweep() {
    const int NUM_VECTORS = 50'000;
    const size_t DIM = 128;
    const int SWEEP_QUERIES = 500;

    std::cout << "\n[" << ++bench_num << "] HNSW PARAMETER SWEEP (recall-latency tradeoff)\n";
    print_separator();

    struct ParamSet {
        uint8_t M;
        uint16_t ef_c;
        uint16_t ef_s;
        const char* label;
    };

    std::vector<ParamSet> params = {
        {8,   50,  32,  "M=8  ef_c=50   ef_s=32"},
        {8,   50,  64,  "M=8  ef_c=50   ef_s=64"},
        {8,   50,  128, "M=8  ef_c=50   ef_s=128"},
        {8,   50,  256, "M=8  ef_c=50   ef_s=256"},
        {16,  100, 64,  "M=16 ef_c=100  ef_s=64"},
        {16,  100, 128, "M=16 ef_c=100  ef_s=128"},
        {16,  100, 256, "M=16 ef_c=100  ef_s=256"},
        {16,  160, 128, "M=16 ef_c=160  ef_s=128"},
        {16,  160, 256, "M=16 ef_c=160  ef_s=256"},
        {24,  200, 128, "M=24 ef_c=200  ef_s=128"},
        {24,  200, 256, "M=24 ef_c=200  ef_s=256"},
    };

    // Pre-generate corpus and queries
    std::vector<std::vector<float>> corpus(NUM_VECTORS);
    for (auto& v : corpus) v = rand_vec(DIM);
    std::vector<std::vector<float>> queries(SWEEP_QUERIES);
    for (auto& q : queries) q = rand_vec(DIM);

    for (auto& p : params) {
        std::string db_name = DB_BASE + "_sweep_" + std::to_string(p.M) + "_" + std::to_string(p.ef_c);
        cleanup(db_name);

        CoreEngine::RedBoxVector db(db_name + ".db", DIM, NUM_VECTORS, p.M, p.ef_c);
        db.set_hnsw_ef_search(p.ef_s);

        for (int i = 0; i < NUM_VECTORS; ++i)
            db.insert_auto(corpus[i]);

        db.warm_pages();

        // Warmup
        for (int i = 0; i < 10; ++i) db.search(queries[i]);

        std::vector<double> latencies;
        latencies.reserve(SWEEP_QUERIES);
        auto t0 = Clock::now();
        for (int i = 0; i < SWEEP_QUERIES; ++i) {
            auto q = queries[i];
            auto lt0 = Clock::now();
            (void)db.search(q);
            auto lt1 = Clock::now();
            latencies.push_back(Ms(lt1 - lt0).count());
        }
        auto t1 = Clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();
        double qps = SWEEP_QUERIES / secs;

        // Measure recall vs brute-force (subset for speed)
        int recall_hits = 0;
        int recall_queries = std::min(100, SWEEP_QUERIES);
        for (int i = 0; i < recall_queries; ++i) {
            // BF top-1
            float best_dist = std::numeric_limits<float>::max();
            int best_j = -1;
            for (int j = 0; j < NUM_VECTORS; ++j) {
                float d = 0;
                for (size_t d2 = 0; d2 < DIM; ++d2) {
                    float diff = queries[i][d2] - corpus[j][d2];
                    d += diff * diff;
                }
                if (d < best_dist) { best_dist = d; best_j = j; }
            }

            int db_result = db.search(queries[i]);
            if (db_result == best_j + 1) recall_hits++;
        }
        double recall = (double)recall_hits / recall_queries;

        std::cout << "   " << p.label
                  << " | QPS: " << std::setw(6) << std::setprecision(0) << std::fixed << qps
                  << " | Recall@1: " << std::setw(5) << std::setprecision(1) << (recall * 100) << "%"
                  << " | P99: " << std::setw(6) << std::setprecision(3)
                  << compute_stats(latencies).p99 << " ms\n";

        cleanup(db_name);
    }
}

// ==========================================
// 6. HNSW RECALL UNDER DELETION
// ==========================================
void bench_hnsw_recall_under_deletion() {
    const int NUM_VECTORS = 50'000;
    const size_t DIM = 128;
    const int RECALL_QUERIES = 200;
    const int TOP_K = 100;

    std::cout << "\n[" << ++bench_num << "] HNSW RECALL UNDER DELETION\n";
    print_separator();

    std::string db_name = DB_BASE + "_hnsw_recall_del";
    cleanup(db_name);

    // Pre-generate data
    std::vector<std::vector<float>> corpus(NUM_VECTORS);
    for (auto& v : corpus) v = rand_vec(DIM);
    std::vector<std::vector<float>> queries(RECALL_QUERIES);
    for (auto& q : queries) q = rand_vec(DIM);

    for (int del_pct : {0, 20, 40, 60}) {
        cleanup(db_name);
        int del_count = NUM_VECTORS * del_pct / 100;

        CoreEngine::RedBoxVector db(db_name + ".db", DIM, NUM_VECTORS, HNSW_M, HNSW_EF_C);
        db.set_hnsw_ef_search(HNSW_EF_S);

        for (int i = 0; i < NUM_VECTORS; ++i)
            db.insert_auto(corpus[i]);

        std::set<int> deleted_set;
        for (int i = 1; i <= del_count; ++i) {
            db.remove((uint64_t)i);
            deleted_set.insert(i);
        }

        db.warm_pages();

        // Recompute BF ground truth on LIVE vectors only
        std::vector<std::set<int>> bf_live(RECALL_QUERIES);
        for (int i = 0; i < RECALL_QUERIES; ++i) {
            std::vector<std::pair<float, int>> dists;
            dists.reserve(NUM_VECTORS - del_count);
            for (int j = 0; j < NUM_VECTORS; ++j) {
                if (deleted_set.count(j + 1)) continue; // skip deleted
                float d = 0;
                for (size_t d2 = 0; d2 < DIM; ++d2) {
                    float diff = queries[i][d2] - corpus[j][d2];
                    d += diff * diff;
                }
                dists.push_back({d, j});
            }
            int live_topk = std::min(TOP_K, (int)dists.size());
            std::partial_sort(dists.begin(), dists.begin() + live_topk, dists.end());
            for (int k = 0; k < live_topk; ++k)
                bf_live[i].insert(dists[k].second);
        }

        int total_hits = 0;
        int total_possible = 0;
        for (int i = 0; i < RECALL_QUERIES; ++i) {
            auto hnsw_results = db.search_N(queries[i], TOP_K);
            total_possible += std::min(TOP_K, (int)bf_live[i].size());
            for (int id : hnsw_results) {
                if (bf_live[i].count(id - 1))
                    total_hits++;
            }
        }
        double recall = total_possible > 0 ? (double)total_hits / total_possible : 0.0;

        // Also measure QPS
        std::vector<double> latencies;
        latencies.reserve(RECALL_QUERIES);
        auto t0 = Clock::now();
        for (int i = 0; i < RECALL_QUERIES; ++i)
            (void)db.search(queries[i]);
        auto t1 = Clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();
        double qps = RECALL_QUERIES / secs;

        std::cout << "   " << std::setw(2) << del_pct << "% deleted | "
                  << "Recall@" << TOP_K << ": " << std::setw(5) << std::setprecision(1) << std::fixed << (recall * 100) << "% | "
                  << "QPS: " << std::setw(6) << std::setprecision(0) << qps << "\n";
    }

    cleanup(db_name);
}

// ==========================================
// 7. DATASET SIZE SCALING
// ==========================================
void bench_dataset_scaling() {
    const size_t DIM = 128;

    std::cout << "\n[" << ++bench_num << "] DATASET SIZE SCALING\n";
    print_separator();

    for (int num_vectors : {10'000, 50'000, 100'000}) {
        std::string db_name = DB_BASE + "_scale_" + std::to_string(num_vectors);
        cleanup(db_name);

        CoreEngine::RedBoxVector db(db_name + ".db", DIM, num_vectors, HNSW_M, HNSW_EF_C);
        db.set_hnsw_ef_search(HNSW_EF_S);

        auto t0 = Clock::now();
        for (int i = 0; i < num_vectors; ++i)
            db.insert_auto(rand_vec(DIM));
        auto t1 = Clock::now();
        double insert_secs = std::chrono::duration<double>(t1 - t0).count();

        db.warm_pages();

        // Search QPS
        int queries = std::min(1000, num_vectors);
        std::vector<double> latencies;
        latencies.reserve(queries);
        auto qs = rand_vec(DIM);
        for (int i = 0; i < 10; ++i) db.search(qs);

        auto t_start = Clock::now();
        for (int i = 0; i < queries; ++i)
            (void)db.search(rand_vec(DIM));
        auto t_end = Clock::now();
        double search_secs = std::chrono::duration<double>(t_end - t_start).count();

        std::cout << "   " << std::setw(7) << num_vectors << " vectors | "
                  << "Insert: " << std::setw(6) << std::setprecision(1) << std::fixed << insert_secs << "s | "
                  << "Search QPS: " << std::setw(6) << std::setprecision(0) << (queries / search_secs) << "\n";

        cleanup(db_name);
    }
}

// ==========================================
// MAIN
// ==========================================
int main() {
    std::cout << "===============================================\n";
    std::cout << "      RedBoxDb EXTENDED BENCHMARK SUITE\n";
    std::cout << "===============================================\n";

    bench_hnsw_mixed();
    bench_hnsw_heavy_deletion();
    bench_delete_throughput();
    bench_hnsw_param_sweep();
    bench_hnsw_recall_under_deletion();
    bench_dimension_scaling();
    bench_dataset_scaling();

    std::cout << "\n===============================================\n";
    std::cout << "   EXTENDED BENCHMARK COMPLETE\n";
    std::cout << "===============================================\n";

    return 0;
}
