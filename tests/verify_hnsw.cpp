#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iomanip>
#include <set>
#include <filesystem>
#include "redboxdb/engine.hpp"

static constexpr int DIM = 128;
static constexpr int DB_SIZE = 100'000;
static constexpr int QUERIES = 200;
static constexpr int TOP_K = 100;
static constexpr uint8_t HNSW_M = 16;
static constexpr uint16_t HNSW_EF_C = 160;
static constexpr uint16_t HNSW_EF_S = 256;

using Clock = std::chrono::high_resolution_clock;

std::mt19937 rng(42);
std::uniform_real_distribution<float> dis(0.0f, 1.0f);

std::vector<float> rand_vec() {
    std::vector<float> v(DIM);
    for (auto& x : v) x = dis(rng);
    return v;
}

float l2(const float* a, const float* b) {
    float sum = 0;
    for (int i = 0; i < DIM; ++i) {
        float d = a[i] - b[i];
        sum += d * d;
    }
    return sum;
}

int main() {
    // Generate corpus and queries
    std::vector<std::vector<float>> corpus(DB_SIZE);
    for (auto& v : corpus) v = rand_vec();

    std::vector<std::vector<float>> queries(QUERIES);
    for (auto& q : queries) q = rand_vec();

    std::string db_file = "verify_hnsw.db";
    namespace fs = std::filesystem;
    fs::remove(db_file);
    fs::remove(db_file + ".del");

    // ===== INSERT SPEED =====
    std::cout << "=== INSERT SPEED ===\n";
    auto* db = new CoreEngine::RedBoxVector(db_file, DIM, DB_SIZE, HNSW_M, HNSW_EF_C);
    db->set_hnsw_ef_search(HNSW_EF_S);

    auto t0 = Clock::now();
    for (int i = 0; i < DB_SIZE; ++i)
        db->insert_auto(corpus[i]);
    auto t1 = Clock::now();
    double insert_secs = std::chrono::duration<double>(t1 - t0).count();
    double vecs_per_sec = DB_SIZE / insert_secs;
    std::cout << "  " << DB_SIZE << " vectors in " << std::fixed << std::setprecision(2) << insert_secs << "s\n";
    std::cout << "  Throughput: " << std::setprecision(0) << vecs_per_sec << " vecs/sec\n";
    bool insert_ok = vecs_per_sec >= 1500;
    std::cout << "  " << (insert_ok ? "[PASS]" : "[FAIL]") << " >= 1500 vecs/sec\n\n";

    // Fault in all mmap pages for fair QPS measurement
    db->warm_pages();

    // ===== QPS =====
    std::cout << "=== QPS ===\n";
    // Warmup
    for (int i = 0; i < 50; ++i)
        db->search(queries[i % QUERIES]);

    auto qps_start = Clock::now();
    for (int i = 0; i < QUERIES; ++i)
        db->search(queries[i]);
    auto qps_end = Clock::now();
    double search_secs = std::chrono::duration<double>(qps_end - qps_start).count();
    double qps = QUERIES / search_secs;
    std::cout << "  " << QUERIES << " queries in " << std::fixed << std::setprecision(3) << search_secs << "s\n";
    std::cout << "  QPS: " << std::setprecision(0) << qps << "\n";
    bool qps_ok = qps >= 1500;
    std::cout << "  " << (qps_ok ? "[PASS]" : "[FAIL]") << " >= 1500 QPS\n\n";

    // ===== RECALL =====
    std::cout << "=== RECALL@" << TOP_K << " ===\n";
    // Brute-force ground truth
    int total_hits = 0;
    for (int i = 0; i < QUERIES; ++i) {
        // BF top-K
        std::vector<std::pair<float, int>> bf_dists;
        bf_dists.reserve(DB_SIZE);
        for (int j = 0; j < DB_SIZE; ++j)
            bf_dists.push_back({l2(queries[i].data(), corpus[j].data()), j});
        std::partial_sort(bf_dists.begin(), bf_dists.begin() + TOP_K, bf_dists.end());
        std::set<int> true_ids;
        for (int k = 0; k < TOP_K; ++k)
            true_ids.insert(bf_dists[k].second);

        // HNSW search_N
        auto hnsw_ids_raw = db->search_N(queries[i], TOP_K);
        // search_N returns IDs (1-based), convert to 0-based indices
        std::set<int> hnsw_indices;
        for (int id : hnsw_ids_raw) {
            // Find the 0-based index for this ID
            for (int j = 0; j < DB_SIZE; ++j) {
                // IDs are 1-based: id 1 = corpus[0]
                if (j + 1 == id) {
                    hnsw_indices.insert(j);
                    break;
                }
            }
        }

        // Compare
        int hits = 0;
        for (int idx : hnsw_indices)
            if (true_ids.count(idx)) ++hits;
        total_hits += hits;
    }
    double recall = (double)total_hits / (QUERIES * TOP_K);
    std::cout << "  Recall@" << TOP_K << ": " << std::fixed << std::setprecision(1) << recall * 100 << "%\n";
    bool recall_ok = recall >= 0.85;
    std::cout << "  " << (recall_ok ? "[PASS]" : "[FAIL]") << " >= 85%\n\n";

    // ===== SUMMARY =====
    std::cout << "=== SUMMARY ===\n";
    std::cout << "  Insert: " << (insert_ok ? "PASS" : "FAIL") << " (" << std::setprecision(0) << vecs_per_sec << " vecs/sec)\n";
    std::cout << "  QPS:    " << (qps_ok ? "PASS" : "FAIL") << " (" << std::setprecision(0) << qps << " qps)\n";
    std::cout << "  Recall: " << (recall_ok ? "PASS" : "FAIL") << " (" << std::fixed << std::setprecision(1) << recall * 100 << "%)\n";
    bool all_pass = insert_ok && qps_ok && recall_ok;
    std::cout << "\n  Overall: " << (all_pass ? "ALL PASS" : "SOME FAILED") << "\n";

    delete db;
    fs::remove(db_file);
    fs::remove(db_file + ".del");

    return all_pass ? 0 : 1;
}
