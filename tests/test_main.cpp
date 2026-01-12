#include <gtest/gtest.h>
#include <filesystem>
#include <vector>
#include <string>
#include "redboxdb/engine.hpp" // Ensure this path is correct

// Test Fixture to handle file cleanup automatically
class RedBoxDbTest : public ::testing::Test {
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

TEST_F(RedBoxDbTest, InsertAndSearchInMemory) {
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

TEST_F(RedBoxDbTest, PersistenceCheck) {
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

TEST_F(RedBoxDbTest, LargeDatasetHandling) {
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

TEST_F(RedBoxDbTest, ZeroCopyCorrectness) {
    // This verifies that your new "Raw Pointer" search didn't break anything
    CoreEngine::RedBoxVector db(test_db_file, dim, capacity);

    db.insert(100, { 10.0f, 10.0f, 10.0f }); // Far away
    db.insert(200, { 1.0f, 1.0f, 1.0f });   // Target

    int result = db.search({ 1.1f, 1.1f, 1.1f });
    EXPECT_EQ(result, 200);
}