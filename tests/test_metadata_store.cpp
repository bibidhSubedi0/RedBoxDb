#include <gtest/gtest.h>
#include "redboxdb/metadata_store.hpp"
#include "redboxdb/SpecificMetadata.hpp"
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>

static std::string get_pg_conninfo() {
    const char* host = std::getenv("REDBOX_PG_HOST");
    const char* port = std::getenv("REDBOX_PG_PORT");
    const char* db   = std::getenv("REDBOX_PG_DBNAME");
    const char* user = std::getenv("REDBOX_PG_USER");
    const char* pass = std::getenv("REDBOX_PG_PASSWORD");

    return "host=" + std::string(host ? host : "localhost")
         + " port=" + std::string(port ? port : "5432")
         + " dbname=" + std::string(db ? db : "redbox_test")
         + " user=" + std::string(user ? user : "redbox")
         + " password=" + std::string(pass ? pass : "test");
}

static bool pg_available() {
    try {
        Metadata::Store test(get_pg_conninfo(), 1);
        return true;
    } catch (...) {
        return false;
    }
}

class MetadataStoreTest : public ::testing::Test {
protected:
    static std::unique_ptr<Metadata::Store> store;
    static bool pg_ok;

    static void SetUpTestSuite() {
        pg_ok = pg_available();
        if (!pg_ok) {
            GTEST_SKIP() << "PostgreSQL not available (set REDBOX_PG_* env vars)";
        }
        store = std::make_unique<Metadata::Store>(get_pg_conninfo(), 2);
        std::string schema_path = std::string(REDBOX_SOURCE_DIR) + "/sql/schema.sql";
        store->run_migrations(schema_path);
    }

    static void TearDownTestSuite() {
        if (store) {
            store.reset();
        }
    }
};

std::unique_ptr<Metadata::Store> MetadataStoreTest::store = nullptr;
bool MetadataStoreTest::pg_ok = false;

TEST_F(MetadataStoreTest, CreateDatabase) {
    CoreEngine::SpecificMetadata params{};
    params.vector_count = 0;
    params.max_capacity = 10000;
    params.dimensions = 128;
    params.next_id = 1;

    store->create_database("test_create", 128, CoreEngine::IndexType::IVF, 10000, params);

    CoreEngine::SpecificMetadata loaded{};
    store->load_database("test_create", loaded);

    EXPECT_EQ(loaded.dimensions, 128u);
    EXPECT_EQ(loaded.max_capacity, 10000u);
    EXPECT_EQ(loaded.vector_count, 0u);
}

TEST_F(MetadataStoreTest, CreateHnswDatabase) {
    CoreEngine::SpecificMetadata params{};
    params.vector_count = 0;
    params.max_capacity = 5000;
    params.dimensions = 64;
    params.next_id = 1;
    params.hnsw_M = 16;
    params.hnsw_ef_construction = 200;
    params.hnsw_ef_search = 256;
    params.hnsw_max_level = 16;
    params.index_type = static_cast<uint8_t>(CoreEngine::IndexType::HNSW);

    store->create_database("test_hnsw", 64, CoreEngine::IndexType::HNSW, 5000, params);

    CoreEngine::SpecificMetadata loaded{};
    store->load_database("test_hnsw", loaded);

    EXPECT_EQ(loaded.dimensions, 64u);
    EXPECT_EQ(loaded.hnsw_M, 16);
    EXPECT_EQ(loaded.hnsw_ef_construction, 200);
    EXPECT_EQ(loaded.index_type, static_cast<uint8_t>(CoreEngine::IndexType::HNSW));
}

TEST_F(MetadataStoreTest, UpdateCounts) {
    CoreEngine::SpecificMetadata params{};
    params.dimensions = 128;
    params.max_capacity = 10000;
    params.vector_count = 0;
    params.next_id = 1;

    store->create_database("test_counts", 128, CoreEngine::IndexType::IVF, 10000, params);
    store->update_counts("test_counts", 42, 43);

    CoreEngine::SpecificMetadata loaded{};
    store->load_database("test_counts", loaded);

    EXPECT_EQ(loaded.vector_count, 42u);
    EXPECT_EQ(loaded.next_id, 43u);
}

TEST_F(MetadataStoreTest, UpdateHnswState) {
    CoreEngine::SpecificMetadata params{};
    params.dimensions = 128;
    params.max_capacity = 10000;
    params.vector_count = 0;
    params.next_id = 1;
    params.hnsw_M = 16;
    params.hnsw_ef_construction = 200;
    params.index_type = static_cast<uint8_t>(CoreEngine::IndexType::HNSW);

    store->create_database("test_hnsw_state", 128, CoreEngine::IndexType::HNSW, 10000, params);
    store->update_hnsw_state("test_hnsw_state", 5, 3);

    CoreEngine::SpecificMetadata loaded{};
    store->load_database("test_hnsw_state", loaded);

    EXPECT_EQ(loaded.hnsw_entry_point, 5u);
    EXPECT_EQ(loaded.hnsw_graph_version, 3u);
}

TEST_F(MetadataStoreTest, ListDatabases) {
    std::vector<Metadata::DbInfo> dbs;
    store->list_databases(dbs);

    EXPECT_GE(dbs.size(), 2u);

    bool found_create = false, found_hnsw = false;
    for (auto& d : dbs) {
        if (d.name == "test_create") found_create = true;
        if (d.name == "test_hnsw") found_hnsw = true;
    }
    EXPECT_TRUE(found_create);
    EXPECT_TRUE(found_hnsw);
}

TEST_F(MetadataStoreTest, DropDatabase) {
    CoreEngine::SpecificMetadata params{};
    params.dimensions = 32;
    params.max_capacity = 1000;
    params.vector_count = 0;
    params.next_id = 1;

    store->create_database("test_drop", 32, CoreEngine::IndexType::IVF, 1000, params);
    store->drop_database("test_drop");

    EXPECT_THROW(store->load_database("test_drop", params), std::exception);
}

TEST_F(MetadataStoreTest, AuditLog) {
    store->log_operation("test_create", "INSERT", 1);
    store->log_operation("test_create", "SEARCH", 0);
    store->log_operation("test_create", "DELETE", 1);

    std::vector<Metadata::DbInfo> dbs;
    store->list_databases(dbs);

    EXPECT_GE(dbs.size(), 1u);
}

TEST_F(MetadataStoreTest, SeedFromFiles) {
    std::string tmp_dir = "/tmp/redbox_meta_test";
    std::filesystem::create_directories(tmp_dir);

    std::string db_path = tmp_dir + "/test_seed.db";
    int fd = open(db_path.c_str(), O_RDWR | O_CREAT, 0644);
    ASSERT_GE(fd, 0);

    CoreEngine::SpecificMetadata header{};
    header.dimensions = 64;
    header.max_capacity = 500;
    header.vector_count = 10;
    header.next_id = 11;
    header.index_type = static_cast<uint8_t>(CoreEngine::IndexType::IVF);

    ssize_t wr = write(fd, &header, sizeof(header));
    EXPECT_EQ(wr, (ssize_t)sizeof(header));
    int tr = ftruncate(fd, sizeof(header));
    EXPECT_EQ(tr, 0);
    close(fd);

    store->seed_from_files(tmp_dir);

    CoreEngine::SpecificMetadata loaded{};
    store->load_database("test_seed", loaded);

    EXPECT_EQ(loaded.dimensions, 64u);
    EXPECT_EQ(loaded.vector_count, 10u);

    store->drop_database("test_seed");
    std::filesystem::remove_all(tmp_dir);
}
