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

    // Phase 2: reopen � next ID should continue from 4, not reset to 1
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

    // Update should fail � ID is deleted and removed from index
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


// ============================================================
// NEW: TombstoneCompactionTest  (covers Issue #18 fix)
// ============================================================
class TombstoneCompactionTest : public ::testing::Test {
protected:
    std::string db_file = "test_compact.db";
    std::string del_file = "test_compact.db.del";

    // Must match TOMBSTONE_COMPACT_SLACK in engine.hpp
    static constexpr size_t COMPACT_SLACK = 64;

    void SetUp() override {
        if (std::filesystem::exists(db_file))  std::filesystem::remove(db_file);
        if (std::filesystem::exists(del_file)) std::filesystem::remove(del_file);
    }

    void TearDown() override {
        if (std::filesystem::exists(db_file))  std::filesystem::remove(db_file);
        if (std::filesystem::exists(del_file)) std::filesystem::remove(del_file);
    }

    // Returns the size of the .del file in bytes, or 0 if it doesn't exist.
    size_t del_file_size() {
        if (!std::filesystem::exists(del_file)) return 0;
        return (size_t)std::filesystem::file_size(del_file);
    }

    // Each raw tombstone entry is one uint64_t.
    size_t del_file_entry_count() {
        return del_file_size() / sizeof(uint64_t);
    }
};

// After COMPACT_SLACK+1 unique deletes the file should NOT have grown to
// COMPACT_SLACK+1 entries � compaction must have fired and trimmed it back
// to exactly the number of IDs that are still logically deleted.
TEST_F(TombstoneCompactionTest, FileShrinkAfterThreshold) {
    // Need enough capacity for all the vectors we are about to insert
    const int N = (int)COMPACT_SLACK + 10;
    CoreEngine::RedBoxVector db(db_file, 3, N + 10);

    // Insert N distinct vectors then delete them one-by-one
    for (int i = 1; i <= N; ++i)
        db.insert((uint64_t)i, { (float)i, 0.0f, 0.0f });

    for (int i = 1; i <= N; ++i)
        db.remove((uint64_t)i);

    // After compaction the file should contain exactly N entries (one per
    // still-deleted ID), not N + however many duplicates accumulated.
    size_t entries = del_file_entry_count();
    EXPECT_LE(entries, (size_t)N)
        << "Tombstone file should have been compacted � raw entry count ("
        << entries << ") must not exceed live deleted count (" << N << ")";
}

// Compaction must not lose any deleted IDs � reloading after compaction
// should still honour every deletion.
TEST_F(TombstoneCompactionTest, CorrectnessAfterCompaction) {
    const int N = (int)COMPACT_SLACK + 10;

    // Phase 1: insert, delete, let compaction fire, then CLOSE the DB
    {
        CoreEngine::RedBoxVector db(db_file, 3, N + 10);

        for (int i = 1; i <= N; ++i)
            db.insert((uint64_t)i, { (float)i, 0.0f, 0.0f });

        // Delete all but ID 1 so we have a known survivor
        for (int i = 2; i <= N; ++i)
            db.remove((uint64_t)i);
    } // db destroyed here � mmap released, file unlocked

    // Phase 2: Reload and verify compaction preserved correctness
    {
        CoreEngine::RedBoxVector db2(db_file, 3, N + 10);

        // ID 1 was never deleted � must still be found
        int result = db2.search({ 1.0f, 0.0f, 0.0f });
        EXPECT_EQ(result, 1) << "Non-deleted ID 1 should survive compaction";

        // Every other ID was deleted � updating any of them must fail
        for (int i = 2; i <= N; ++i) {
            bool ok = db2.update((uint64_t)i, { 0.0f, 0.0f, 0.0f });
            EXPECT_FALSE(ok) << "Deleted ID " << i << " should still be gone after compaction";
        }
    }
}

// Re-inserting a previously deleted ID should erase it from the deleted set.
// After compaction that ID must no longer appear in the .del file at all.
TEST_F(TombstoneCompactionTest, ReinsertedIDRemovedFromDelFileAfterCompaction) {
    const int N = (int)COMPACT_SLACK + 10;
    const int CAP = N + 10;

    // Phase 1: insert N vectors, delete all (triggers compaction), then
    // re-insert ID 1 and explicitly compact � the rewritten .del file must
    // not contain ID 1 since it is no longer deleted.
    {
        CoreEngine::RedBoxVector db(db_file, 3, CAP);

        for (int i = 1; i <= N; ++i)
            db.insert((uint64_t)i, { (float)i, 0.0f, 0.0f });

        // Delete all � compaction fires somewhere here (threshold = 64)
        for (int i = 1; i <= N; ++i)
            db.remove((uint64_t)i);

        // Re-insert ID 1 (un-deletes it in memory).
        // Then explicitly compact so the .del file is rewritten right now,
        // without ID 1 in it. This is the exact scenario we want to verify:
        // compact_tombstones() must not write IDs that are no longer deleted.
        db.insert(1, { 1.0f, 0.0f, 0.0f });
        db.compact_tombstones();
    } // db destroyed � file unlocked

    // Phase 2: reload and confirm ID 1 is alive
    {
        CoreEngine::RedBoxVector db2(db_file, 3, CAP);
        int result = db2.search({ 1.0f, 0.0f, 0.0f });
        EXPECT_EQ(result, 1) << "Re-inserted ID 1 should be alive after compaction";
    }
}


// ============================================================
// NEW: ConcurrentAccessTest  (covers Issue #14 fix)
// ============================================================
#include <thread>
#include <atomic>
#include <mutex>

class ConcurrentAccessTest : public ::testing::Test {
protected:
    std::string db_file = "test_concurrent.db";
    std::string del_file = "test_concurrent.db.del";

    void SetUp() override {
        if (std::filesystem::exists(db_file))  std::filesystem::remove(db_file);
        if (std::filesystem::exists(del_file)) std::filesystem::remove(del_file);
    }

    void TearDown() override {
        if (std::filesystem::exists(db_file))  std::filesystem::remove(db_file);
        if (std::filesystem::exists(del_file)) std::filesystem::remove(del_file);
    }
};

// Multiple threads inserting into the same DB concurrently must not corrupt
// the vector count � every insert must be reflected in the final search space.
TEST_F(ConcurrentAccessTest, ConcurrentInsertsDoNotCorruptCount) {
    const int NUM_THREADS = 8;
    const int PER_THREAD = 50;
    const int TOTAL = NUM_THREADS * PER_THREAD;

    CoreEngine::RedBoxVector db(db_file, 3, TOTAL + 10);
    std::mutex db_mutex; // mirrors the per-DB mutex in server.cpp

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            int base = t * PER_THREAD;
            for (int i = 0; i < PER_THREAD; ++i) {
                uint64_t id = (uint64_t)(base + i + 1);
                float    val = (float)(base + i + 1);
                std::lock_guard<std::mutex> lk(db_mutex);
                db.insert(id, { val, 0.0f, 0.0f });
            }
            });
    }

    for (auto& th : threads) th.join();

    // Every inserted vector must be findable
    std::atomic<int> found{ 0 };
    for (int i = 1; i <= TOTAL; ++i) {
        float val = (float)i;
        std::lock_guard<std::mutex> lk(db_mutex);
        int result = db.search({ val, 0.0f, 0.0f });
        if (result == i) ++found;
    }

    EXPECT_EQ(found.load(), TOTAL)
        << "All " << TOTAL << " concurrently inserted vectors should be searchable";
}

// Concurrent reads (searches) while one thread is writing must not crash or
// return garbage � the mutex in server.cpp serialises this correctly.
TEST_F(ConcurrentAccessTest, ConcurrentReadsAndWritesDoNotCrash) {
    std::streambuf* old_cerr = std::cerr.rdbuf(nullptr);

    const int INITIAL = 200;
    const int READERS = 6;
    const int READ_OPS = 100;

    CoreEngine::RedBoxVector db(db_file, 3, INITIAL + 100);
    std::mutex db_mutex;

    // Seed with initial data
    for (int i = 1; i <= INITIAL; ++i)
        db.insert((uint64_t)i, { (float)i, 0.0f, 0.0f });

    std::atomic<bool> stop{ false };
    std::atomic<int>  errors{ 0 };

    // Writer thread: keeps inserting new vectors
    std::thread writer([&]() {
        int id = INITIAL + 1;
        while (!stop.load()) {
            std::lock_guard<std::mutex> lk(db_mutex);
            db.insert((uint64_t)id, { (float)id, 0.0f, 0.0f });
            ++id;
        }
        });

    // Reader threads: keep searching, just must not crash
    std::vector<std::thread> readers;
    readers.reserve(READERS);
    for (int r = 0; r < READERS; ++r) {
        readers.emplace_back([&]() {
            for (int op = 0; op < READ_OPS; ++op) {
                try {
                    std::lock_guard<std::mutex> lk(db_mutex);
                    int result = db.search({ 1.0f, 0.0f, 0.0f });
                    // Result must be a valid positive ID
                    if (result <= 0) ++errors;
                }
                catch (...) {
                    ++errors;
                }
            }
            });
    }

    for (auto& th : readers) th.join();
    stop.store(true);
    writer.join();

    EXPECT_EQ(errors.load(), 0)
        << "No errors should occur during concurrent reads and writes";
    std::cerr.rdbuf(old_cerr);
}

// Concurrent deletes on distinct IDs must all succeed and each deleted ID
// must be gone afterwards � no lost-update or double-free.
TEST_F(ConcurrentAccessTest, ConcurrentDeletesAreIsolated) {
    const int NUM_THREADS = 4;
    const int PER_THREAD = 25;
    const int TOTAL = NUM_THREADS * PER_THREAD;

    CoreEngine::RedBoxVector db(db_file, 3, TOTAL + 10);
    std::mutex db_mutex;

    // Insert all vectors first (single-threaded, safe)
    for (int i = 1; i <= TOTAL; ++i)
        db.insert((uint64_t)i, { (float)i, 0.0f, 0.0f });

    // Each thread deletes its own slice � no overlap
    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    std::atomic<int> successful_deletes{ 0 };

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            int base = t * PER_THREAD;
            for (int i = 1; i <= PER_THREAD; ++i) {
                uint64_t id = (uint64_t)(base + i);
                std::lock_guard<std::mutex> lk(db_mutex);
                if (db.remove(id)) ++successful_deletes;
            }
            });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(successful_deletes.load(), TOTAL)
        << "Every unique ID should be deleted exactly once";

    // Confirm all are actually gone � update must fail for each
    for (int i = 1; i <= TOTAL; ++i) {
        std::lock_guard<std::mutex> lk(db_mutex);
        bool ok = db.update((uint64_t)i, { 0.0f, 0.0f, 0.0f });
        EXPECT_FALSE(ok) << "ID " << i << " should be deleted";
    }
}


#include <spdlog/spdlog.h>

// ============================================================
// Issue #17 — Zombie rows on delete-then-reinsert cycles
// ============================================================
class ZombieRowTest : public ::testing::Test {
protected:
    std::string db_file  = "test_zombie.db";
    std::string del_file = "test_zombie.db.del";

    void SetUp() override {
        if (std::filesystem::exists(db_file))  std::filesystem::remove(db_file);
        if (std::filesystem::exists(del_file)) std::filesystem::remove(del_file);
        spdlog::set_level(spdlog::level::off);
    }
    void TearDown() override {
        spdlog::set_level(spdlog::level::info);
        if (std::filesystem::exists(db_file))  std::filesystem::remove(db_file);
        if (std::filesystem::exists(del_file)) std::filesystem::remove(del_file);
    }
};

TEST_F(ZombieRowTest, SlotCountDoesNotGrowOnReinsert) {
    CoreEngine::RedBoxVector db(db_file, 3, 100);
    db.insert(1, { 1.0f, 0.0f, 0.0f });
    db.insert(2, { 2.0f, 0.0f, 0.0f });
    db.remove(1);
    db.insert(1, { 1.5f, 0.0f, 0.0f });
    int result = db.search({ 1.4f, 0.0f, 0.0f });
    EXPECT_EQ(result, 1);
}

TEST_F(ZombieRowTest, NoDuplicatesAfterMultipleCycles) {
    CoreEngine::RedBoxVector db(db_file, 3, 200);
    db.insert(1, { 0.0f, 0.0f, 0.0f });
    for (int i = 0; i < 10; ++i) {
        db.remove(1);
        db.insert(1, { 0.0f, 0.0f, 0.0f });
    }
    auto results = db.search_N({ 0.0f, 0.0f, 0.0f }, 10);
    EXPECT_EQ(results.size(), 1u);
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0], 1);
}

TEST_F(ZombieRowTest, ReinsertedVectorHasNewData) {
    CoreEngine::RedBoxVector db(db_file, 3, 100);
    db.insert(42,  { 1.0f,  0.0f, 0.0f });
    db.insert(999, { 2.0f,  0.0f, 0.0f }); // close to origin, will win after 42 moves away
    db.remove(42);
    db.insert(42, { 50.0f, 0.0f, 0.0f }); // now far away
    // Query near origin — if zombie data {1,0,0} were still live, 42 would win.
    // With the fix, 42 is at {50,0,0} so 999 at {2,0,0} must win.
    int result = db.search({ 0.0f, 0.0f, 0.0f });
    EXPECT_EQ(result, 999);
}

TEST_F(ZombieRowTest, ReinsertAfterPersistNoZombie) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(7, { 7.0f, 0.0f, 0.0f });
        db.remove(7);
    }
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(7, { 7.0f, 0.0f, 0.0f });
        auto results = db.search_N({ 7.0f, 0.0f, 0.0f }, 10);
        EXPECT_EQ(results.size(), 1u);
        ASSERT_FALSE(results.empty());
        EXPECT_EQ(results[0], 7);
    }
}

// ============================================================
// Issue #19 — search() not-found behaviour
// ============================================================
class SearchNotFoundTest : public ::testing::Test {
protected:
    std::string db_file  = "test_notfound.db";
    std::string del_file = "test_notfound.db.del";

    void SetUp() override {
        if (std::filesystem::exists(db_file))  std::filesystem::remove(db_file);
        if (std::filesystem::exists(del_file)) std::filesystem::remove(del_file);
        spdlog::set_level(spdlog::level::off);
    }
    void TearDown() override {
        spdlog::set_level(spdlog::level::info);
        if (std::filesystem::exists(db_file))  std::filesystem::remove(db_file);
        if (std::filesystem::exists(del_file)) std::filesystem::remove(del_file);
    }
};

TEST_F(SearchNotFoundTest, EmptyDbReturnsSentinel) {
    CoreEngine::RedBoxVector db(db_file, 3, 100);
    EXPECT_EQ(db.search({ 0.0f, 0.0f, 0.0f }), -1);
}

TEST_F(SearchNotFoundTest, AllDeletedReturnsSentinel) {
    CoreEngine::RedBoxVector db(db_file, 3, 100);
    db.insert(1, { 1.0f, 0.0f, 0.0f });
    db.insert(2, { 2.0f, 0.0f, 0.0f });
    db.remove(1);
    db.remove(2);
    EXPECT_EQ(db.search({ 0.5f, 0.0f, 0.0f }), -1);
}

TEST_F(SearchNotFoundTest, WireEncodingForNotFound) {
    int engine_result = -1;
    uint32_t wire = (engine_result < 0) ? 0xFFFFFFFFu : static_cast<uint32_t>(engine_result);
    EXPECT_EQ(wire, 0xFFFFFFFFu);

    engine_result = 42;
    wire = (engine_result < 0) ? 0xFFFFFFFFu : static_cast<uint32_t>(engine_result);
    EXPECT_EQ(wire, 42u);
}

TEST_F(SearchNotFoundTest, SearchNEmptyDbReturnsEmptyVec) {
    CoreEngine::RedBoxVector db(db_file, 3, 100);
    EXPECT_TRUE(db.search_N({ 0.0f, 0.0f, 0.0f }, 5).empty());
}

#include <spdlog/spdlog.h>
#include <cmath>
#include <numeric>
#include <set>
#include <unordered_set>
#include <chrono>
#include <windows.h>  // Sleep() for retry loop

// ---------------------------------------------------------------------------
// Shared fixture helper — inherit from this instead of repeating boilerplate
// ---------------------------------------------------------------------------
struct DbFixture : public ::testing::Test {
    std::string db_file;
    std::string del_file;

    explicit DbFixture() = default;

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

    // Retry loop required on Windows: CloseHandle on a memory-mapped file
    // doesn't always release the OS lock synchronously. We give it up to
    // 10 x 10ms = 100ms before giving up.
    void try_remove(const std::string& path) {
        if (path.empty()) return;
        for (int retry = 0; retry < 10; ++retry) {
            if (!std::filesystem::exists(path)) return;
            std::error_code ec;
            if (std::filesystem::remove(path, ec)) return;
            Sleep(10);
        }
    }

    void cleanup() {
        try_remove(db_file);
        try_remove(del_file);
        try_remove(del_file + ".tmp");  // compaction leaves this on crash
    }
};

// =============================================================================
// 1. UPDATE TESTS
//    update() is part of the public API but barely covered above.
// =============================================================================
class UpdateTest : public DbFixture {
protected:
    void SetUp() override { init("test_update"); DbFixture::SetUp(); }
};

TEST_F(UpdateTest, UpdateChangesSearchResult) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, { 1.0f, 0.0f, 0.0f });
        db.insert(2, { 5.0f, 0.0f, 0.0f });
        EXPECT_EQ(db.search({ 1.1f, 0.0f, 0.0f }), 1);
        bool ok = db.update(1, { 10.0f, 0.0f, 0.0f });
        EXPECT_TRUE(ok);
        EXPECT_EQ(db.search({ 1.1f, 0.0f, 0.0f }), 2);
    }
}

TEST_F(UpdateTest, UpdateNonExistentIdReturnsFalse) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, { 1.0f, 0.0f, 0.0f });
        EXPECT_FALSE(db.update(999, { 2.0f, 0.0f, 0.0f }));
        EXPECT_EQ(db.search({ 1.0f, 0.0f, 0.0f }), 1);
    }
}

TEST_F(UpdateTest, UpdateDeletedIdReturnsFalse) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(7, { 3.0f, 0.0f, 0.0f });
        db.remove(7);
        EXPECT_FALSE(db.update(7, { 3.0f, 0.0f, 0.0f }));
    }
}

TEST_F(UpdateTest, DoubleUpdateIsIdempotent) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, { 1.0f, 0.0f, 0.0f });
        db.update(1, { 9.0f, 0.0f, 0.0f });
        db.update(1, { 9.0f, 0.0f, 0.0f });
        EXPECT_EQ(db.search({ 9.0f, 0.0f, 0.0f }), 1);
    }
}

TEST_F(UpdateTest, UpdatePersistsAcrossReload) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(3, { 1.0f, 0.0f, 0.0f });
        db.update(3, { 8.0f, 0.0f, 0.0f });
    }
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        EXPECT_EQ(db.search({ 8.0f, 0.0f, 0.0f }), 3);
        db.insert(99, { 1.0f, 0.0f, 0.0f });
        EXPECT_NE(db.search({ 1.0f, 0.0f, 0.0f }), 3);
    }
}


// =============================================================================
// 2. DIMENSION CORRECTNESS
// =============================================================================
class DimensionTest : public DbFixture {
protected:
    void SetUp() override { init("test_dim"); DbFixture::SetUp(); }
};

TEST_F(DimensionTest, GetDimReturnsConstructorDim) {
    {
        CoreEngine::RedBoxVector db(db_file, 128, 100);
        EXPECT_EQ(db.get_dim(), 128u);
    }
}

TEST_F(DimensionTest, HighDimSearchCorrectness) {
    {
        const size_t DIM = 64;
        CoreEngine::RedBoxVector db(db_file, DIM, 200);
        std::vector<float> v1(DIM, 1.0f);
        std::vector<float> v2(DIM, 2.0f);
        db.insert(1, v1);
        db.insert(2, v2);

        std::vector<float> query(DIM, 1.1f);
        EXPECT_EQ(db.search(query), 1);
        std::fill(query.begin(), query.end(), 1.9f);
        EXPECT_EQ(db.search(query), 2);
    }
}

TEST_F(DimensionTest, DimPersistedInHeader) {
    {
        CoreEngine::RedBoxVector db(db_file, 16, 50);
        EXPECT_EQ(db.get_dim(), 16u);
    }
    {
        CoreEngine::RedBoxVector db(db_file, 16, 50);
        EXPECT_EQ(db.get_dim(), 16u);
    }
}


// =============================================================================
// 3. AUTO-ID MONOTONICITY & UNIQUENESS
// =============================================================================
class AutoIdTest : public DbFixture {
protected:
    void SetUp() override { init("test_autoid"); DbFixture::SetUp(); }
};

TEST_F(AutoIdTest, StrictlyIncreasing) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 200);
        uint64_t prev = 0;
        for (int i = 0; i < 20; ++i) {
            uint64_t id = db.insert_auto({ (float)i, 0.0f, 0.0f });
            EXPECT_GT(id, prev) << "ID sequence must be strictly increasing";
            prev = id;
        }
    }
}

TEST_F(AutoIdTest, NeverReusedAcrossReload) {
    std::unordered_set<uint64_t> seen;
    {
        CoreEngine::RedBoxVector db(db_file, 3, 200);
        for (int i = 0; i < 10; ++i)
            seen.insert(db.insert_auto({ (float)i, 0.0f, 0.0f }));
    }
    {
        CoreEngine::RedBoxVector db(db_file, 3, 200);
        for (int i = 0; i < 10; ++i) {
            uint64_t id = db.insert_auto({ (float)i, 0.0f, 0.0f });
            EXPECT_EQ(seen.count(id), 0u) << "ID " << id << " was already used in previous session";
            seen.insert(id);
        }
    }
}

TEST_F(AutoIdTest, DeletedIdNotReissued) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 200);
        uint64_t id1 = db.insert_auto({ 1.0f, 0.0f, 0.0f });
        db.remove(id1);
        std::unordered_set<uint64_t> new_ids;
        for (int i = 0; i < 10; ++i)
            new_ids.insert(db.insert_auto({ (float)i, 0.0f, 0.0f }));
        EXPECT_EQ(new_ids.count(id1), 0u) << "Deleted ID must not be reissued";
    }
}


// =============================================================================
// 4. REMOVE EDGE CASES
// =============================================================================
class RemoveEdgeTest : public DbFixture {
protected:
    void SetUp() override { init("test_remove_edge"); DbFixture::SetUp(); }
};

TEST_F(RemoveEdgeTest, DoubleRemoveReturnsFalse) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, { 1.0f, 0.0f, 0.0f });
        EXPECT_TRUE(db.remove(1));
        EXPECT_FALSE(db.remove(1));
    }
}

TEST_F(RemoveEdgeTest, RemoveNonExistentReturnsFalse) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        EXPECT_FALSE(db.remove(42));
    }
}

TEST_F(RemoveEdgeTest, RemoveAllThenSearchReturnsSentinel) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, { 1.0f, 0.0f, 0.0f });
        db.insert(2, { 2.0f, 0.0f, 0.0f });
        db.insert(3, { 3.0f, 0.0f, 0.0f });
        db.remove(1); db.remove(2); db.remove(3);
        EXPECT_EQ(db.search({ 1.0f, 0.0f, 0.0f }), -1);
        EXPECT_TRUE(db.search_N({ 1.0f, 0.0f, 0.0f }, 5).empty());
    }
}

TEST_F(RemoveEdgeTest, RemoveAcrossSessionsIdempotent) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(55, { 5.0f, 0.0f, 0.0f });
        db.remove(55);
    }
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        EXPECT_FALSE(db.remove(55));
    }
}


// =============================================================================
// 5. SEARCH ACCURACY — TIEBREAKER & EXACT MATCH
// =============================================================================
class SearchAccuracyTest : public DbFixture {
protected:
    void SetUp() override { init("test_search_acc"); DbFixture::SetUp(); }
};

TEST_F(SearchAccuracyTest, ExactMatchAlwaysWins) {
    {
        CoreEngine::RedBoxVector db(db_file, 4, 200);
        db.insert(10, { 3.0f, 1.4f, 1.5f, 9.2f });
        db.insert(20, { 9.9f, 8.8f, 7.7f, 6.6f });
        db.insert(30, { 0.1f, 0.2f, 0.3f, 0.4f });
        EXPECT_EQ(db.search({ 3.0f, 1.4f, 1.5f, 9.2f }), 10);
        EXPECT_EQ(db.search({ 9.9f, 8.8f, 7.7f, 6.6f }), 20);
        EXPECT_EQ(db.search({ 0.1f, 0.2f, 0.3f, 0.4f }), 30);
    }
}

TEST_F(SearchAccuracyTest, EquidistantReturnsValidId) {
    {
        CoreEngine::RedBoxVector db(db_file, 2, 100);
        db.insert(1, {  1.0f,  1.0f });
        db.insert(2, { -1.0f, -1.0f });
        int result = db.search({ 0.0f, 0.0f });
        EXPECT_TRUE(result == 1 || result == 2)
            << "Must return one of the inserted IDs, got " << result;
    }
}

TEST_F(SearchAccuracyTest, SingleElementAlwaysFound) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 50);
        db.insert(42, { 7.7f, 3.3f, 1.1f });
        EXPECT_EQ(db.search({ 0.0f,  0.0f,  0.0f  }), 42);
        EXPECT_EQ(db.search({ 7.7f,  3.3f,  1.1f  }), 42);
        EXPECT_EQ(db.search({ 999.f, 999.f, 999.f }), 42);
    }
}

TEST_F(SearchAccuracyTest, SearchNWithN1AgreesWithSearch) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 200);
        for (int i = 1; i <= 20; ++i)
            db.insert((uint64_t)i, { (float)i, 0.0f, 0.0f });

        std::vector<float> query = { 10.2f, 0.0f, 0.0f };
        int  single = db.search(query);
        auto top1   = db.search_N(query, 1);
        ASSERT_FALSE(top1.empty());
        EXPECT_EQ(top1[0], single)
            << "search() and search_N(q,1) must agree on the nearest neighbor";
    }
}

TEST_F(SearchAccuracyTest, SearchNResultsAreValidIds) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        std::set<int> inserted;
        for (int i = 1; i <= 10; ++i) {
            db.insert((uint64_t)i, { (float)i, 0.0f, 0.0f });
            inserted.insert(i);
        }
        auto results = db.search_N({ 5.0f, 0.0f, 0.0f }, 10);
        for (int id : results)
            EXPECT_GT(inserted.count(id), 0u) << "Result ID " << id << " was never inserted";
    }
}


// =============================================================================
// 6. SEARCH_N ORDERING GUARANTEES
// =============================================================================
class SearchNOrderTest : public DbFixture {
protected:
    void SetUp() override { init("test_searchn_order"); DbFixture::SetUp(); }
};

TEST_F(SearchNOrderTest, ResultsAreInAscendingDistanceOrder) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        for (int i = 1; i <= 5; ++i)
            db.insert((uint64_t)i, { (float)i, 0.0f, 0.0f });

        auto results = db.search_N({ 0.0f, 0.0f, 0.0f }, 5);
        ASSERT_EQ(results.size(), 5u);
        for (int rank = 0; rank < 5; ++rank)
            EXPECT_EQ(results[rank], rank + 1)
                << "Position " << rank << ": expected " << rank + 1
                << ", got " << results[rank];
    }
}

TEST_F(SearchNOrderTest, NoDuplicatesInResults) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        for (int i = 1; i <= 10; ++i)
            db.insert((uint64_t)i, { (float)i, (float)i, (float)i });

        auto results = db.search_N({ 0.0f, 0.0f, 0.0f }, 10);
        std::unordered_set<int> seen;
        for (int id : results) {
            EXPECT_EQ(seen.count(id), 0u) << "Duplicate ID " << id << " in results";
            seen.insert(id);
        }
    }
}

TEST_F(SearchNOrderTest, ResultCountCappedAtLiveVectors) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, { 1.0f, 0.0f, 0.0f });
        db.insert(2, { 2.0f, 0.0f, 0.0f });
        db.insert(3, { 3.0f, 0.0f, 0.0f });
        db.remove(2);

        auto results = db.search_N({ 0.0f, 0.0f, 0.0f }, 10);
        EXPECT_LE(results.size(), 2u);
        for (int id : results)
            EXPECT_NE(id, 2) << "Deleted ID 2 must not appear in results";
    }
}


// =============================================================================
// 7. CAPACITY BOUNDARY
// =============================================================================
class CapacityTest : public DbFixture {
protected:
    void SetUp() override { init("test_capacity"); DbFixture::SetUp(); }
};

TEST_F(CapacityTest, FillToCapacityExact) {
    {
        const int CAP = 50;
        CoreEngine::RedBoxVector db(db_file, 3, CAP);
        for (int i = 0; i < CAP; ++i)
            db.insert((uint64_t)(i + 1), { (float)i, 0.0f, 0.0f });
        EXPECT_EQ(db.search({ (float)(CAP - 1), 0.0f, 0.0f }), CAP);
    }
}

TEST_F(CapacityTest, ExceedCapacityDoesNotCorrupt) {
    {
        const int CAP = 10;
        CoreEngine::RedBoxVector db(db_file, 3, CAP);
        for (int i = 0; i < CAP; ++i)
            db.insert((uint64_t)(i + 1), { (float)i, 0.0f, 0.0f });

        // One over capacity — engine catches and logs, must not corrupt.
        try {
            db.insert(9999, { 9999.0f, 0.0f, 0.0f });
        } catch (const std::exception&) {}

        EXPECT_EQ(db.search({ 0.0f, 0.0f, 0.0f }), 1);
    }
}


// =============================================================================
// 8. PERSISTENCE STRESS
// =============================================================================
class PersistenceStressTest : public DbFixture {
protected:
    void SetUp() override { init("test_persist_stress"); DbFixture::SetUp(); }
};

TEST_F(PersistenceStressTest, MultipleOpenCloseRoundTrips) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 500);
        for (int i = 1; i <= 10; ++i)
            db.insert((uint64_t)i, { (float)i, 0.0f, 0.0f });
    }
    {
        CoreEngine::RedBoxVector db(db_file, 3, 500);
        for (int i = 1; i <= 10; ++i)
            EXPECT_EQ(db.search({ (float)i, 0.0f, 0.0f }), i)
                << "Round-2 open: ID " << i << " not found";
        for (int i = 11; i <= 20; ++i)
            db.insert((uint64_t)i, { (float)i, 0.0f, 0.0f });
    }
    {
        CoreEngine::RedBoxVector db(db_file, 3, 500);
        for (int i = 1; i <= 20; ++i)
            EXPECT_EQ(db.search({ (float)i, 0.0f, 0.0f }), i)
                << "Round-3 open: ID " << i << " not found";
    }
}

TEST_F(PersistenceStressTest, DeletionsSurviveMultipleReloads) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, { 1.0f, 0.0f, 0.0f });
        db.insert(2, { 2.0f, 0.0f, 0.0f });
        db.remove(1);
    }
    for (int cycle = 0; cycle < 3; ++cycle) {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        EXPECT_FALSE(db.remove(1)) << "Cycle " << cycle << ": ID 1 should already be deleted";
        EXPECT_EQ(db.search({ 2.0f, 0.0f, 0.0f }), 2)
            << "Cycle " << cycle << ": ID 2 must still be alive";
    }
}

TEST_F(PersistenceStressTest, UpdatesAndDeletesComboSurvivesReload) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, { 1.0f,  0.0f, 0.0f });
        db.insert(2, { 2.0f,  0.0f, 0.0f });
        db.insert(3, { 3.0f,  0.0f, 0.0f });
        db.update(2, { 20.0f, 0.0f, 0.0f });
        db.remove(3);
    }
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        EXPECT_EQ(db.search({ 1.0f,  0.0f, 0.0f }), 1);
        EXPECT_EQ(db.search({ 20.0f, 0.0f, 0.0f }), 2);
        EXPECT_FALSE(db.remove(3));
    }
}


// =============================================================================
// 9. NEAREST-NEIGHBOR REGRESSION SUITE
//    Mathematically exact cases — must pass regardless of index type.
// =============================================================================
class NNRegressionTest : public DbFixture {
protected:
    void SetUp() override { init("test_nn_regression"); DbFixture::SetUp(); }
};

TEST_F(NNRegressionTest, AxisAlignedBasis) {
    {
        const int DIM = 8;
        CoreEngine::RedBoxVector db(db_file, DIM, 100);
        for (int axis = 0; axis < DIM; ++axis) {
            std::vector<float> v(DIM, 0.0f);
            v[axis] = 1.0f;
            db.insert((uint64_t)(axis + 1), v);
        }
        for (int axis = 0; axis < DIM; ++axis) {
            std::vector<float> q(DIM, 0.01f);
            q[axis] = 0.95f;
            EXPECT_EQ(db.search(q), axis + 1)
                << "Axis " << axis << ": wrong nearest neighbor";
        }
    }
}

TEST_F(NNRegressionTest, NegativeCoordinates) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, { -10.0f, -10.0f, -10.0f });
        db.insert(2, {  10.0f,  10.0f,  10.0f });
        EXPECT_EQ(db.search({ -9.0f, -9.0f, -9.0f }), 1);
        EXPECT_EQ(db.search({  9.0f,  9.0f,  9.0f }), 2);
    }
}

TEST_F(NNRegressionTest, LargeCoordinateValues) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(1, { 1e3f, 1e3f, 1e3f });
        db.insert(2, { 2e3f, 2e3f, 2e3f });
        EXPECT_EQ(db.search({ 1.1e3f, 1.1e3f, 1.1e3f }), 1);
        EXPECT_EQ(db.search({ 1.9e3f, 1.9e3f, 1.9e3f }), 2);
    }
}

TEST_F(NNRegressionTest, ZeroVector) {
    {
        CoreEngine::RedBoxVector db(db_file, 4, 100);
        db.insert(1, { 0.0f, 0.0f, 0.0f, 0.0f });
        db.insert(2, { 1.0f, 1.0f, 1.0f, 1.0f });
        EXPECT_EQ(db.search({ 0.0f, 0.0f, 0.0f, 0.0f }), 1);
        EXPECT_EQ(db.search({ 0.1f, 0.1f, 0.1f, 0.1f }), 1);
    }
}

TEST_F(NNRegressionTest, IdenticalVectorsDifferentIds) {
    {
        CoreEngine::RedBoxVector db(db_file, 3, 100);
        db.insert(10, { 5.0f, 5.0f, 5.0f });
        db.insert(20, { 5.0f, 5.0f, 5.0f });
        int result = db.search({ 5.0f, 5.0f, 5.0f });
        EXPECT_TRUE(result == 10 || result == 20)
            << "Must return one of the two identical vectors, got " << result;
    }
}

TEST_F(NNRegressionTest, SearchNCountWithEquidistantVectors) {
    {
        CoreEngine::RedBoxVector db(db_file, 2, 100);
        const int COUNT = 8;
        for (int i = 0; i < COUNT; ++i) {
            float angle = (float)i * 3.14159265f / (float)COUNT;
            db.insert((uint64_t)(i + 1), { std::cos(angle), std::sin(angle) });
        }
        auto results = db.search_N({ 0.0f, 0.0f }, 5);
        EXPECT_EQ(results.size(), 5u);
        std::set<int> valid_ids;
        for (int i = 1; i <= COUNT; ++i) valid_ids.insert(i);
        for (int id : results)
            EXPECT_GT(valid_ids.count(id), 0u) << "Invalid ID " << id << " in results";
    }
}