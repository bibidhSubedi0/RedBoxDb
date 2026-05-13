#pragma once
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <algorithm>
#include "redboxdb/storage_manager.hpp"

namespace CoreEngine {

    class RedBoxVector {
    private:
        static constexpr int     default_capacity      = 1000;
        static constexpr size_t  TOMBSTONE_COMPACT_SLACK = 64;
        static constexpr int     PARALLEL_THRESHOLD    = 50000;
        static constexpr uint8_t DEFAULT_CLUSTERS      = 25;
        static constexpr uint8_t DEFAULT_PROBES        = 1;

        size_t dimension;
        std::unique_ptr<StorageManager::Manager> _manager;
        std::string file_name;

        // Soft deletion
        std::string tombstone_file;
        std::unordered_set<uint64_t> deleted_ids;
        size_t tombstone_entries_on_disk = 0; // tracks raw entry count in the file

        // Hot-path parallel array: deleted_flags[slot] == 1 means that mmap slot
        // is logically deleted. Indexed by slot position, not by ID.
        // Replaces the per-row deleted_ids.count() hash lookup in the search loops.
        std::vector<uint8_t> deleted_flags;

        std::unordered_map<uint64_t, size_t> id_to_index;

        // In-memory inverted index: cluster_index[c] = list of slot numbers in cluster c.
        // Built on open from cluster_block, updated on every insert.
        // Lets search() jump directly to ~1000 candidates without scanning 100k slots.
        std::vector<std::vector<int>> cluster_index;

        bool   use_avx2;
        size_t num_threads;

    public:
        RedBoxVector(std::string file_name, size_t dim,
                     int     capacity   = default_capacity,
                     uint8_t k          = DEFAULT_CLUSTERS,
                     uint8_t num_probes = DEFAULT_PROBES);

        void     insert(uint64_t id, const std::vector<float>& vec);
        uint64_t insert_auto(const std::vector<float>& vec);
        int      search(const std::vector<float>& query);
        std::vector<int> search_N(const std::vector<float>& query, int N);
        bool     remove(uint64_t id);
        uint32_t get_dim() const;
        bool     update(uint64_t id, const std::vector<float>& vec);

        // Tombstone helpers
        void load_tombstones();
        void append_tombstone(uint64_t id);
        void compact_tombstones();

        // Legacy / status
        void saveToDisk(const std::string& filename);
        void loadFromDisk(const std::string& filename);
    };

}