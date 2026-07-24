#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <mutex>
#include <redboxdb/SpecificMetadata.hpp>

namespace Metadata {

struct DbInfo {
    std::string name;
    uint32_t    dimensions;
    CoreEngine::IndexType index_type;
    uint64_t    capacity;
    uint64_t    vector_count;
    uint64_t    next_id;
};

class Store {
public:
    Store(const std::string& conninfo, int pool_size = 4);
    ~Store();

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    void run_migrations(const std::string& schema_path);
    void seed_from_files(const std::string& data_dir);

    void create_database(const std::string& name, uint32_t dim,
                         CoreEngine::IndexType type, uint64_t capacity,
                         const CoreEngine::SpecificMetadata& params);
    void load_database(const std::string& name, CoreEngine::SpecificMetadata& out);
    void update_counts(const std::string& name, uint64_t vector_count, uint64_t next_id);
    void update_hnsw_state(const std::string& name, uint32_t entry_point, uint32_t graph_version);
    void drop_database(const std::string& name);
    void list_databases(std::vector<DbInfo>& out);

    void log_operation(const std::string& db_name, const std::string& op, uint64_t vec_id = 0);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
