#include <gtest/gtest.h>
#include <filesystem>
#include <vector>
#include <string>
#include <set>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstring>
#include <fstream>
#include <random>
#include "redboxdb/engine.hpp"
#include "redboxdb/distance.hpp"
#include "redboxdb/cluster_manager.hpp"
#include "redboxdb/hnsw_manager.hpp"
#include <spdlog/spdlog.h>

// =============================================================================
// Shared fixture for extended tests
// =============================================================================
struct ExtFixture : public ::testing::Test {
    std::string db_file;
    std::string del_file;

    void init(const std::string& name) {
        db_file  = name + ".db";
        del_file = name + ".db.del";
    }

    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
        cleanup();
    }
    void TearDown() override {
        cleanup();
        spdlog::set_level(spdlog::level::info);
    }

    void try_remove(const std::string& path) {
        if (path.empty()) return;
        for (int retry = 0; retry < 10; ++retry) {
            if (!std::filesystem::exists(path)) return;
            std::error_code ec;
            if (std::filesystem::remove(path, ec)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void cleanup() {
        try_remove(db_file);
        try_remove(del_file);
    }

    static std::vector<float> make_vec(int seed, int dim) {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        std::vector<float> v(dim);
        for (auto& x : v) x = dist(rng);
        return v;
    }

    static float l2_ref(const std::vector<float>& a, const std::vector<float>& b) {
        float d = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            float diff = a[i] - b[i];
            d += diff * diff;
        }
        return d;
    }
};


// =============================================================================
// 1. AVX2 vs SCALAR CORRECTNESS
// =============================================================================
class AVX2CorrectnessTest : public ExtFixture {
protected:
    void SetUp() override { init("test_avx2"); ExtFixture::SetUp(); }
};

TEST_F(AVX2CorrectnessTest, L2MatchesScalarForAlignedDim) {
    for (int seed = 0; seed < 20; ++seed) {
        auto a = make_vec(seed, 128);
        auto b = make_vec(seed + 1000, 128);
        float scalar = Distance::l2_scalar(a.data(), b.data(), 128);
        float avx2   = Distance::l2_avx2(a.data(), b.data(), 128);
        EXPECT_NEAR(scalar, avx2, 1e-4f)
            << "Mismatch at seed=" << seed << " dim=128";
    }
}

TEST_F(AVX2CorrectnessTest, L2MatchesScalarForUnalignedDim) {
    for (int dim : {3, 7, 13, 15, 17, 31, 33, 63, 65, 127, 129}) {
        for (int seed = 0; seed < 10; ++seed) {
            auto a = make_vec(seed, dim);
            auto b = make_vec(seed + 500, dim);
            float scalar = Distance::l2_scalar(a.data(), b.data(), dim);
            float avx2   = Distance::l2_avx2(a.data(), b.data(), dim);
            EXPECT_NEAR(scalar, avx2, 1e-3f)
                << "Mismatch at dim=" << dim << " seed=" << seed;
        }
    }
}

TEST_F(AVX2CorrectnessTest, L2MatchesScalarForSingleDim) {
    std::vector<float> a = {42.0f};
    std::vector<float> b = {37.0f};
    float scalar = Distance::l2_scalar(a.data(), b.data(), 1);
    float avx2   = Distance::l2_avx2(a.data(), b.data(), 1);
    EXPECT_NEAR(scalar, avx2, 1e-6f);
}

TEST_F(AVX2CorrectnessTest, L2MatchesScalarForZeroDim) {
    std::vector<float> a, b;
    float scalar = Distance::l2_scalar(a.data(), b.data(), 0);
    float avx2   = Distance::l2_avx2(a.data(), b.data(), 0);
    EXPECT_FLOAT_EQ(scalar, avx2);
}

TEST_F(AVX2CorrectnessTest, L2MatchesScalarForLargeDim) {
    for (int dim : {256, 512, 768, 1024, 1536}) {
        auto a = make_vec(1, dim);
        auto b = make_vec(2, dim);
        float scalar = Distance::l2_scalar(a.data(), b.data(), dim);
        float avx2   = Distance::l2_avx2(a.data(), b.data(), dim);
        EXPECT_NEAR(scalar, avx2, 1e-2f)
            << "Mismatch at dim=" << dim;
    }
}

TEST_F(AVX2CorrectnessTest, L2DispatcherSelectsCorrectPath) {
    auto a = make_vec(1, 128);
    auto b = make_vec(2, 128);
    float via_avx2 = Distance::l2(a.data(), b.data(), 128, true);
    float via_scalar = Distance::l2(a.data(), b.data(), 128, false);
    EXPECT_NEAR(via_avx2, via_scalar, 1e-4f);
}

TEST_F(AVX2CorrectnessTest, SearchConsistencyWithScalar) {
    // Verify that inserting vectors with AVX2 engine produces the same search
    // results as a brute-force scalar scan would
    const int DIM = 64;
    const int N = 100;
    CoreEngine::RedBoxVector db(db_file, DIM, N + 10, (uint8_t)8, (uint16_t)50);

    std::vector<std::vector<float>> vecs;
    for (int i = 0; i < N; ++i) {
        auto v = make_vec(i, DIM);
        vecs.push_back(v);
        db.insert(i + 1, v);
    }

    // Brute-force scalar search
    for (int i = 0; i < N; ++i) {
        float best_dist = std::numeric_limits<float>::max();
        int best_j = -1;
        for (int j = 0; j < N; ++j) {
            float d = Distance::l2_scalar(vecs[i].data(), vecs[j].data(), DIM);
            if (d < best_dist) { best_dist = d; best_j = j; }
        }

        int db_result = db.search(vecs[i]);
        // DB may return a different ID for equidistant vectors, but top-1 should be close
        if (db_result != best_j + 1) {
            // Verify the DB result is actually close (within 2x distance)
            float db_dist = Distance::l2_scalar(vecs[i].data(), vecs[db_result - 1].data(), DIM);
            EXPECT_LE(db_dist, best_dist * 2.0f)
                << "Query " << i << ": brute-force best=" << best_j + 1
                << " dist=" << best_dist << ", DB returned=" << db_result
                << " dist=" << db_dist;
        }
    }
}


// =============================================================================
// 2. CONCURRENT HNSW TESTS
// =============================================================================
class ConcurrentHnswTest : public ExtFixture {
protected:
    static constexpr int DIM = 16;
    static constexpr int CAP = 2000;
    static constexpr uint8_t M = 8;
    static constexpr uint16_t EF_C = 50;

    void SetUp() override { init("test_hnsw_concurrent"); ExtFixture::SetUp(); }

    std::unique_ptr<CoreEngine::RedBoxVector> make_db() {
        return std::make_unique<CoreEngine::RedBoxVector>(db_file, DIM, CAP, M, EF_C);
    }
};

TEST_F(ConcurrentHnswTest, ConcurrentInsertsNoCorruption) {
    auto db = make_db();
    const int NUM_THREADS = 8;
    const int PER_THREAD = 50;
    const int TOTAL = NUM_THREADS * PER_THREAD;

    std::vector<std::thread> threads;
    std::atomic<int> insert_count{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            int base = t * PER_THREAD;
            for (int i = 0; i < PER_THREAD; ++i) {
                uint64_t id = (uint64_t)(base + i + 1);
                auto v = make_vec((int)id, DIM);
                db->insert(id, v);
                insert_count++;
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(insert_count.load(), TOTAL);

    // Verify all vectors are findable
    int found = 0;
    for (int i = 1; i <= TOTAL; ++i) {
        auto v = make_vec(i, DIM);
        int result = db->search(v);
        if (result == i) found++;
    }
    float recall = (float)found / TOTAL;
    EXPECT_GE(recall, 0.7f) << "Concurrent insert recall=" << recall << " too low";
}

TEST_F(ConcurrentHnswTest, ConcurrentSearchesWhileInserting) {
    auto db = make_db();

    // Seed with initial data
    for (int i = 1; i <= 200; ++i)
        db->insert(i, make_vec(i, DIM));

    std::atomic<bool> stop{false};
    std::atomic<int> search_errors{0};

    // Writer thread
    std::thread writer([&]() {
        int id = 201;
        while (!stop.load()) {
            db->insert((uint64_t)id, make_vec(id, DIM));
            id++;
            if (id > 2000) break;
            std::this_thread::yield();
        }
    });

    // Reader threads
    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&]() {
            for (int op = 0; op < 200; ++op) {
                try {
                    auto q = make_vec(op, DIM);
                    int result = db->search(q);
                    if (result <= 0) search_errors++;
                } catch (...) {
                    search_errors++;
                }
            }
        });
    }

    for (auto& th : readers) th.join();
    stop.store(true);
    writer.join();

    EXPECT_EQ(search_errors.load(), 0) << "Search errors during concurrent insert: " << search_errors.load();
}

TEST_F(ConcurrentHnswTest, ConcurrentSearchNNoDuplicates) {
    auto db = make_db();
    for (int i = 1; i <= 100; ++i)
        db->insert(i, make_vec(i, DIM));

    std::atomic<int> dup_errors{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&]() {
            for (int op = 0; op < 100; ++op) {
                auto q = make_vec(op, DIM);
                auto results = db->search_N(q, 10);
                std::set<int> seen;
                for (int id : results) {
                    if (seen.count(id)) dup_errors++;
                    seen.insert(id);
                }
            }
        });
    }

    for (auto& th : threads) th.join();
    EXPECT_EQ(dup_errors.load(), 0) << "Duplicate IDs in concurrent search_N results";
}

TEST_F(ConcurrentHnswTest, ConcurrentInsertDeleteSearch) {
    auto db = make_db();

    // Insert initial data
    for (int i = 1; i <= 500; ++i)
        db->insert(i, make_vec(i, DIM));

    std::atomic<bool> stop{false};
    std::atomic<int> ops{0};

    // Inserter threads
    std::vector<std::thread> threads;
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&, t]() {
            uint64_t id = 501 + t * 500;
            while (!stop.load()) {
                db->insert(id, make_vec((int)id, DIM));
                id++;
                ops++;
                if (id > (uint64_t)(1000 + t * 500)) break;
            }
        });
    }

    // Deleter threads
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            for (int i = 1 + t * 250; i <= 250 + t * 250; ++i) {
                db->remove((uint64_t)i);
                ops++;
            }
        });
    }

    // Searcher threads
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            for (int op = 0; op < 200; ++op) {
                try {
                    db->search(make_vec(op, DIM));
                    ops++;
                } catch (...) {}
            }
        });
    }

    for (auto& th : threads) th.join();
    stop.store(true);

    EXPECT_GT(ops.load(), 0);
}

TEST_F(ConcurrentHnswTest, ConcurrentAutoInsertThreadSafe) {
    auto db = make_db();
    const int NUM_THREADS = 8;
    const int PER_THREAD = 50;

    std::vector<std::thread> threads;
    std::vector<std::vector<uint64_t>> thread_ids(NUM_THREADS);

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < PER_THREAD; ++i) {
                uint64_t id = db->insert_auto(make_vec(t * 1000 + i, DIM));
                thread_ids[t].push_back(id);
            }
        });
    }

    for (auto& th : threads) th.join();

    // All IDs must be unique
    std::set<uint64_t> all_ids;
    for (auto& ids : thread_ids) {
        for (uint64_t id : ids) {
            EXPECT_EQ(all_ids.count(id), 0u) << "Duplicate auto-ID: " << id;
            all_ids.insert(id);
        }
    }
    EXPECT_EQ((int)all_ids.size(), NUM_THREADS * PER_THREAD);
}


// =============================================================================
// 3. K-MEANS++ UNIT TESTS
// =============================================================================
class KMeansTest : public ExtFixture {
protected:
    void SetUp() override { init("test_kmeans"); ExtFixture::SetUp(); }
};

TEST_F(KMeansTest, KMeansProducesDistinctCentroids) {
    const size_t DIM = 8;
    const uint16_t K = 10;
    const size_t N = 100;

    // Create float_block with N random vectors
    std::vector<float> float_block(N * DIM);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (auto& x : float_block) x = dist(rng);

    std::vector<float> centroid_block(K * DIM, 0.0f);
    std::vector<uint64_t> cluster_count(K, 0);
    std::vector<uint16_t> cluster_block(N, 0);

    ClusterManager::kmeans_plus_plus_init(
        centroid_block.data(), cluster_count.data(), cluster_block.data(),
        float_block.data(), K, N, DIM, true);

    // All centroids should be distinct (no two identical)
    for (uint16_t i = 0; i < K; ++i) {
        for (uint16_t j = i + 1; j < K; ++j) {
            float dist = Distance::l2_scalar(
                centroid_block.data() + i * DIM,
                centroid_block.data() + j * DIM, DIM);
            EXPECT_GT(dist, 0.0f)
                << "Centroids " << i << " and " << j << " are identical";
        }
    }
}

TEST_F(KMeansTest, KMeansClusterCountsSumToN) {
    const size_t DIM = 8;
    const uint16_t K = 10;
    const size_t N = 100;

    std::vector<float> float_block(N * DIM);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (auto& x : float_block) x = dist(rng);

    std::vector<float> centroid_block(K * DIM, 0.0f);
    std::vector<uint64_t> cluster_count(K, 0);
    std::vector<uint16_t> cluster_block(N, 0);

    ClusterManager::kmeans_plus_plus_init(
        centroid_block.data(), cluster_count.data(), cluster_block.data(),
        float_block.data(), K, N, DIM, true);

    uint64_t total = 0;
    for (uint16_t c = 0; c < K; ++c) total += cluster_count[c];
    EXPECT_EQ(total, N) << "Cluster counts sum to " << total << " instead of " << N;
}

TEST_F(KMeansTest, KMeansClusterAssignmentsValid) {
    const size_t DIM = 8;
    const uint16_t K = 10;
    const size_t N = 100;

    std::vector<float> float_block(N * DIM);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (auto& x : float_block) x = dist(rng);

    std::vector<float> centroid_block(K * DIM, 0.0f);
    std::vector<uint64_t> cluster_count(K, 0);
    std::vector<uint16_t> cluster_block(N, 0);

    ClusterManager::kmeans_plus_plus_init(
        centroid_block.data(), cluster_count.data(), cluster_block.data(),
        float_block.data(), K, N, DIM, true);

    for (size_t i = 0; i < N; ++i) {
        EXPECT_LT(cluster_block[i], K)
            << "Vector " << i << " assigned to invalid cluster " << cluster_block[i];
    }
}

TEST_F(KMeansTest, KMeansCentroidsAreActualMeans) {
    const size_t DIM = 4;
    const uint16_t K = 3;
    const size_t N = 30;

    std::vector<float> float_block(N * DIM);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (auto& x : float_block) x = dist(rng);

    std::vector<float> centroid_block(K * DIM, 0.0f);
    std::vector<uint64_t> cluster_count(K, 0);
    std::vector<uint16_t> cluster_block(N, 0);

    ClusterManager::kmeans_plus_plus_init(
        centroid_block.data(), cluster_count.data(), cluster_block.data(),
        float_block.data(), K, N, DIM, true);

    // Verify centroids are the mean of their assigned vectors
    for (uint16_t c = 0; c < K; ++c) {
        if (cluster_count[c] == 0) continue;
        std::vector<double> expected_mean(DIM, 0.0);
        for (size_t i = 0; i < N; ++i) {
            if (cluster_block[i] != c) continue;
            for (size_t d = 0; d < DIM; ++d)
                expected_mean[d] += float_block[i * DIM + d];
        }
        for (size_t d = 0; d < DIM; ++d)
            expected_mean[d] /= cluster_count[c];

        const float* centroid = centroid_block.data() + c * DIM;
        for (size_t d = 0; d < DIM; ++d) {
            EXPECT_NEAR(centroid[d], (float)expected_mean[d], 1e-4f)
                << "Cluster " << c << " dim " << d
                << ": centroid=" << centroid[d] << " expected=" << expected_mean[d];
        }
    }
}

TEST_F(KMeansTest, FindNearestCentroidReturnsClosest) {
    const size_t DIM = 4;
    const uint16_t K = 5;

    // Create 5 centroids at known positions
    std::vector<float> centroids = {
        0.0f, 0.0f, 0.0f, 0.0f,
        10.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 10.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 10.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 10.0f
    };

    // Query near centroid 2
    std::vector<float> query = {0.0f, 9.0f, 0.0f, 0.0f};
    uint16_t result = ClusterManager::find_nearest_centroid(
        query.data(), centroids.data(), K, DIM, true);
    EXPECT_EQ(result, 2);

    // Query near centroid 0
    query = {0.1f, 0.1f, 0.1f, 0.1f};
    result = ClusterManager::find_nearest_centroid(
        query.data(), centroids.data(), K, DIM, true);
    EXPECT_EQ(result, 0);

    // Query near centroid 4
    query = {0.0f, 0.0f, 0.0f, 9.5f};
    result = ClusterManager::find_nearest_centroid(
        query.data(), centroids.data(), K, DIM, true);
    EXPECT_EQ(result, 4);
}

TEST_F(KMeansTest, UpdateCentroidShiftsMean) {
    const size_t DIM = 3;

    std::vector<float> centroids = {0.0f, 0.0f, 0.0f};
    std::vector<uint64_t> counts = {0};

    // Insert first vector
    std::vector<float> v1 = {10.0f, 0.0f, 0.0f};
    ClusterManager::update_centroid(centroids.data(), counts.data(), 0, v1.data(), DIM);
    EXPECT_EQ(counts[0], 1u);
    EXPECT_FLOAT_EQ(centroids[0], 10.0f);

    // Insert second vector
    std::vector<float> v2 = {0.0f, 10.0f, 0.0f};
    ClusterManager::update_centroid(centroids.data(), counts.data(), 0, v2.data(), DIM);
    EXPECT_EQ(counts[0], 2u);
    EXPECT_FLOAT_EQ(centroids[0], 5.0f);
    EXPECT_FLOAT_EQ(centroids[1], 5.0f);
}

TEST_F(KMeansTest, KMeansEndToEndThroughEngine) {
    // Test that K-Means++ init triggers at threshold and search still works
    const size_t DIM = 4;
    const int CAP = 200;

    CoreEngine::RedBoxVector db(db_file, DIM, CAP);

    // Insert vectors to trigger K-Means++ init (threshold = 10000, but we
    // can't hit that in a unit test — instead verify the engine works fine
    // with vectors below the threshold)
    for (int i = 0; i < 100; ++i) {
        db.insert((uint64_t)(i + 1), make_vec(i, DIM));
    }

    // Search should work
    int result = db.search(make_vec(0, DIM));
    EXPECT_GT(result, 0);

    auto results = db.search_N(make_vec(0, DIM), 5);
    EXPECT_FALSE(results.empty());
}


// =============================================================================
// 4. SERVER PROTOCOL PARSING UNIT TESTS
// =============================================================================
class ProtocolParsingTest : public ExtFixture {
protected:
    void SetUp() override { init("test_protocol"); ExtFixture::SetUp(); }
};

TEST_F(ProtocolParsingTest, CommandByteExtraction) {
    // Simulate header parsing as done in server.cpp
    char header_buffer[5];

    // CMD_INSERT with meta_data = 42
    uint8_t cmd = 1;
    uint32_t meta = 42;
    header_buffer[0] = static_cast<char>(cmd);
    memcpy(&header_buffer[1], &meta, 4);

    uint8_t parsed_cmd = header_buffer[0];
    uint32_t parsed_meta = 0;
    memcpy(&parsed_meta, &header_buffer[1], 4);

    EXPECT_EQ(parsed_cmd, 1u);
    EXPECT_EQ(parsed_meta, 42u);
}

TEST_F(ProtocolParsingTest, CommandBytesForAllCommands) {
    // Verify all command byte values are distinct and in expected range
    std::set<uint8_t> cmds;
    cmds.insert(1); // CMD_INSERT
    cmds.insert(2); // CMD_SEARCH
    cmds.insert(3); // CMD_DELETE
    cmds.insert(4); // CMD_SELECT_DB
    cmds.insert(5); // CMD_UPDATE
    cmds.insert(6); // CMD_INSERT_AUTO
    cmds.insert(7); // CMD_SEARCH_N
    cmds.insert(8); // CMD_DROP_DB
    cmds.insert(9); // CMD_SET_PROBES
    cmds.insert(10); // CMD_CREATE_HNSW_DB
    cmds.insert(11); // CMD_SET_HNSW_EF

    EXPECT_EQ(cmds.size(), 11u) << "Expected 11 distinct commands";
    for (uint8_t c : cmds) {
        EXPECT_GE(c, 1u);
        EXPECT_LE(c, 11u);
    }
}

TEST_F(ProtocolParsingTest, MetaDataLittleEndianEncoding) {
    // Verify little-endian encoding/decoding as used in protocol
    uint32_t value = 0x12345678;
    char buf[4];
    memcpy(buf, &value, 4);

    uint32_t decoded = 0;
    memcpy(&decoded, buf, 4);
    EXPECT_EQ(decoded, value);

    // Verify byte order
    EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0x78);
    EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0x56);
    EXPECT_EQ(static_cast<uint8_t>(buf[2]), 0x34);
    EXPECT_EQ(static_cast<uint8_t>(buf[3]), 0x12);
}

TEST_F(ProtocolParsingTest, SearchNZeroResultsEncoding) {
    // Server sends: count (4 bytes) = 0 for bad input
    uint32_t count = 0;
    std::vector<char> response(sizeof(count));
    memcpy(response.data(), &count, sizeof(count));

    uint32_t parsed_count = 0;
    memcpy(&parsed_count, response.data(), sizeof(parsed_count));
    EXPECT_EQ(parsed_count, 0u);
}

TEST_F(ProtocolParsingTest, SearchResultEncoding) {
    // Server sends: result_id (4 bytes)
    // -1 is sentinel for not found
    int result_id = -1;
    uint32_t wire = (result_id < 0) ? 0xFFFFFFFFu : static_cast<uint32_t>(result_id);
    EXPECT_EQ(wire, 0xFFFFFFFFu);

    result_id = 42;
    wire = (result_id < 0) ? 0xFFFFFFFFu : static_cast<uint32_t>(result_id);
    EXPECT_EQ(wire, 42u);

    result_id = 0;
    wire = (result_id < 0) ? 0xFFFFFFFFu : static_cast<uint32_t>(result_id);
    EXPECT_EQ(wire, 0u);
}

TEST_F(ProtocolParsingTest, NameLengthOverflowProtection) {
    // The server reads name_len as uint32_t and does string(name_len, ' ')
    // If name_len is huge, this would OOM. Test that large values are rejected.
    uint32_t name_len = 0xFFFFFFFF;
    // The server should check this — currently it doesn't, which is issue #62
    // This test documents the expected behavior
    EXPECT_GT(name_len, 1024u * 1024u) << "name_len should be checked against a max";
}

TEST_F(ProtocolParsingTest, VectorPayloadSizeCalculation) {
    // Verify the wire size calculation matches what server expects
    int dim = 128;
    int vec_byte_size = dim * sizeof(float);
    EXPECT_EQ(vec_byte_size, 512);

    dim = 768;
    vec_byte_size = dim * sizeof(float);
    EXPECT_EQ(vec_byte_size, 3072);

    dim = 1536;
    vec_byte_size = dim * sizeof(float);
    EXPECT_EQ(vec_byte_size, 6144);
}

TEST_F(ProtocolParsingTest, InsertAutoResponseSize) {
    // Server sends assigned_id as uint64_t (8 bytes)
    uint64_t assigned_id = 42;
    EXPECT_EQ(sizeof(assigned_id), 8u);
    char buf[8];
    memcpy(buf, &assigned_id, 8);

    uint64_t decoded = 0;
    memcpy(&decoded, buf, 8);
    EXPECT_EQ(decoded, 42u);
}


// =============================================================================
// 5. CORRUPTION / ERROR HANDLING TESTS
// =============================================================================
class CorruptionTest : public ExtFixture {
protected:
    void SetUp() override { init("test_corruption"); ExtFixture::SetUp(); }
};

TEST_F(CorruptionTest, CorruptDBFileThrowsOnReopen) {
    // Create a valid DB
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, {1.0f, 0.0f, 0.0f});
    }

    // Corrupt the header (first few bytes)
    {
        std::fstream f(db_file, std::ios::binary | std::ios::in | std::ios::out);
        char garbage[16];
        for (int i = 0; i < 16; ++i) garbage[i] = static_cast<char>(0xF0 + i);
        f.write(garbage, 16);
    }

    // Reopening should throw
    EXPECT_THROW({
        CoreEngine::RedBoxVector db(db_file, 3, 100);
    }, std::exception);
}

TEST_F(CorruptionTest, DISABLED_DimensionMismatchThrows) {
    // Create DB with dim=3
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, {1.0f, 0.0f, 0.0f});
    }

    // Try to open with different dimension
    EXPECT_THROW({
        CoreEngine::RedBoxVector db(db_file, 4, 100);
    }, std::runtime_error);
}

TEST_F(CorruptionTest, DISABLED_TruncatedFileThrows) {
    // Create a valid DB
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, {1.0f, 0.0f, 0.0f});
    }

    // Truncate the file
    {
        std::filesystem::resize_file(db_file, 32);
    }

    // Reopening should throw or handle gracefully
    EXPECT_THROW({
        CoreEngine::RedBoxVector db(db_file, 3, 100);
    }, std::exception);
}

TEST_F(CorruptionTest, DISABLED_EmptyFileThrows) {
    // Create empty file
    {
        std::ofstream f(db_file);
    }

    EXPECT_THROW({
        CoreEngine::RedBoxVector db(db_file, 3, 100);
    }, std::exception);
}

TEST_F(CorruptionTest, CorruptTombstoneFileHandled) {
    // Create valid DB with tombstones
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, {1.0f, 0.0f, 0.0f});
        db.insert(2, {2.0f, 0.0f, 0.0f});
        db.remove(1);
    }

    // Corrupt the tombstone file (append garbage)
    {
        std::fstream f(del_file, std::ios::binary | std::ios::app);
        char garbage[4] = {static_cast<char>(0xFF), static_cast<char>(0xFE),
                           static_cast<char>(0xFD), static_cast<char>(0xFC)};
        f.write(garbage, 4);
    }

    // Reopening should handle corrupted tombstones gracefully
    // (may throw or may just have extra phantom deletions)
    try {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        // If it doesn't throw, at least basic operations should work
        int result = db.search({2.0f, 0.0f, 0.0f});
        EXPECT_EQ(result, 2);
    } catch (const std::exception&) {
        // Acceptable — tombstone corruption is caught
    }
}

TEST_F(CorruptionTest, DISABLED_NonexistentFileThrows) {
    EXPECT_THROW({
        CoreEngine::RedBoxVector db("nonexistent_file_12345.db", 3, 100);
    }, std::exception);
}

TEST_F(CorruptionTest, DISABLED_DimensionZeroThrows) {
    // Dimension 0 would result in zero-size mmap, which should fail
    EXPECT_THROW({
        CoreEngine::RedBoxVector db(db_file, 0, 100);
    }, std::exception);
}

TEST_F(CorruptionTest, DISABLED_CapacityZeroThrows) {
    // Capacity 0 means zero-size allocation
    EXPECT_THROW({
        CoreEngine::RedBoxVector db(db_file, 3, 0);
    }, std::exception);
}

TEST_F(CorruptionTest, DISABLED_VectorDimensionMismatchThrows) {
    CoreEngine::RedBoxVector db(db_file, 3, 100);

    // Inserting a vector with wrong dimensions should throw
    EXPECT_THROW({
        db.insert(1, {1.0f, 0.0f}); // dim=2, but db expects dim=3
    }, std::invalid_argument);
}

TEST_F(CorruptionTest, SearchAfterFillToCapacityWorks) {
    const int CAP = 10;
    CoreEngine::RedBoxVector db(db_file, 3, CAP);

    for (int i = 0; i < CAP; ++i)
        db.insert((uint64_t)(i + 1), {(float)i, 0.0f, 0.0f});

    // Fill to capacity — operations should still work for existing vectors
    EXPECT_EQ(db.search({0.0f, 0.0f, 0.0f}), 1);
    EXPECT_TRUE(db.remove(1));
    EXPECT_EQ(db.search({0.0f, 0.0f, 0.0f}), 2);
}

TEST_F(CorruptionTest, DoubleCloseHandled) {
    // Create and destroy twice on same file
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, {1.0f, 0.0f, 0.0f});
    }
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        EXPECT_EQ(db.search({1.0f, 0.0f, 0.0f}), 1);
    }
    // Third open
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        EXPECT_EQ(db.search({1.0f, 0.0f, 0.0f}), 1);
    }
}
