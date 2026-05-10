#include <iostream>
#include <filesystem>
#include "redboxdb/engine.hpp"
#include "redboxdb/storage_manager.hpp"
#include <vector>
#include <cmath>
#include <fstream>
#include <queue>
#include <algorithm>
#include "redboxdb/cpu_features.hpp"
#include "redboxdb/distance.hpp"
#include <cstring>


namespace CoreEngine {

    RedBoxVector::RedBoxVector(std::string file_name, size_t dim, int capacity): dimension(dim), file_name(file_name), tombstone_file(file_name + ".del")
    {
        _manager = std::make_unique<StorageManager::Manager>(file_name, dim, capacity);
        load_tombstones();

        // Build ID→index from existing file contents
        int existing = static_cast<int>(_manager->get_count());
        for (int i = 0; i < existing; ++i) {
            auto [id, _] = _manager->get_vector_raw(i);
            if (!deleted_ids.count(id))
                id_to_index[id] = i;
        }

        use_avx2 = Platform::has_avx2();
        std::cout << "[DB] AVX2: " << (use_avx2 ? "enabled" : "disabled") << "\n";
    }

    // -----------------------------------------------------------------------
    void RedBoxVector::insert(uint64_t id, const std::vector<float>& vec) {
        if (deleted_ids.count(id)) {
            deleted_ids.erase(id);
            // The stale tombstone entry for this id will be cleaned up the
            // next time compact_tombstones() runs : no problem.
        }
        try {
            size_t new_index = _manager->get_count();
            _manager->add_vector(id, vec);
            id_to_index[id] = new_index;
        }
        catch (const std::exception& e) {
            std::cerr << "Insert Error: " << e.what() << "\n";
        }
    }

    uint64_t RedBoxVector::insert_auto(const std::vector<float>& vec) {
        uint64_t new_id = _manager->next_id();
        insert(new_id, vec);
        return new_id;
    }

    void RedBoxVector::saveToDisk([[maybe_unused]] const std::string& filename) {
        std::cout << "-> Persistence handled by StorageManager (Auto-Save active).\n";
    }

    void RedBoxVector::loadFromDisk(const std::string& filename) {
        std::cout << "-> Database attached to: " << filename << "\n";
        std::cout << "-> Current Record Count: " << _manager->get_count() << "\n";
    }

    // -----------------------------------------------------------------------
    int RedBoxVector::search(const std::vector<float>& query) {
        float min_dist = 1e9f;
        int   best_id = -1;
        int   count = static_cast<int>(_manager->get_count());

        for (int i = 0; i < count; ++i) {
            auto [id, vec_ptr] = _manager->get_vector_raw(i);
            if (deleted_ids.count(id)) continue;

            float dist = Distance::l2(vec_ptr, query.data(), dimension, use_avx2);
            if (dist < min_dist) {
                min_dist = dist;
                best_id = static_cast<int>(id);
            }
        }
        return best_id;
    }

    // -----------------------------------------------------------------------
    // Tombstone helpers
    // -----------------------------------------------------------------------
    void RedBoxVector::load_tombstones() {
        std::ifstream f(tombstone_file, std::ios::binary);
        if (!f.is_open()) return;

        uint64_t id;
        while (f.read(reinterpret_cast<char*>(&id), sizeof(id))) {
            deleted_ids.insert(id);
            ++tombstone_entries_on_disk;
        }
    }

    void RedBoxVector::append_tombstone(uint64_t id) {
        std::ofstream f(tombstone_file, std::ios::binary | std::ios::app);
        if (f.is_open()) {
            f.write(reinterpret_cast<const char*>(&id), sizeof(id));
            ++tombstone_entries_on_disk;
        }
    }

    // Rewrites the .del file so it contains exactly one entry per live
    // deleted ID — no duplicates, no stale re-inserted IDs.
    //
    // Called automatically from remove() when the file has accumulated
    // TOMBSTONE_COMPACT_SLACK extra entries beyond the live set size.
    void RedBoxVector::compact_tombstones() {
        // Truncate-and-rewrite (atomic-ish: write to .tmp, then rename)
        std::string tmp_file = tombstone_file + ".tmp";

        {
            std::ofstream f(tmp_file, std::ios::binary | std::ios::trunc);
            if (!f.is_open()) {
                std::cerr << "[DB] compact_tombstones: could not open " << tmp_file << "\n";
                return;
            }
            for (uint64_t id : deleted_ids) {
                f.write(reinterpret_cast<const char*>(&id), sizeof(id));
            }
        } // f is flushed & closed here

        // Replace old file
        if (std::filesystem::exists(tombstone_file)) {
            std::filesystem::remove(tombstone_file);
        }
        if (std::rename(tmp_file.c_str(), tombstone_file.c_str()) != 0) {
            std::cerr << "[DB] compact_tombstones: rename failed\n";
            return;
        }

        size_t old_count = tombstone_entries_on_disk;
        tombstone_entries_on_disk = deleted_ids.size();
        std::cout << "[DB] Tombstone compacted: " << old_count
            << " entries -> " << tombstone_entries_on_disk << "\n";
    }

    // -----------------------------------------------------------------------
    bool RedBoxVector::remove(uint64_t id) {
        if (deleted_ids.count(id)) return false;

        deleted_ids.insert(id);
        id_to_index.erase(id);
        append_tombstone(id);

        // Compact when the file has grown TOMBSTONE_COMPACT_SLACK entries past
        // the number of IDs that are actually still deleted.
        if (tombstone_entries_on_disk > deleted_ids.size() + TOMBSTONE_COMPACT_SLACK) {
            compact_tombstones();
        }

        return true;
    }

    // -----------------------------------------------------------------------
    std::vector<int> RedBoxVector::search_N(const std::vector<float>& query, int N) {
        std::priority_queue<std::pair<float, int>> pq;
        int count = static_cast<int>(_manager->get_count());

        for (int i = 0; i < count; ++i) {
            auto [id, vec_ptr] = _manager->get_vector_raw(i);
            if (deleted_ids.count(id)) continue;

            float dist = Distance::l2(vec_ptr, query.data(), dimension, use_avx2);

            if ((int)pq.size() < N) {
                pq.push({ dist, static_cast<int>(id) });
            }
            else if (dist < pq.top().first) {
                pq.pop();
                pq.push({ dist, static_cast<int>(id) });
            }
        }

        std::vector<int> result;
        result.reserve(pq.size());
        while (!pq.empty()) {
            result.push_back(pq.top().second);
            pq.pop();
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

    // -----------------------------------------------------------------------
    uint32_t RedBoxVector::get_dim() const {
        return static_cast<uint32_t>(dimension);
    }

    bool RedBoxVector::update(uint64_t id, const std::vector<float>& vec) {
        if (deleted_ids.count(id)) return false;

        auto it = id_to_index.find(id);
        if (it == id_to_index.end()) return false;

        auto record = _manager->get_vector_raw(static_cast<int>(it->second));
        float* dst = const_cast<float*>(record.second);
        std::memcpy(dst, vec.data(), dimension * sizeof(float));
        return true;
    }

} 


// ============================================================
// StorageManager — unchanged from original, kept in same TU
// ============================================================
namespace StorageManager {
        Manager::Manager(const std::string& db_file, size_t dimensions, int initial_capacity)
            : allocated_size(initial_capacity), filename(db_file),
            hFile(NULL), hMapFile(NULL), map_base(nullptr)
        {
        row_size_bytes = sizeof(uint64_t) + (dimensions * sizeof(float));

        hFile = CreateFileA(filename.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not open file");

        LARGE_INTEGER fileSize;
        GetFileSizeEx(hFile, &fileSize);
        size_t current_size = (size_t)fileSize.QuadPart;
        size_t required_size = sizeof(CoreEngine::SpecificMetadata) +
            (row_size_bytes * initial_capacity);

        if (current_size == 0) {
            LARGE_INTEGER distance;
            distance.QuadPart = required_size;
            if (!SetFilePointerEx(hFile, distance, NULL, FILE_BEGIN))
                throw std::runtime_error("Resize failed");
            if (!SetEndOfFile(hFile))
                throw std::runtime_error("SetEndOfFile failed");
        }

        hMapFile = CreateFileMappingA(hFile, NULL, PAGE_READWRITE, 0, 0, NULL);
        map_base = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);

        header = (CoreEngine::SpecificMetadata*)map_base;
        data_start = (char*)map_base + sizeof(CoreEngine::SpecificMetadata);

        if (current_size == 0) {
            header->vector_count = 0;
            header->max_capacity = initial_capacity;
            header->dimensions = dimensions;
            header->data_type_size = sizeof(float);
            header->next_id = 1;
        }
        else {
            if (header->dimensions != dimensions) {
                throw std::runtime_error("DB Dimension mismatch! File has " +
                    std::to_string(header->dimensions));
            }
        }
    }

    uint64_t Manager::next_id() { return header->next_id++; }

    Manager::~Manager() {
        if (map_base) { FlushViewOfFile(map_base, 0); UnmapViewOfFile(map_base); }
        if (hMapFile)  CloseHandle(hMapFile);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    }

    void Manager::add_vector(uint64_t id, const std::vector<float>& vec) {
        if (vec.size() != header->dimensions)
            throw std::invalid_argument("Vector dimension mismatch");
        if (header->vector_count >= header->max_capacity)
            throw std::runtime_error("Database full");

        size_t offset = header->vector_count * row_size_bytes;
        char* row_ptr = data_start + offset;

        *(uint64_t*)row_ptr = id;
        float* vec_ptr = (float*)(row_ptr + sizeof(uint64_t));
        std::memcpy(vec_ptr, vec.data(), header->dimensions * sizeof(float));

        header->vector_count++;
    }

    std::pair<uint64_t, const float*> Manager::get_vector_raw(int index) {
        if (index >= (int)header->vector_count)
            throw std::out_of_range("Index out of bounds");

        size_t      offset = index * row_size_bytes;
        char* row_ptr = data_start + offset;
        uint64_t    id = *(uint64_t*)row_ptr;
        const float* vec_ptr = (const float*)(row_ptr + sizeof(uint64_t));
        return { id, vec_ptr };
    }

    uint64_t Manager::get_count() const { return header->vector_count; }

} // namespace StorageManager