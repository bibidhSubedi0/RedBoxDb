#pragma once
#include <vector>
#include <unordered_set>
#include "redboxdb/storage_manager.hpp"
#include <unordered_map>

namespace CoreEngine {

    class RedBoxVector {
    private:
        static constexpr int    default_capacity = 1000;
        static constexpr size_t TOMBSTONE_COMPACT_SLACK = 64;

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

        bool use_avx2;

    public:
        RedBoxVector(std::string file_name, size_t dim, int capacity = default_capacity);

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
        // Rewrites the .del file to contain only the current live deleted_ids set.
        void compact_tombstones();

        // Legacy / status
        void saveToDisk(const std::string& filename);
        void loadFromDisk(const std::string& filename);
    };

}