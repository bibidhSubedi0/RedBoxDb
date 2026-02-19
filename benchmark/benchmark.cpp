#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm> // For sort (percentiles)
#include <filesystem>
#include <iomanip>
#include <numeric>   // For accumulate
#include "redboxdb/engine.hpp"

// --- CONFIGURATION ---
const int NUM_VECTORS = 100'000;    // 100k Vectors
const int DIMENSIONS = 128;        // 128-dim (Standard for AI)
const int NUM_QUERIES = 100;        // How many searches to run for averaging
const std::string DB_FILE = "benchmark_suite.db";

// Helper: Random float generator
std::vector<float> generate_random_vector(size_t dim, std::mt19937& gen, std::uniform_real_distribution<float>& dis) {
    std::vector<float> vec(dim);
    for (size_t i = 0; i < dim; ++i) {
        vec[i] = dis(gen);
    }
    return vec;
}

int main() {
    std::cout << "===============================================" << std::endl;
    std::cout << "      RedBoxDb PROFESSIONAL BENCHMARK" << std::endl;
    std::cout << "===============================================" << std::endl;
    std::cout << "Config: " << NUM_VECTORS << " vectors | " << DIMENSIONS << " dimensions" << std::endl;
    std::cout << "DB File: " << DB_FILE << std::endl;

    // cleanup
    if (std::filesystem::exists(DB_FILE)) std::filesystem::remove(DB_FILE);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    // ==========================================
    // TEST 1: WRITE THROUGHPUT
    // ==========================================
    {
        std::cout << "\n[1/3] Benchmarking INSERTION..." << std::endl;
        CoreEngine::RedBoxVector db(DB_FILE, DIMENSIONS, NUM_VECTORS);

        auto start = std::chrono::high_resolution_clock::now();

        // Batch insert
        for (int i = 0; i < NUM_VECTORS; ++i) {
            auto vec = generate_random_vector(DIMENSIONS, gen, dis);
            db.insert(i, vec);
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;

        double throughput = NUM_VECTORS / diff.count();
        std::cout << "-> Time: " << diff.count() << "s" << std::endl;
        std::cout << "-> Rate: " << (long long)throughput << " vectors/sec" << std::endl;
    }

    // ==========================================
    // TEST 2: LATENCY & QUERY PER SECOND (QPS)
    // ==========================================
    {
        std::cout << "\n[2/3] Benchmarking SEARCH LATENCY..." << std::endl;
        CoreEngine::RedBoxVector db(DB_FILE, DIMENSIONS, NUM_VECTORS);

        std::vector<double> latencies_ms;
        latencies_ms.reserve(NUM_QUERIES);

        // Warmup (run 1 query to page-in memory)
        db.search(generate_random_vector(DIMENSIONS, gen, dis));

        auto total_start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < NUM_QUERIES; ++i) {
            std::vector<float> query = generate_random_vector(DIMENSIONS, gen, dis);

            auto t1 = std::chrono::high_resolution_clock::now();
            int result = db.search(query);
            (void)result; // silence unused warning
            auto t2 = std::chrono::high_resolution_clock::now();

            std::chrono::duration<double, std::milli> ms = t2 - t1;
            latencies_ms.push_back(ms.count());
        }

        auto total_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> total_diff = total_end - total_start;

        // --- STATISTICS ---
        std::sort(latencies_ms.begin(), latencies_ms.end());

        double avg = std::accumulate(latencies_ms.begin(), latencies_ms.end(), 0.0) / latencies_ms.size();
        double min = latencies_ms.front();
        double max = latencies_ms.back();
        double p95 = latencies_ms[(size_t)(NUM_QUERIES * 0.95)];
        double p99 = latencies_ms[(size_t)(NUM_QUERIES * 0.99)];

        std::cout << "-> Queries Run: " << NUM_QUERIES << std::endl;
        std::cout << "-> QPS:         " << (NUM_QUERIES / total_diff.count()) << " q/s" << std::endl;
        std::cout << "-----------------------------------------------" << std::endl;
        std::cout << "   Min Latency: " << min << " ms" << std::endl;
        std::cout << "   Avg Latency: " << avg << " ms" << std::endl;
        std::cout << "   P95 Latency: " << p95 << " ms" << std::endl;
        std::cout << "   P99 Latency: " << p99 << " ms" << " (The metric that matters)" << std::endl;
        std::cout << "   Max Latency: " << max << " ms" << std::endl;
    }

    // ==========================================
    // TEST 3: BANDWIDTH ANALYSIS
    // ==========================================
    {
        std::cout << "\n[3/3] System Bandwidth..." << std::endl;
        // Calculation: 100k vectors * 128 dim * 4 bytes = ~51.2 MB
        double db_size_mb = (double)(NUM_VECTORS * DIMENSIONS * sizeof(float)) / (1024 * 1024);

        // We know the Avg latency from Test 2
        // If Avg is 20ms, we scan 51.2MB in 20ms.
        // GB/s = (51.2 MB / 1000) / (20 ms / 1000)

        // Let's re-measure one purely for bandwidth
        CoreEngine::RedBoxVector db(DB_FILE, DIMENSIONS, NUM_VECTORS);
        auto t1 = std::chrono::high_resolution_clock::now();
        db.search(generate_random_vector(DIMENSIONS, gen, dis));
        auto t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> seconds = t2 - t1;

        double bandwidth_gbs = (db_size_mb / 1024.0) / seconds.count();

        std::cout << "-> Data Scanned: " << db_size_mb << " MB per query" << std::endl;
        std::cout << "-> Memory Bandwidth: " << bandwidth_gbs << " GB/s" << std::endl;
    }

    return 0;
}