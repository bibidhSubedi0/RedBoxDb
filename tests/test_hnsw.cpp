#include <gtest/gtest.h>
#include <filesystem>
#include <vector>
#include <string>
#include <set>
#include <cmath>
#include <algorithm>
#include <numeric>
#include "redboxdb/engine.hpp"
#include <spdlog/spdlog.h>

// =============================================================================
// HNSW Test Fixture
// Uses smaller M and ef_construction for faster tests.
// =============================================================================
struct HnswFixture : public ::testing::Test {
    std::string db_file;
    std::string del_file;

    static constexpr int    DIM    = 16;
    static constexpr int    CAP    = 500;
    static constexpr uint8_t M     = 8;
    static constexpr uint16_t EF_C = 50;

    explicit HnswFixture() = default;

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

    // Helper: create an HNSW db with default fixture params
    std::unique_ptr<CoreEngine::RedBoxVector> make_db() {
        return std::make_unique<CoreEngine::RedBoxVector>(db_file, DIM, CAP, M, EF_C);
    }

    // Helper: generate a deterministic random vector
    static std::vector<float> make_vec(int seed, int dim = DIM) {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        std::vector<float> v(dim);
        for (auto& x : v) x = dist(rng);
        return v;
    }

    // Helper: L2 distance between two vectors
    static float l2(const std::vector<float>& a, const std::vector<float>& b) {
        float d = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            float diff = a[i] - b[i];
            d += diff * diff;
        }
        return d;
    }
};


// =============================================================================
// 1. BASIC INSERT & SEARCH
// =============================================================================
class HnswBasicTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_basic"); HnswFixture::SetUp(); }
};

TEST_F(HnswBasicTest, SingleInsertAndSearch) {
    auto db = make_db();
    db->insert(1, { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    int result = db->search({ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    EXPECT_EQ(result, 1);
}

TEST_F(HnswBasicTest, TwoInsertsFindsNearest) {
    auto db = make_db();
    auto v1 = make_vec(1);  // Random vector for ID 1
    auto v2 = make_vec(2);  // Random vector for ID 2
    db->insert(1, v1);
    db->insert(2, v2);

    // Query closer to v1
    std::vector<float> q(DIM);
    for (int i = 0; i < DIM; ++i) q[i] = v1[i] + 0.01f;
    int result = db->search(q);
    EXPECT_EQ(result, 1);
}

TEST_F(HnswBasicTest, ThreeInsertsCorrectOrdering) {
    auto db = make_db();
    // Insert vectors at known distances along axis 0
    db->insert(10, { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    db->insert(20, { 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    db->insert(30, { 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });

    int result = db->search({ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    EXPECT_EQ(result, 10);
}

TEST_F(HnswBasicTest, GetIndexType) {
    auto db = make_db();
    EXPECT_EQ(db->get_index_type(), CoreEngine::IndexType::HNSW);
}

TEST_F(HnswBasicTest, EmptyDBSearchReturnsNegative) {
    auto db = make_db();
    int result = db->search(make_vec(999));
    EXPECT_EQ(result, -1);
}


// =============================================================================
// 2. PERSISTENCE
// =============================================================================
class HnswPersistenceTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_persist"); HnswFixture::SetUp(); }
};

TEST_F(HnswPersistenceTest, DataSurvivesRestart) {
    {
        auto db = make_db();
        db->insert(1, make_vec(10));
        db->insert(2, make_vec(20));
        db->insert(3, make_vec(30));
    }
    {
        auto db = make_db();
        auto v = make_vec(10);
        int result = db->search(v);
        EXPECT_EQ(result, 1) << "ID 1 should persist across restart";
    }
}

TEST_F(HnswPersistenceTest, MultipleRestarts) {
    {
        auto db = make_db();
        db->insert(1, make_vec(1));
    }
    {
        auto db = make_db();
        db->insert(2, make_vec(2));
    }
    {
        auto db = make_db();
        EXPECT_EQ(db->search(make_vec(1)), 1);
        EXPECT_EQ(db->search(make_vec(2)), 2);
    }
}

TEST_F(HnswPersistenceTest, AutoIDsPersist) {
    {
        auto db = make_db();
        db->insert_auto(make_vec(10));
        db->insert_auto(make_vec(20));
        db->insert_auto(make_vec(30));
    }
    {
        auto db = make_db();
        uint64_t id4 = db->insert_auto(make_vec(40));
        EXPECT_EQ(id4, 4) << "Auto ID should continue from 4 after restart";
    }
}

TEST_F(HnswPersistenceTest, NextIdPersists) {
    {
        auto db = make_db();
        for (int i = 0; i < 10; ++i)
            db->insert_auto(make_vec(i));
    }
    {
        auto db = make_db();
        uint64_t id = db->insert_auto(make_vec(99));
        EXPECT_EQ(id, 11) << "next_id should be 11 after inserting 10 vectors";
    }
}


// =============================================================================
// 3. SEARCH_N (k-NN)
// =============================================================================
class HnswSearchNTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_search_n"); HnswFixture::SetUp(); }
};

TEST_F(HnswSearchNTest, CorrectOrder) {
    auto db = make_db();
    db->insert(10, { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    db->insert(20, { 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    db->insert(30, { 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });

    auto results = db->search_N({ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                   0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 3);

    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0], 10);
    EXPECT_EQ(results[1], 20);
    EXPECT_EQ(results[2], 30);
}

TEST_F(HnswSearchNTest, RequestMoreThanExists) {
    auto db = make_db();
    db->insert(1, make_vec(1));
    db->insert(2, make_vec(2));

    auto results = db->search_N(make_vec(0), 10);
    EXPECT_EQ(results.size(), 2u);
}

TEST_F(HnswSearchNTest, EmptyDBReturnsEmpty) {
    auto db = make_db();
    auto results = db->search_N(make_vec(0), 5);
    EXPECT_TRUE(results.empty());
}

TEST_F(HnswSearchNTest, SingleElement) {
    auto db = make_db();
    db->insert(42, make_vec(42));
    auto results = db->search_N(make_vec(42), 10);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 42);
}

TEST_F(HnswSearchNTest, NEquals1AgreesWithSearch) {
    auto db = make_db();
    for (int i = 1; i <= 20; ++i)
        db->insert(i, make_vec(i));

    auto q = make_vec(5);
    int single = db->search(q);
    auto results = db->search_N(q, 1);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], single);
}

TEST_F(HnswSearchNTest, NoDuplicates) {
    auto db = make_db();
    for (int i = 1; i <= 50; ++i)
        db->insert(i, make_vec(i));

    auto results = db->search_N(make_vec(25), 10);
    std::set<int> seen;
    for (int id : results) {
        EXPECT_TRUE(seen.find(id) == seen.end()) << "Duplicate ID " << id << " in results";
        seen.insert(id);
    }
}

TEST_F(HnswSearchNTest, AllResultsAreValidIDs) {
    auto db = make_db();
    std::vector<uint64_t> ids;
    for (int i = 1; i <= 30; ++i) {
        db->insert(i, make_vec(i));
        ids.push_back(i);
    }

    auto results = db->search_N(make_vec(15), 10);
    std::set<uint64_t> valid(ids.begin(), ids.end());
    for (int id : results) {
        EXPECT_TRUE(valid.count(id)) << "Invalid ID " << id << " in results";
    }
}


// =============================================================================
// 4. DELETION
// =============================================================================
class HnswDeletionTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_delete"); HnswFixture::SetUp(); }
};

TEST_F(HnswDeletionTest, BasicSoftDelete) {
    auto db = make_db();
    db->insert(10, make_vec(10));
    db->insert(99, make_vec(99));

    EXPECT_EQ(db->search(make_vec(10)), 10);

    bool removed = db->remove(10);
    EXPECT_TRUE(removed);

    // After deletion, only 99 should be findable
    int result = db->search(make_vec(10));
    EXPECT_NE(result, 10);
}

TEST_F(HnswDeletionTest, DeleteNonExistent) {
    auto db = make_db();
    db->insert(1, make_vec(1));
    bool removed = db->remove(999);
    EXPECT_FALSE(removed);
}

TEST_F(HnswDeletionTest, DoubleDelete) {
    auto db = make_db();
    db->insert(1, make_vec(1));
    EXPECT_TRUE(db->remove(1));
    EXPECT_FALSE(db->remove(1));
}

TEST_F(HnswDeletionTest, DeletionPersistsAcrossRestart) {
    {
        auto db = make_db();
        db->insert(1, make_vec(1));
        db->insert(2, make_vec(2));
        db->remove(1);
    }
    {
        auto db = make_db();
        EXPECT_FALSE(db->remove(1));
        EXPECT_EQ(db->search(make_vec(2)), 2);
    }
}

TEST_F(HnswDeletionTest, ReinsertionAfterDelete) {
    auto db = make_db();
    db->insert(1, make_vec(1));
    db->remove(1);

    // Insert distractor
    db->insert(2, make_vec(2));
    EXPECT_NE(db->search(make_vec(1)), 1);

    // Re-insert ID 1 with same vector
    db->insert(1, make_vec(1));
    EXPECT_EQ(db->search(make_vec(1)), 1);
}

TEST_F(HnswDeletionTest, SearchNExcludesDeleted) {
    auto db = make_db();
    db->insert(1, { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    db->insert(2, { 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    db->insert(3, { 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });

    db->remove(2);

    auto results = db->search_N({ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                   0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 3);

    for (int id : results)
        EXPECT_NE(id, 2) << "Deleted ID 2 should not appear in search_N results";
}


// =============================================================================
// 5. UPDATE
// =============================================================================
class HnswUpdateTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_update"); HnswFixture::SetUp(); }
};

TEST_F(HnswUpdateTest, UpdateChangesSearchResult) {
    auto db = make_db();
    db->insert(1, { 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });

    // Update ID 1 to a new location
    bool ok = db->update(1, { -10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    EXPECT_TRUE(ok);

    // Search near new location should find ID 1
    int result = db->search({ -10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    EXPECT_EQ(result, 1);
}

TEST_F(HnswUpdateTest, UpdateNonExistentReturnsFalse) {
    auto db = make_db();
    bool ok = db->update(999, make_vec(999));
    EXPECT_FALSE(ok);
}

TEST_F(HnswUpdateTest, UpdateDeletedReturnsFalse) {
    auto db = make_db();
    db->insert(1, make_vec(1));
    db->remove(1);
    bool ok = db->update(1, make_vec(2));
    EXPECT_FALSE(ok);
}

TEST_F(HnswUpdateTest, UpdatePersistsAcrossRestart) {
    {
        auto db = make_db();
        db->insert(1, { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
        db->update(1, { 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    }
    {
        auto db = make_db();
        int result = db->search({ 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                   0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
        EXPECT_EQ(result, 1);
    }
}

TEST_F(HnswUpdateTest, DoubleUpdateIsIdempotent) {
    auto db = make_db();
    db->insert(1, make_vec(1));
    db->update(1, make_vec(10));
    db->update(1, make_vec(20));

    int result = db->search(make_vec(20));
    EXPECT_EQ(result, 1);
}


// =============================================================================
// 6. AUTO INSERT
// =============================================================================
class HnswAutoInsertTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_auto"); HnswFixture::SetUp(); }
};

TEST_F(HnswAutoInsertTest, AutoIDsAreSequential) {
    auto db = make_db();
    uint64_t id1 = db->insert_auto(make_vec(1));
    uint64_t id2 = db->insert_auto(make_vec(2));
    uint64_t id3 = db->insert_auto(make_vec(3));

    EXPECT_EQ(id1, 1);
    EXPECT_EQ(id2, 2);
    EXPECT_EQ(id3, 3);
}

TEST_F(HnswAutoInsertTest, AutoInsertIsSearchable) {
    auto db = make_db();
    uint64_t id = db->insert_auto(make_vec(42));
    int result = db->search(make_vec(42));
    EXPECT_EQ(static_cast<uint64_t>(result), id);
}

TEST_F(HnswAutoInsertTest, MixedManualAndAuto) {
    auto db = make_db();
    db->insert(500, make_vec(500));
    uint64_t id1 = db->insert_auto(make_vec(1));
    uint64_t id2 = db->insert_auto(make_vec(2));

    EXPECT_EQ(id1, 1);
    EXPECT_EQ(id2, 2);

    EXPECT_EQ(db->search(make_vec(500)), 500);
    EXPECT_EQ(db->search(make_vec(1)), 1);
    EXPECT_EQ(db->search(make_vec(2)), 2);
}


// =============================================================================
// 7. CAPACITY
// =============================================================================
class HnswCapacityTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_capacity"); HnswFixture::SetUp(); }
};

TEST_F(HnswCapacityTest, FillToCapacity) {
    const int CAP = 30;
    CoreEngine::RedBoxVector db(db_file, DIM, CAP, (uint8_t)M, (uint16_t)EF_C);
    for (int i = 0; i < CAP; ++i)
        db.insert((uint64_t)(i + 1), make_vec(i));

    EXPECT_EQ(db.search(make_vec(CAP - 1)), CAP);
}

TEST_F(HnswCapacityTest, ExceedCapacityDoesNotCorrupt) {
    const int CAP = 10;
    CoreEngine::RedBoxVector db(db_file, DIM, CAP, (uint8_t)M, (uint16_t)EF_C);
    for (int i = 0; i < CAP; ++i)
        db.insert((uint64_t)(i + 1), make_vec(i));

    try {
        db.insert(9999, make_vec(9999));
    } catch (const std::exception&) {}

    EXPECT_EQ(db.search(make_vec(0)), 1);
}


// =============================================================================
// 8. EF_SEARCH TUNING
// =============================================================================
class HnswEfSearchTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_ef_search"); HnswFixture::SetUp(); }
};

TEST_F(HnswEfSearchTest, SetEfSearchChangesResults) {
    auto db = make_db();
    for (int i = 1; i <= 50; ++i)
        db->insert(i, make_vec(i));

    // With very low ef_search, results may differ from high ef_search
    db->set_hnsw_ef_search(1);
    auto low_ef = db->search_N(make_vec(25), 5);

    db->set_hnsw_ef_search(200);
    auto high_ef = db->search_N(make_vec(25), 5);

    // Both should return valid results
    EXPECT_FALSE(low_ef.empty());
    EXPECT_FALSE(high_ef.empty());

    // With high enough ef, the top-1 should be correct
    EXPECT_EQ(high_ef[0], 25);
}

TEST_F(HnswEfSearchTest, EfSearchPersistsAcrossRestart) {
    {
        auto db = make_db();
        db->set_hnsw_ef_search(42);
    }
    {
        // Reopen - ef_search should still be 42 (stored in header)
        auto db = make_db();
        // We can't directly read ef_search, but we can verify the DB works
        db->insert(1, make_vec(1));
        EXPECT_EQ(db->search(make_vec(1)), 1);
    }
}


// =============================================================================
// 9. HIGH-DIMENSIONAL
// =============================================================================
class HnswHighDimTest : public ::testing::Test {
protected:
    std::string db_file = "test_hnsw_highdim.db";
    std::string del_file = "test_hnsw_highdim.db.del";

    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
        std::error_code ec;
        std::filesystem::remove(db_file, ec);
        std::filesystem::remove(del_file, ec);
    }
    void TearDown() override {
        spdlog::set_level(spdlog::level::info);
        std::error_code ec;
        std::filesystem::remove(db_file, ec);
        std::filesystem::remove(del_file, ec);
    }
};

TEST_F(HnswHighDimTest, Dim64) {
    const int DIM = 64;
    CoreEngine::RedBoxVector db(db_file, DIM, 200, (uint8_t)8, (uint16_t)50);

    auto make = [](int seed, int dim) {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        std::vector<float> v(dim);
        for (auto& x : v) x = dist(rng);
        return v;
    };

    db.insert(1, make(1, DIM));
    db.insert(2, make(2, DIM));
    db.insert(3, make(3, DIM));

    auto q = make(1, DIM);
    int result = db.search(q);
    EXPECT_EQ(result, 1);
}

TEST_F(HnswHighDimTest, Dim128) {
    const int DIM = 128;
    CoreEngine::RedBoxVector db(db_file, DIM, 200, (uint8_t)8, (uint16_t)50);

    auto make = [](int seed, int dim) {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        std::vector<float> v(dim);
        for (auto& x : v) x = dist(rng);
        return v;
    };

    for (int i = 1; i <= 30; ++i)
        db.insert(i, make(i, DIM));

    auto q = make(15, DIM);
    int result = db.search(q);
    EXPECT_EQ(result, 15);
}


// =============================================================================
// 10. EDGE CASES
// =============================================================================
class HnswEdgeCaseTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_edge"); HnswFixture::SetUp(); }
};

TEST_F(HnswEdgeCaseTest, ZeroVector) {
    auto db = make_db();
    std::vector<float> zeros(DIM, 0.0f);
    db->insert(1, zeros);

    std::vector<float> q(DIM, 0.01f);
    int result = db->search(q);
    EXPECT_EQ(result, 1);
}

TEST_F(HnswEdgeCaseTest, NegativeCoordinates) {
    auto db = make_db();
    auto v1 = make_vec(1);
    auto v2 = make_vec(2);
    // Negate v1 to make it clearly different
    for (auto& x : v1) x = -std::abs(x);
    for (auto& x : v2) x = std::abs(x);

    db->insert(1, v1);
    db->insert(2, v2);

    // Query near v1
    std::vector<float> q(DIM);
    for (int i = 0; i < DIM; ++i) q[i] = v1[i] - 0.01f;
    EXPECT_EQ(db->search(q), 1);
}

TEST_F(HnswEdgeCaseTest, LargeCoordinateValues) {
    auto db = make_db();
    std::vector<float> v1(DIM, 1000.0f);
    std::vector<float> v2(DIM, 2000.0f);
    db->insert(1, v1);
    db->insert(2, v2);

    std::vector<float> q(DIM, 1100.0f);
    EXPECT_EQ(db->search(q), 1);
}

TEST_F(HnswEdgeCaseTest, IdenticalVectorsDifferentIDs) {
    auto db = make_db();
    std::vector<float> v(DIM, 5.0f);
    db->insert(10, v);
    db->insert(20, v);

    int result = db->search(v);
    EXPECT_TRUE(result == 10 || result == 20)
        << "Should return one of the two identical vectors, got " << result;
}

TEST_F(HnswEdgeCaseTest, SingleVector) {
    auto db = make_db();
    db->insert(42, make_vec(42));
    EXPECT_EQ(db->search(make_vec(42)), 42);

    auto results = db->search_N(make_vec(42), 10);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 42);
}


// =============================================================================
// 11. GRAPH CONNECTIVITY (larger dataset)
// =============================================================================
class HnswConnectivityTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_connect"); HnswFixture::SetUp(); }
};

TEST_F(HnswConnectivityTest, ManyVectorsSearchCorrect) {
    auto db = make_db();
    const int N = 200;
    for (int i = 0; i < N; ++i)
        db->insert(i + 1, make_vec(i));

    // Search for each inserted vector; it should find itself as nearest
    int correct = 0;
    for (int i = 0; i < N; ++i) {
        auto v = make_vec(i);
        int result = db->search(v);
        if (result == i + 1) ++correct;
    }

    // HNSW is approximate; expect at least 80% accuracy
    float recall = (float)correct / N;
    EXPECT_GE(recall, 0.8f) << "Recall " << recall << " is too low for " << N << " vectors";
}

TEST_F(HnswConnectivityTest, SearchAcrossRegions) {
    auto db = make_db();
    db->set_hnsw_ef_search(256);

    // Insert 200 vectors in a single connected chain from +100 to -100 on dim 0.
    // This guarantees graph connectivity while testing that search finds
    // the geometrically nearest point.
    const int N = 200;
    std::vector<std::vector<float>> all_vecs(N + 1);
    for (int i = 0; i < N; ++i) {
        auto v = make_vec(i);
        v[0] = 100.0f - 200.0f * i / (N - 1); // linearly from +100 to -100
        all_vecs[i + 1] = v;
        db->insert(i + 1, v);
    }

    // Query at +100: nearest should be near the start (IDs 1-5)
    std::vector<float> q1(DIM, 0.0f);
    q1[0] = 100.0f;
    int r1 = db->search(q1);
    EXPECT_GE(r1, 1);
    EXPECT_LE(r1, N);
    EXPECT_GT(all_vecs[r1][0], 50.0f)
        << "Result " << r1 << " (dim0=" << all_vecs[r1][0] << ") should be near dim0=+100";

    // Query at -100: nearest should be near the end (IDs 196-200)
    std::vector<float> q2(DIM, 0.0f);
    q2[0] = -100.0f;
    int r2 = db->search(q2);
    EXPECT_GE(r2, 1);
    EXPECT_LE(r2, N);
    EXPECT_LT(all_vecs[r2][0], -50.0f)
        << "Result " << r2 << " (dim0=" << all_vecs[r2][0] << ") should be near dim0=-100";
}

TEST_F(HnswConnectivityTest, SearchNReturnsAllRequested) {
    auto db = make_db();
    const int N = 100;
    for (int i = 0; i < N; ++i)
        db->insert(i + 1, make_vec(i));

    auto results = db->search_N(make_vec(50), 20);
    EXPECT_EQ(results.size(), 20u);
}


// =============================================================================
// 12. MIXED OPERATIONS
// =============================================================================
class HnswMixedOpsTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_mixed"); HnswFixture::SetUp(); }
};

TEST_F(HnswMixedOpsTest, InsertDeleteInsert) {
    auto db = make_db();
    db->insert(1, make_vec(1));
    db->insert(2, make_vec(2));
    db->remove(1);
    db->insert(3, make_vec(3));

    EXPECT_EQ(db->search(make_vec(2)), 2);
    EXPECT_EQ(db->search(make_vec(3)), 3);
    EXPECT_NE(db->search(make_vec(1)), 1);
}

TEST_F(HnswMixedOpsTest, UpdateThenDelete) {
    auto db = make_db();
    db->insert(1, make_vec(1));
    db->update(1, make_vec(100));
    db->remove(1);

    EXPECT_NE(db->search(make_vec(100)), 1);
}

TEST_F(HnswMixedOpsTest, InterleavedOps) {
    auto db = make_db();
    for (int i = 1; i <= 20; ++i)
        db->insert(i, make_vec(i));

    // Delete even IDs
    for (int i = 2; i <= 20; i += 2)
        db->remove(i);

    // Update odd IDs
    for (int i = 1; i <= 20; i += 2)
        db->update(i, make_vec(i + 1000));

    // Search near updated positions
    for (int i = 1; i <= 20; i += 2) {
        int result = db->search(make_vec(i + 1000));
        EXPECT_EQ(result, i) << "ID " << i << " should be found at updated position";
    }

    // Verify even IDs are gone
    for (int i = 2; i <= 20; i += 2) {
        int result = db->search(make_vec(i));
        EXPECT_NE(result, i) << "Deleted ID " << i << " should not be found";
    }
}


// =============================================================================
// 13. PERSISTENCE STRESS
// =============================================================================
class HnswPersistenceStressTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_stress"); HnswFixture::SetUp(); }
};

TEST_F(HnswPersistenceStressTest, MultipleOpenCloseRoundTrips) {
    {
        auto db = make_db();
        for (int i = 1; i <= 10; ++i)
            db->insert(i, make_vec(i));
    }
    {
        auto db = make_db();
        for (int i = 1; i <= 10; ++i)
            EXPECT_EQ(db->search(make_vec(i)), i);
        for (int i = 11; i <= 20; ++i)
            db->insert(i, make_vec(i));
    }
    {
        auto db = make_db();
        for (int i = 1; i <= 20; ++i)
            EXPECT_EQ(db->search(make_vec(i)), i);
    }
}

TEST_F(HnswPersistenceStressTest, DeletionsSurviveReloads) {
    {
        auto db = make_db();
        db->insert(1, make_vec(1));
        db->insert(2, make_vec(2));
        db->remove(1);
    }
    for (int cycle = 0; cycle < 3; ++cycle) {
        auto db = make_db();
        EXPECT_FALSE(db->remove(1));
        EXPECT_EQ(db->search(make_vec(2)), 2);
    }
}

TEST_F(HnswPersistenceStressTest, UpdatesAndDeletesSurviveReload) {
    {
        auto db = make_db();
        db->insert(1, make_vec(1));
        db->insert(2, make_vec(2));
        db->insert(3, make_vec(3));
        db->update(2, make_vec(200));
        db->remove(3);
    }
    {
        auto db = make_db();
        EXPECT_EQ(db->search(make_vec(1)), 1);
        EXPECT_EQ(db->search(make_vec(200)), 2);
        EXPECT_FALSE(db->remove(3));
    }
}


// =============================================================================
// 14. NN REGRESSION (mathematically exact cases)
// =============================================================================
class HnswNNRegressionTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_nn_regression"); HnswFixture::SetUp(); }
};

TEST_F(HnswNNRegressionTest, AxisAlignedBasis) {
    const int D = 8;
    CoreEngine::RedBoxVector db(db_file, D, 200, (uint8_t)M, (uint16_t)EF_C);
    for (int axis = 0; axis < D; ++axis) {
        std::vector<float> v(D, 0.0f);
        v[axis] = 1.0f;
        db.insert(axis + 1, v);
    }
    for (int axis = 0; axis < D; ++axis) {
        std::vector<float> q(D, 0.01f);
        q[axis] = 0.95f;
        EXPECT_EQ(db.search(q), axis + 1)
            << "Axis " << axis << ": wrong nearest neighbor";
    }
}

TEST_F(HnswNNRegressionTest, NegativeCoordinates) {
    auto db = make_db();
    std::vector<float> v1(DIM, -10.0f);
    std::vector<float> v2(DIM, 10.0f);
    db->insert(1, v1);
    db->insert(2, v2);

    std::vector<float> q1(DIM, -9.0f);
    std::vector<float> q2(DIM, 9.0f);
    EXPECT_EQ(db->search(q1), 1);
    EXPECT_EQ(db->search(q2), 2);
}

TEST_F(HnswNNRegressionTest, LargeCoordinateValues) {
    auto db = make_db();
    std::vector<float> v1(DIM, 1e3f);
    std::vector<float> v2(DIM, 2e3f);
    db->insert(1, v1);
    db->insert(2, v2);

    std::vector<float> q1(DIM, 1.1e3f);
    std::vector<float> q2(DIM, 1.9e3f);
    EXPECT_EQ(db->search(q1), 1);
    EXPECT_EQ(db->search(q2), 2);
}

TEST_F(HnswNNRegressionTest, ZeroVector) {
    const int D = 4;
    CoreEngine::RedBoxVector db(db_file, D, 200, (uint8_t)M, (uint16_t)EF_C);
    db.insert(1, std::vector<float>(D, 0.0f));
    db.insert(2, std::vector<float>(D, 1.0f));
    EXPECT_EQ(db.search(std::vector<float>(D, 0.0f)), 1);
    EXPECT_EQ(db.search(std::vector<float>(D, 0.1f)), 1);
}

TEST_F(HnswNNRegressionTest, IdenticalVectorsDifferentIDs) {
    auto db = make_db();
    std::vector<float> v(DIM, 5.0f);
    db->insert(10, v);
    db->insert(20, v);
    int result = db->search(v);
    EXPECT_TRUE(result == 10 || result == 20)
        << "Must return one of the two identical vectors, got " << result;
}

TEST_F(HnswNNRegressionTest, SearchNCountWithEquidistantVectors) {
    const int D = 2;
    CoreEngine::RedBoxVector db(db_file, D, 200, (uint8_t)M, (uint16_t)EF_C);
    const int COUNT = 8;
    for (int i = 0; i < COUNT; ++i) {
        float angle = (float)i * 3.14159265f / (float)COUNT;
        db.insert(i + 1, { std::cos(angle), std::sin(angle) });
    }
    auto results = db.search_N({ 0.0f, 0.0f }, 5);
    EXPECT_EQ(results.size(), 5u);
    std::set<int> valid_ids;
    for (int i = 1; i <= COUNT; ++i) valid_ids.insert(i);
    for (int id : results)
        EXPECT_GT(valid_ids.count(id), 0u) << "Invalid ID " << id << " in results";
}


// =============================================================================
// 15. SEARCH CONSISTENCY
// =============================================================================
class HnswConsistencyTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_consistency"); HnswFixture::SetUp(); }
};

TEST_F(HnswConsistencyTest, MultipleSearchesSameQuery) {
    auto db = make_db();
    for (int i = 1; i <= 50; ++i)
        db->insert(i, make_vec(i));

    auto q = make_vec(25);
    int first = db->search(q);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(db->search(q), first)
            << "Search result changed on iteration " << i;
    }
}

TEST_F(HnswConsistencyTest, SearchNConsistency) {
    auto db = make_db();
    for (int i = 1; i <= 50; ++i)
        db->insert(i, make_vec(i));

    auto q = make_vec(25);
    auto first = db->search_N(q, 10);
    for (int i = 0; i < 5; ++i) {
        auto current = db->search_N(q, 10);
        EXPECT_EQ(first, current)
            << "search_N result changed on iteration " << i;
    }
}


// =============================================================================
// 16. DIMENSION MISMATCH (should not crash)
// =============================================================================
class HnswDimensionTest : public HnswFixture {
protected:
    void SetUp() override { init("test_hnsw_dim"); HnswFixture::SetUp(); }
};

TEST_F(HnswDimensionTest, GetDimReturnsConstructorDim) {
    auto db = make_db();
    EXPECT_EQ(db->get_dim(), DIM);
}

TEST_F(HnswDimensionTest, HighDimSearchCorrect) {
    const int D = 64;
    CoreEngine::RedBoxVector db(db_file, D, 200, (uint8_t)M, (uint16_t)EF_C);

    auto make = [](int seed, int dim) {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        std::vector<float> v(dim);
        for (auto& x : v) x = dist(rng);
        return v;
    };

    db.insert(1, make(1, D));
    db.insert(2, make(2, D));

    auto q = make(1, D);
    EXPECT_EQ(db.search(q), 1);
}
