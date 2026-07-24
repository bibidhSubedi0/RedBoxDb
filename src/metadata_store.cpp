#include "redboxdb/metadata_store.hpp"
#include <pqxx/pqxx>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cstring>
#include <unordered_set>

#include <redboxdb/SpecificMetadata.hpp>

#ifndef _WIN32
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#else
    #include <windows.h>
    #include <fcntl.h>
    #include <io.h>
#endif

namespace Metadata {

struct Store::Impl {
    std::unique_ptr<pqxx::connection> conn;
    std::mutex mtx;

    explicit Impl(const std::string& conninfo)
        : conn(std::make_unique<pqxx::connection>(conninfo))
    {}
};

Store::Store(const std::string& conninfo, int /*pool_size*/)
    : impl_(std::make_unique<Impl>(conninfo))
{
    std::cout << "[METADATA] Connected to PostgreSQL\n";
}

Store::~Store() = default;

void Store::run_migrations(const std::string& schema_path) {
    std::ifstream file(schema_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open schema file: " + schema_path);
    }
    std::stringstream ss;
    ss << file.rdbuf();
    std::string sql = ss.str();

    std::lock_guard<std::mutex> lock(impl_->mtx);
    pqxx::work tx(*impl_->conn);
    tx.exec(sql);
    tx.commit();
    std::cout << "[METADATA] Schema migrations applied from " << schema_path << "\n";
}

void Store::create_database(const std::string& name, uint32_t dim,
                            CoreEngine::IndexType type, uint64_t capacity,
                            const CoreEngine::SpecificMetadata& params) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    pqxx::work tx(*impl_->conn);

    std::string type_str = (type == CoreEngine::IndexType::HNSW) ? "HNSW" : "IVF";

    tx.exec(
        "INSERT INTO databases (name, dimensions, index_type, capacity, vector_count, next_id, "
        "hnsw_m, hnsw_ef_construction, hnsw_ef_search, hnsw_max_level, hnsw_entry_point, "
        "hnsw_graph_version, ivf_num_clusters, ivf_num_probes, ivf_initialized) "
        "VALUES ($1, $2, $3::index_type, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15)",
        pqxx::params{name, (int)dim, type_str, (long)capacity,
                     (long)params.vector_count, (long)params.next_id,
                     (int)params.hnsw_M, (int)params.hnsw_ef_construction,
                     (int)params.hnsw_ef_search, (int)params.hnsw_max_level,
                     (int)params.hnsw_entry_point, (int)params.hnsw_graph_version,
                     (int)params.num_clusters, (int)params.num_probes,
                     params.is_initialized != 0}
    );
    tx.commit();
    std::cout << "[METADATA] Created database: " << name << " (" << type_str << ", dim=" << dim << ")\n";
}

void Store::load_database(const std::string& name, CoreEngine::SpecificMetadata& out) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    pqxx::read_transaction tx(*impl_->conn);

    auto result = tx.exec(
        "SELECT dimensions, index_type, capacity, vector_count, next_id, "
        "hnsw_m, hnsw_ef_construction, hnsw_ef_search, hnsw_max_level, "
        "hnsw_entry_point, hnsw_graph_version, "
        "ivf_num_clusters, ivf_num_probes, ivf_initialized "
        "FROM databases WHERE name = $1",
        pqxx::params{name}
    );

    if (result.empty()) {
        throw std::runtime_error("Database not found in metadata: " + name);
    }

    auto row = result[0];
    memset(&out, 0, sizeof(out));

    out.dimensions     = row["dimensions"].as<uint64_t>();
    out.vector_count   = row["vector_count"].as<uint64_t>();
    out.max_capacity   = row["capacity"].as<uint64_t>();
    out.next_id        = row["next_id"].as<uint64_t>();
    out.data_type_size = 4;

    std::string idx = row["index_type"].as<std::string>();
    out.index_type = (idx == "HNSW") ? static_cast<uint8_t>(CoreEngine::IndexType::HNSW)
                                     : static_cast<uint8_t>(CoreEngine::IndexType::IVF);

    if (out.index_type == static_cast<uint8_t>(CoreEngine::IndexType::HNSW)) {
        out.hnsw_M               = row["hnsw_m"].as<int>();
        out.hnsw_ef_construction  = row["hnsw_ef_construction"].as<int>();
        out.hnsw_ef_search        = row["hnsw_ef_search"].as<int>();
        out.hnsw_max_level        = row["hnsw_max_level"].as<int>();
        out.hnsw_entry_point      = row["hnsw_entry_point"].as<uint32_t>();
        out.hnsw_graph_version    = row["hnsw_graph_version"].as<uint32_t>();
    } else {
        out.num_clusters  = row["ivf_num_clusters"].as<int>();
        out.num_probes    = row["ivf_num_probes"].as<int>();
        out.is_initialized = row["ivf_initialized"].as<bool>() ? 1 : 0;
    }

    out.version = CoreEngine::SpecificMetadata::CURRENT_VERSION;
}

void Store::update_counts(const std::string& name, uint64_t vector_count, uint64_t next_id) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    pqxx::work tx(*impl_->conn);
    tx.exec(
        "UPDATE databases SET vector_count = $1, next_id = $2, updated_at = NOW() WHERE name = $3",
        pqxx::params{(long)vector_count, (long)next_id, name}
    );
    tx.commit();
}

void Store::update_hnsw_state(const std::string& name, uint32_t entry_point, uint32_t graph_version) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    pqxx::work tx(*impl_->conn);
    tx.exec(
        "UPDATE databases SET hnsw_entry_point = $1, hnsw_graph_version = $2, updated_at = NOW() WHERE name = $3",
        pqxx::params{(int)entry_point, (int)graph_version, name}
    );
    tx.commit();
}

void Store::drop_database(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    pqxx::work tx(*impl_->conn);
    tx.exec("DELETE FROM databases WHERE name = $1", pqxx::params{name});
    tx.commit();
    std::cout << "[METADATA] Dropped database: " << name << "\n";
}

void Store::list_databases(std::vector<DbInfo>& out) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    pqxx::read_transaction tx(*impl_->conn);

    auto result = tx.exec(
        "SELECT name, dimensions, index_type, capacity, vector_count, next_id FROM databases ORDER BY name"
    );

    out.clear();
    out.reserve(result.size());
    for (auto row : result) {
        DbInfo info;
        info.name         = row["name"].as<std::string>();
        info.dimensions   = row["dimensions"].as<uint32_t>();
        info.capacity     = row["capacity"].as<uint64_t>();
        info.vector_count = row["vector_count"].as<uint64_t>();
        info.next_id      = row["next_id"].as<uint64_t>();

        std::string idx = row["index_type"].as<std::string>();
        info.index_type  = (idx == "HNSW") ? CoreEngine::IndexType::HNSW
                                           : CoreEngine::IndexType::IVF;
        out.push_back(std::move(info));
    }
}

void Store::log_operation(const std::string& db_name, const std::string& op, uint64_t vec_id) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    try {
        pqxx::work tx(*impl_->conn);
        tx.exec(
            "INSERT INTO audit_log (db_name, operation, vector_id) VALUES ($1, $2, $3)",
            pqxx::params{db_name, op, (long)vec_id}
        );
        tx.commit();
    } catch (const std::exception& e) {
        std::cerr << "[METADATA] Audit log error: " << e.what() << "\n";
    }
}

void Store::seed_from_files(const std::string& data_dir) {
    namespace fs = std::filesystem;

    if (!fs::exists(data_dir)) {
        std::cout << "[METADATA] Data directory not found, skipping seed: " << data_dir << "\n";
        return;
    }

    std::unordered_set<std::string> known;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        pqxx::read_transaction tx(*impl_->conn);
        auto existing = tx.exec("SELECT name FROM databases");
        for (auto row : existing) {
            known.insert(row["name"].as<std::string>());
        }
    }

    for (auto& entry : fs::directory_iterator(data_dir)) {
        if (!entry.is_regular_file()) continue;
        std::string path = entry.path().string();
        std::string ext = entry.path().extension().string();
        if (ext != ".db") continue;

        std::string db_name = entry.path().stem().string();
        if (known.count(db_name)) continue;

        int fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) continue;

        struct stat st;
        if (fstat(fd, &st) < 0 || st.st_size < 128) {
            close(fd);
            continue;
        }

        void* map = mmap(nullptr, 128, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map == MAP_FAILED) {
            close(fd);
            continue;
        }

        CoreEngine::SpecificMetadata header;
        memcpy(&header, map, sizeof(header));
        munmap(map, 128);
        close(fd);

        std::cout << "[METADATA] Seeding from file: " << db_name
                  << " (dim=" << header.dimensions
                  << " count=" << header.vector_count
                  << " type=" << (header.index_type == 1 ? "HNSW" : "IVF") << ")\n";

        std::lock_guard<std::mutex> lock(impl_->mtx);
        pqxx::work tx(*impl_->conn);
        tx.exec(
            "INSERT INTO databases (name, dimensions, index_type, capacity, vector_count, next_id, "
            "hnsw_m, hnsw_ef_construction, hnsw_ef_search, hnsw_max_level, hnsw_entry_point, "
            "hnsw_graph_version, ivf_num_clusters, ivf_num_probes, ivf_initialized) "
            "VALUES ($1, $2, $3::index_type, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15) "
            "ON CONFLICT (name) DO NOTHING",
            pqxx::params{db_name, (int)header.dimensions,
                         (header.index_type == 1) ? "HNSW" : "IVF",
                         (long)header.max_capacity, (long)header.vector_count, (long)header.next_id,
                         (int)header.hnsw_M, (int)header.hnsw_ef_construction,
                         (int)header.hnsw_ef_search, (int)header.hnsw_max_level,
                         (int)header.hnsw_entry_point, (int)header.hnsw_graph_version,
                         (int)header.num_clusters, (int)header.num_probes,
                         header.is_initialized != 0}
        );
        tx.commit();
        std::cout << "[METADATA] Seeded: " << db_name << "\n";
    }
}

}
