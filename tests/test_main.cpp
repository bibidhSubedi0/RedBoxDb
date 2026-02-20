#include <gtest/gtest.h>
#include <filesystem>
#include <vector>
#include <string>
#include "redboxdb/engine.hpp" // Ensure this path is correct

// Test Fixture to handle file cleanup automatically
class BasicPersistanceAndZC : public ::testing::Test {
protected:
    std::string test_db_file = "test_redbox.db";
    int dim = 3;
    int capacity = 1000;

    void SetUp() override {
        // Remove file before each test to ensure a clean state
        if (std::filesystem::exists(test_db_file)) {
            std::filesystem::remove(test_db_file);
        }
    }

    void TearDown() override {
        // Optional: Remove file after test
        // if (std::filesystem::exists(test_db_file)) {
        //    std::filesystem::remove(test_db_file);
        // }
    }
};

TEST_F(BasicPersistanceAndZC, InsertAndSearchInMemory) {
    CoreEngine::RedBoxVector db(test_db_file, dim, capacity);

    // Target: [1.0, 0.0, 0.0] -> ID 1
    db.insert(1, { 1.0f, 0.0f, 0.0f });

    // Distractor: [0.0, 1.0, 0.0] -> ID 2
    db.insert(2, { 0.0f, 1.0f, 0.0f });

    // Search for something close to Target
    std::vector<float> query = { 0.9f, 0.1f, 0.0f };
    int result = db.search(query);

    EXPECT_EQ(result, 1) << "Should find ID 1 as the closest vector";
}

TEST_F(BasicPersistanceAndZC, PersistenceCheck) {
    // Scope 1: Write Data
    {
        CoreEngine::RedBoxVector db(test_db_file, dim, capacity);
        db.insert(50, { 0.5f, 0.5f, 0.5f });
    } // Destructor closes file/map

    // Scope 2: Read Data (Simulate app restart)
    {
        CoreEngine::RedBoxVector db(test_db_file, dim, capacity);

        // Exact match search
        int result = db.search({ 0.5f, 0.5f, 0.5f });
        EXPECT_EQ(result, 50) << "Data should persist across object destruction";
    }
}

TEST_F(BasicPersistanceAndZC, LargeDatasetHandling) {
    CoreEngine::RedBoxVector db(test_db_file, dim, 5000);

    // Insert 100 vectors
    for (int i = 0; i < 100; ++i) {
        // Vector is [i, 0, 0]
        db.insert(i, { static_cast<float>(i), 0.0f, 0.0f });
    }

    // Search for vector [42.1, 0, 0] -> Should be closest to ID 42
    int result = db.search({ 42.1f, 0.0f, 0.0f });
    EXPECT_EQ(result, 42);
}

TEST_F(BasicPersistanceAndZC, ZeroCopyCorrectness) {
    // This verifies that your new "Raw Pointer" search didn't break anything
    CoreEngine::RedBoxVector db(test_db_file, dim, capacity);

    db.insert(100, { 10.0f, 10.0f, 10.0f }); // Far away
    db.insert(200, { 1.0f, 1.0f, 1.0f });   // Target

    int result = db.search({ 1.1f, 1.1f, 1.1f });
    EXPECT_EQ(result, 200);
}


class DeletionTest : public ::testing::Test {
protected:
    std::string db_file = "test_delete.db";
    std::string del_file = "test_delete.db.del";

    void SetUp() override {
        if (std::filesystem::exists(db_file)) std::filesystem::remove(db_file);
        if (std::filesystem::exists(del_file)) std::filesystem::remove(del_file);
    }

    void TearDown() override {
        // Optional cleanup
    }
};

TEST_F(DeletionTest, BasicSoftDelete) {
    CoreEngine::RedBoxVector db(db_file, 3, 1000);

    // Insert Target (ID 10) and Distractor (ID 99)
    db.insert(10, { 1.0f, 1.0f, 1.0f });
    db.insert(99, { 50.0f, 50.0f, 50.0f });

    // 1. Confirm ID 10 is found
    EXPECT_EQ(db.search({ 1.1f, 1.1f, 1.1f }), 10);

    // 2. Delete ID 10
    bool removed = db.remove(10);
    EXPECT_TRUE(removed);

    // 3. Confirm ID 10 is GONE (Should find ID 99 now as it's the only one left)
    EXPECT_EQ(db.search({ 1.1f, 1.1f, 1.1f }), 99);
}

TEST_F(DeletionTest, PersistenceOfDeletion) {
    // Phase 1: Create and Delete
    {
        CoreEngine::RedBoxVector db(db_file, 3, 1000);
        db.insert(5, { 0.0f, 0.0f, 0.0f });
        db.remove(5);
    }

    // Phase 2: Reload and Verify
    {
        CoreEngine::RedBoxVector db(db_file, 3, 1000);
        // We insert a far away vector just so the DB isn't empty
        db.insert(999, { 100.0f, 100.0f, 100.0f });

        // Search for 0,0,0. 
        // If ID 5 was still there, it would be a perfect match.
        // Since it's deleted, we should match 999 (distractor).
        int result = db.search({ 0.0f, 0.0f, 0.0f });
        EXPECT_EQ(result, 999);
    }
}

TEST_F(DeletionTest, ReinsertionUndo) {
    CoreEngine::RedBoxVector db(db_file, 3, 1000);

    db.insert(1, { 0.0f, 0.0f, 0.0f });
    db.remove(1);

    // Verify gone
    db.insert(2, { 10.0f, 10.0f, 10.0f });
    EXPECT_EQ(db.search({ 0.0f, 0.0f, 0.0f }), 2);

    // Re-insert ID 1
    db.insert(1, { 0.0f, 0.0f, 0.0f });

    // Verify back
    EXPECT_EQ(db.search({ 0.0f, 0.0f, 0.0f }), 1);
}



#include <filesystem>
#include <algorithm>

class SearchNTest : public ::testing::Test {
protected:
    std::string db_file = "test_search_n.db";
    std::string del_file = "test_search_n.db.del";

    void SetUp() override {
        if (std::filesystem::exists(db_file)) std::filesystem::remove(db_file);
        if (std::filesystem::exists(del_file)) std::filesystem::remove(del_file);
    }
};

TEST_F(SearchNTest, CorrectOrder) {
    CoreEngine::RedBoxVector db(db_file, 3, 1000);

    // Insert points at increasing distances from Origin
    db.insert(10, { 1.0f, 0.0f, 0.0f }); // Closest
    db.insert(20, { 2.0f, 0.0f, 0.0f });
    db.insert(30, { 3.0f, 0.0f, 0.0f }); // Furthest

    auto results = db.search_N({ 0.0f, 0.0f, 0.0f }, 3);

    ASSERT_EQ(results.size(), 3);
    EXPECT_EQ(results[0], 10); // 1st
    EXPECT_EQ(results[1], 20); // 2nd
    EXPECT_EQ(results[2], 30); // 3rd
}

TEST_F(SearchNTest, RequestMoreThanExists) {
    CoreEngine::RedBoxVector db(db_file, 3, 1000);

    db.insert(1, { 1.0f, 1.0f, 1.0f });
    db.insert(2, { 2.0f, 2.0f, 2.0f });

    // Ask for 5 items, but DB only has 2
    auto results = db.search_N({ 0.0f, 0.0f, 0.0f }, 5);

    EXPECT_EQ(results.size(), 2);
    // Should still be sorted
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
}

TEST_F(SearchNTest, IgnoreDeletedItems) {
    CoreEngine::RedBoxVector db(db_file, 3, 1000);

    db.insert(1, { 1.0f, 0.0f, 0.0f }); // Dist 1
    db.insert(2, { 2.0f, 0.0f, 0.0f }); // Dist 4
    db.insert(3, { 3.0f, 0.0f, 0.0f }); // Dist 9

    // Delete the middle one
    db.remove(2);

    // Ask for Top 2
    auto results = db.search_N({ 0.0f, 0.0f, 0.0f }, 2);

    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 3); // Should skip 2 and grab 3
}

TEST_F(SearchNTest, EmptyDatabase) {
    CoreEngine::RedBoxVector db(db_file, 3, 1000);
    auto results = db.search_N({ 0.0f, 0.0f, 0.0f }, 5);
    EXPECT_TRUE(results.empty());
}

class AutoInsertAndIndexTest : public ::testing::Test {
protected:
    std::string db_file = "test_auto.db";
    std::string del_file = "test_auto.db.del";

    void SetUp() override {
        if (std::filesystem::exists(db_file)) std::filesystem::remove(db_file);
        if (std::filesystem::exists(del_file)) std::filesystem::remove(del_file);
    }
};

TEST_F(AutoInsertAndIndexTest, AutoIDsAreSequential) {
    CoreEngine::RedBoxVector db(db_file, 3, 1000);

    uint64_t id1 = db.insert_auto({ 1.0f, 0.0f, 0.0f });
    uint64_t id2 = db.insert_auto({ 0.0f, 1.0f, 0.0f });
    uint64_t id3 = db.insert_auto({ 0.0f, 0.0f, 1.0f });

    EXPECT_EQ(id1, 1);
    EXPECT_EQ(id2, 2);
    EXPECT_EQ(id3, 3);
}

TEST_F(AutoInsertAndIndexTest, AutoIDsPersistAcrossRestart) {
    // Phase 1: insert 3 vectors
    {
        CoreEngine::RedBoxVector db(db_file, 3, 1000);
        db.insert_auto({ 1.0f, 0.0f, 0.0f });
        db.insert_auto({ 0.0f, 1.0f, 0.0f });
        db.insert_auto({ 0.0f, 0.0f, 1.0f });
    }

    // Phase 2: reopen — next ID should continue from 4, not reset to 1
    {
        CoreEngine::RedBoxVector db(db_file, 3, 1000);
        uint64_t id4 = db.insert_auto({ 1.0f, 1.0f, 0.0f });
        EXPECT_EQ(id4, 4) << "next_id should survive restart";
    }
}

TEST_F(AutoInsertAndIndexTest, AutoInsertIsSearchable) {
    CoreEngine::RedBoxVector db(db_file, 3, 1000);

    uint64_t id = db.insert_auto({ 1.0f, 0.0f, 0.0f });
    int result = db.search({ 0.9f, 0.1f, 0.0f });

    EXPECT_EQ(static_cast<uint64_t>(result), id);
}

TEST_F(AutoInsertAndIndexTest, IndexUpdateOnInsert) {
    CoreEngine::RedBoxVector db(db_file, 3, 1000);

    // Manual insert then update via index (O(1) path)
    db.insert(42, { 1.0f, 0.0f, 0.0f });
    bool updated = db.update(42, { 0.0f, 1.0f, 0.0f });

    EXPECT_TRUE(updated);
    // Confirm the update took effect
    int result = db.search({ 0.0f, 0.9f, 0.1f });
    EXPECT_EQ(result, 42);
}

TEST_F(AutoInsertAndIndexTest, IndexRemovedOnDelete) {
    CoreEngine::RedBoxVector db(db_file, 3, 1000);

    db.insert(10, { 1.0f, 0.0f, 0.0f });
    db.remove(10);

    // Update should fail — ID is deleted and removed from index
    bool updated = db.update(10, { 9.0f, 9.0f, 9.0f });
    EXPECT_FALSE(updated);
}

TEST_F(AutoInsertAndIndexTest, MixedManualAndAutoIDs) {
    CoreEngine::RedBoxVector db(db_file, 3, 1000);

    // Manual insert with high ID
    db.insert(500, { 5.0f, 0.0f, 0.0f });

    // Auto inserts should still start from 1 (counter is independent)
    uint64_t id1 = db.insert_auto({ 1.0f, 0.0f, 0.0f });
    uint64_t id2 = db.insert_auto({ 2.0f, 0.0f, 0.0f });

    EXPECT_EQ(id1, 1);
    EXPECT_EQ(id2, 2);

    // All three should be searchable
    EXPECT_EQ(db.search({ 5.1f, 0.0f, 0.0f }), 500);
    EXPECT_EQ(db.search({ 1.1f, 0.0f, 0.0f }), static_cast<int>(id1));
}