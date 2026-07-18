#pragma once
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <algorithm>
#include <shared_mutex>
#include <random>
#include "redboxdb/storage_manager.hpp"
#include "redboxdb/SpecificMetadata.hpp"

namespace CoreEngine {

    class RedBoxVector {
    private:
        static constexpr int     default_capacity      = 1000;
        static constexpr size_t  TOMBSTONE_COMPACT_SLACK = 64;
        static constexpr int     PARALLEL_THRESHOLD    = 50000;
        static constexpr uint16_t  DEFAULT_CLUSTERS      = 1000;
        static constexpr uint8_t DEFAULT_PROBES        = 10;
        static constexpr uint64_t KMEANS_INIT_THRESHOLD = 10000;

        size_t dimension;
        std::unique_ptr<StorageManager::Manager> _manager;
        std::string file_name;

        // Soft deletion
        std::string tombstone_file;
        std::unordered_set<uint64_t> deleted_ids;
        size_t tombstone_entries_on_disk = 0;

        std::vector<uint8_t> deleted_flags;

        std::unordered_map<uint64_t, size_t> id_to_index;

        // IVF in-memory index
        std::vector<std::vector<int>> cluster_index;

        // HNSW RNG
        std::mt19937 hnsw_rng;

        bool   use_avx2;
        size_t num_threads;

        mutable std::shared_mutex rw_mutex;

    public:
        // IVF constructor
        RedBoxVector(std::string file_name, size_t dim,
                     int     capacity   = default_capacity,
                     uint16_t k         = DEFAULT_CLUSTERS,
                     uint8_t num_probes = DEFAULT_PROBES);

        // HNSW constructor
        RedBoxVector(std::string file_name, size_t dim,
                     int capacity,
                     uint8_t hnsw_M,
                     uint16_t hnsw_ef_construction);

        void     insert(uint64_t id, const std::vector<float>& vec);
        uint64_t insert_auto(const std::vector<float>& vec);
        int      search(const std::vector<float>& query);
        std::vector<int> search_N(const std::vector<float>& query, int N);
        bool     remove(uint64_t id);
        uint32_t get_dim() const;
        bool     update(uint64_t id, const std::vector<float>& vec);
        void     set_num_probes(uint8_t p);
        void     set_hnsw_ef_search(uint16_t ef);

        // Tombstone helpers
        void load_tombstones();
        void append_tombstone(uint64_t id);
        void compact_tombstones();

        // Legacy / status
        void saveToDisk(const std::string& filename);
        void loadFromDisk(const std::string& filename);

        IndexType get_index_type() const { return _manager->get_index_type(); }
    };

}