#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <filesystem>
#include <iomanip> // For std::fixed
#include "redboxdb/engine.hpp"

// --- CONFIGURATION ---
const int NUM_VECTORS = 100'000;    // 100k Vectors
const int DIMENSIONS = 128;        // Realistic AI Dimension
const std::string DB_FILE = "benchmark.db";

// Helper: Generate a random vector
std::vector<float> generate_random_vector(size_t dim, std::mt19937& gen, std::uniform_real_distribution<float>& dis) {
    std::vector<float> vec(dim);
    for (size_t i = 0; i < dim; ++i) {
        vec[i] = dis(gen);
    }
    return vec;
}

int main() {
    // 1. SETUP
    std::cout << "========================================" << std::endl;
    std::cout << "   RedBoxDb BENCHMARK SUITE v1.0" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Vectors:    " << NUM_VECTORS << std::endl;
    std::cout << "Dimensions: " << DIMENSIONS << std::endl;
    std::cout << "Target DB:  " << DB_FILE << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    // Clean previous run
    if (std::filesystem::exists(DB_FILE)) {
        std::filesystem::remove(DB_FILE);
    }

    // RNG Setup
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    // --- BENCHMARK: INSERTION ---
    {
        std::cout << "[TEST] Starting Insertion..." << std::endl;

        // Initialize DB with correct capacity
        CoreEngine::RedBoxVector db(DB_FILE, DIMENSIONS, NUM_VECTORS);

        auto start_time = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < NUM_VECTORS; ++i) {
            std::vector<float> vec = generate_random_vector(DIMENSIONS, gen, dis);
            db.insert(i, vec);

            if (i % 10000 == 0 && i > 0) std::cout << "." << std::flush; // Progress dots
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end_time - start_time;

        std::cout << "\n-> Inserted " << NUM_VECTORS << " vectors in "
            << std::fixed << std::setprecision(2) << diff.count() << "s" << std::endl;
        std::cout << "-> Speed: " << (NUM_VECTORS / diff.count()) << " vectors/sec" << std::endl;
    }

    // --- BENCHMARK: SEARCH (Disk I/O + Compute) ---
    {
        std::cout << "----------------------------------------" << std::endl;
        std::cout << "[TEST] Starting Search (Flat Scan)..." << std::endl;

        // Re-open DB (Simulate cold start)
        CoreEngine::RedBoxVector db(DB_FILE, DIMENSIONS, NUM_VECTORS);

        // Create a random query vector
        std::vector<float> query = generate_random_vector(DIMENSIONS, gen, dis);

        auto start_time = std::chrono::high_resolution_clock::now();

        // Perform Search
        int [[maybe_unused]] result_id = db.search(query);
        (void)result_id;

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> diff = end_time - start_time;

        std::cout << "-> Search completed in " << diff.count() << " ms" << std::endl;
        std::cout << "-> Scanned " << (NUM_VECTORS * DIMENSIONS * sizeof(float)) / (1024.0 * 1024.0)
            << " MB of data." << std::endl;

        // Latency Classification
        if (diff.count() < 10.0) std::cout << "-> Rating: EXTREMELY FAST (<10ms)" << std::endl;
        else if (diff.count() < 100.0) std::cout << "-> Rating: FAST (<100ms)" << std::endl;
        else std::cout << "-> Rating: SLOW (>100ms - Consider Indexing)" << std::endl;
    }

    return 0;
}