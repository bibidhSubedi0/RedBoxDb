#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <numeric>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <cmath>
#include <sstream>
#include <set>
#include <spdlog/spdlog.h>
#include "redboxdb/engine.hpp"

// ==========================================
// CONFIGURATION (matches production benchmarks)
// ==========================================
static const int     NUM_VECTORS   = 100'000;
static const int     DIM           = 128;
static const int     QUERIES_PER_T = 1'000;
static const int     RECALL_QUERIES = 100;
static const int     TOP_K         = 100;
static const uint8_t HNSW_M       = 16;
static const uint16_t HNSW_EF_C   = 160;
static const uint16_t HNSW_EF_S   = 256;

using Clock = std::chrono::high_resolution_clock;

// ==========================================
// HELPERS
// ==========================================
std::mt19937 rng(42);
std::uniform_real_distribution<float> dis(0.0f, 1.0f);

std::vector<float> rand_vec() {
    std::vector<float> v(DIM);
    for (auto& x : v) x = dis(rng);
    return v;
}

void cleanup(const std::string& name) {
    std::string db = name + ".db";
    std::string del = name + ".db.del";
    if (std::filesystem::exists(db))  std::filesystem::remove(db);
    if (std::filesystem::exists(del)) std::filesystem::remove(del);
}

// Measure QPS: each thread runs its own set of queries, wall-clock time
double measure_qps(CoreEngine::RedBoxVector* db,
                   const std::vector<std::vector<float>>& queries,
                   int num_threads, int queries_per_thread)
{
    std::vector<std::thread> threads;
    auto t0 = Clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            int base = t * queries_per_thread;
            for (int i = 0; i < queries_per_thread; ++i)
                (void)db->search(queries[base + i]);
        });
    }
    for (auto& t : threads) t.join();
    auto t1 = Clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    return (double)(num_threads * queries_per_thread) / secs;
}

// Measure insert throughput
double measure_insert_hnsw(const std::vector<std::vector<float>>& corpus) {
    std::string name = "ci_bench_hnsw_insert";
    cleanup(name);
    CoreEngine::RedBoxVector db(name + ".db", DIM, NUM_VECTORS, HNSW_M, HNSW_EF_C);
    auto t0 = Clock::now();
    for (auto& v : corpus) db.insert_auto(v);
    auto t1 = Clock::now();
    cleanup(name);
    return (double)NUM_VECTORS / std::chrono::duration<double>(t1 - t0).count();
}

double measure_insert_ivf(const std::vector<std::vector<float>>& corpus) {
    std::string name = "ci_bench_ivf_insert";
    cleanup(name);
    CoreEngine::RedBoxVector db(name + ".db", DIM, NUM_VECTORS);
    auto t0 = Clock::now();
    for (auto& v : corpus) db.insert_auto(v);
    auto t1 = Clock::now();
    cleanup(name);
    return (double)NUM_VECTORS / std::chrono::duration<double>(t1 - t0).count();
}

// Measure HNSW recall@K vs brute-force
double measure_recall(CoreEngine::RedBoxVector* db,
                      const std::vector<std::vector<float>>& corpus,
                      const std::vector<std::vector<float>>& queries)
{
    int total_hits = 0;
    int total_possible = 0;
    for (int i = 0; i < RECALL_QUERIES; ++i) {
        auto hnsw_results = db->search_N(queries[i], TOP_K);
        // Brute-force top-K
        std::vector<std::pair<float, int>> dists;
        dists.reserve(NUM_VECTORS);
        for (int j = 0; j < NUM_VECTORS; ++j) {
            float d = 0;
            for (size_t d2 = 0; d2 < DIM; ++d2) {
                float diff = queries[i][d2] - corpus[j][d2];
                d += diff * diff;
            }
            dists.push_back({d, j});
        }
        int k = std::min(TOP_K, (int)dists.size());
        std::partial_sort(dists.begin(), dists.begin() + k, dists.end());
        std::set<int> bf_set;
        for (int x = 0; x < k; ++x) bf_set.insert(dists[x].second);

        total_possible += k;
        for (int id : hnsw_results) {
            if (bf_set.count(id - 1)) total_hits++;
        }
    }
    return total_possible > 0 ? (double)total_hits / total_possible : 0.0;
}

// ==========================================
// JSON OUTPUT
// ==========================================
std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

// ==========================================
// MAIN
// ==========================================
int main() {
    spdlog::set_level(spdlog::level::off);
    std::cerr << "CiBench: generating corpus..." << std::flush;
    std::vector<std::vector<float>> corpus(NUM_VECTORS);
    for (auto& v : corpus) v = rand_vec();

    int hw_threads = std::max(1u, std::thread::hardware_concurrency());
    int total_queries = hw_threads * QUERIES_PER_T;
    std::vector<std::vector<float>> queries(total_queries);
    for (auto& q : queries) q = rand_vec();

    std::vector<std::vector<float>> recall_queries(RECALL_QUERIES);
    for (auto& q : recall_queries) q = rand_vec();

    std::cerr << " done.\n";

    // === HNSW ===
    std::cerr << "CiBench: HNSW insert..." << std::flush;
    double hnsw_ins = measure_insert_hnsw(corpus);
    std::cerr << " done.\n";

    std::string hnsw_name = "ci_bench_hnsw_search";
    cleanup(hnsw_name);
    auto* hnsw_db = new CoreEngine::RedBoxVector(hnsw_name + ".db", DIM, NUM_VECTORS, HNSW_M, HNSW_EF_C);
    hnsw_db->set_hnsw_ef_search(HNSW_EF_S);
    for (auto& v : corpus) hnsw_db->insert_auto(v);
    hnsw_db->warm_pages();

    std::cerr << "CiBench: HNSW search 1T..." << std::flush;
    double hnsw_qps_1t = measure_qps(hnsw_db, queries, 1, QUERIES_PER_T);
    std::cerr << " done.\n";

    std::cerr << "CiBench: HNSW search " << hw_threads << "T..." << std::flush;
    double hnsw_qps_nt = measure_qps(hnsw_db, queries, hw_threads, QUERIES_PER_T);
    std::cerr << " done.\n";

    std::cerr << "CiBench: HNSW recall..." << std::flush;
    double hnsw_recall = measure_recall(hnsw_db, corpus, recall_queries);
    std::cerr << " done.\n";

    delete hnsw_db;
    cleanup(hnsw_name);

    // === IVF ===
    std::cerr << "CiBench: IVF insert..." << std::flush;
    double ivf_ins = measure_insert_ivf(corpus);
    std::cerr << " done.\n";

    std::string ivf_name = "ci_bench_ivf_search";
    cleanup(ivf_name);
    auto* ivf_db = new CoreEngine::RedBoxVector(ivf_name + ".db", DIM, NUM_VECTORS);
    for (auto& v : corpus) ivf_db->insert_auto(v);
    ivf_db->warm_pages();

    std::cerr << "CiBench: IVF search 1T..." << std::flush;
    double ivf_qps_1t = measure_qps(ivf_db, queries, 1, QUERIES_PER_T);
    std::cerr << " done.\n";

    std::cerr << "CiBench: IVF search " << hw_threads << "T..." << std::flush;
    double ivf_qps_nt = measure_qps(ivf_db, queries, hw_threads, QUERIES_PER_T);
    std::cerr << " done.\n";

    delete ivf_db;
    cleanup(ivf_name);

    // === OUTPUT JSON TO STDOUT ===
    std::cout << std::fixed << std::setprecision(0)
        << "{"
        << "\"hnsw_insert_per_sec\":" << hnsw_ins << ","
        << "\"hnsw_qps_1t\":" << hnsw_qps_1t << ","
        << "\"hnsw_qps_nt\":" << hnsw_qps_nt << ","
        << std::setprecision(4)
        << "\"hnsw_recall_100\":" << hnsw_recall << ","
        << std::setprecision(0)
        << "\"ivf_insert_per_sec\":" << ivf_ins << ","
        << "\"ivf_qps_1t\":" << ivf_qps_1t << ","
        << "\"ivf_qps_nt\":" << ivf_qps_nt << ","
        << "\"threads\":" << hw_threads << ","
        << "\"num_vectors\":" << NUM_VECTORS << ","
        << "\"dimensions\":" << DIM
        << "}\n";

    std::cerr << "CiBench: all done.\n";
    return 0;
}
