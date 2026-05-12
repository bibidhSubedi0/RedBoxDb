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

        // Build ID→index and deleted_flags from existing file contents
        int existing = static_cast<int>(_manager->get_count());
        deleted_flags.resize(existing, 0);
        for (int i = 0; i < existing; ++i) {
            uint64_t id = _manager->get_id(i);
            if (deleted_ids.count(id))
                deleted_flags[i] = 1;
            else
                id_to_index[id] = i;
        }

        use_avx2    = Platform::has_avx2();
        num_threads = std::max(1u, std::thread::hardware_concurrency());
        std::cout << "[DB] AVX2: " << (use_avx2 ? "enabled" : "disabled")
                  << " | Threads: " << num_threads << "\n";
    }

    // -----------------------------------------------------------------------
    void RedBoxVector::insert(uint64_t id, const std::vector<float>& vec) {
        if (deleted_ids.count(id)) {
            // Clear the old slot's flag — it stays in the mmap as a zombie,
            // but we don't want it showing up as deleted in scans anymore.
            auto old_it = id_to_index.find(id); // not present (erased on remove)
            // old slot flag: we don't have the index anymore, so we can't clear it.
            // It will be skipped correctly: the new append below gets flag=0,
            // and the old zombie slot has no entry in id_to_index so update()
            // won't touch it. The flag on the old slot stays 1 (correct — it IS
            // a dead row). The new row gets flag=0.
            (void)old_it;
            deleted_ids.erase(id);
        }
        try {
            size_t new_index = _manager->get_count();
            _manager->add_vector(id, vec);
            id_to_index[id] = new_index;
            deleted_flags.push_back(0); // new slot is live
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
        int count = static_cast<int>(_manager->get_count());
        if (count == 0) return -1;

        // Single-threaded for small DBs — thread overhead not worth it
        if (count < PARALLEL_THRESHOLD || num_threads == 1) {
            float min_dist = 1e9f;
            int   best_slot = -1;
            for (int i = 0; i < count; ++i) {
                if (deleted_flags[i]) continue;
                const float* vec_ptr = _manager->get_float_ptr(i);
                float dist = Distance::l2(vec_ptr, query.data(), dimension, use_avx2);
                if (dist < min_dist) { min_dist = dist; best_slot = i; }
            }
            if (best_slot == -1) return -1;
            return static_cast<int>(_manager->get_id(best_slot));
        }

        // Parallel scan — each thread scans its own slice, writes to its own slot
        // in these arrays (no false sharing between threads, each index is independent)
        struct ThreadResult { float min_dist; int best_slot; };
        std::vector<ThreadResult> results(num_threads, { 1e9f, -1 });

        auto worker = [&](size_t tid, int start, int end) {
            float local_min  = 1e9f;
            int   local_best = -1;
            for (int i = start; i < end; ++i) {
                if (deleted_flags[i]) continue;
                const float* vec_ptr = _manager->get_float_ptr(i);
                float dist = Distance::l2(vec_ptr, query.data(), dimension, use_avx2);
                if (dist < local_min) { local_min = dist; local_best = i; }
            }
            results[tid] = { local_min, local_best };
        };

        // Divide slots as evenly as possible across threads
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        int slice = count / (int)num_threads;
        for (size_t t = 0; t < num_threads; ++t) {
            int start = (int)t * slice;
            int end   = (t == num_threads - 1) ? count : start + slice;
            threads.emplace_back(worker, t, start, end);
        }
        for (auto& th : threads) th.join();

        // Reduce: find global winner across all thread results
        int   best_slot = -1;
        float best_dist = 1e9f;
        for (auto& r : results) {
            if (r.best_slot != -1 && r.min_dist < best_dist) {
                best_dist = r.min_dist;
                best_slot = r.best_slot;
            }
        }
        if (best_slot == -1) return -1;
        return static_cast<int>(_manager->get_id(best_slot));
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

        // Mark the slot as deleted in the hot-path array before erasing from index
        auto it = id_to_index.find(id);
        if (it != id_to_index.end()) {
            deleted_flags[it->second] = 1;
            id_to_index.erase(it);
        }

        deleted_ids.insert(id);
        append_tombstone(id);

        if (tombstone_entries_on_disk > deleted_ids.size() + TOMBSTONE_COMPACT_SLACK) {
            compact_tombstones();
        }

        return true;
    }

    // -----------------------------------------------------------------------
    std::vector<int> RedBoxVector::search_N(const std::vector<float>& query, int N) {
        using PQ = std::priority_queue<std::pair<float, int>>;
        int count = static_cast<int>(_manager->get_count());
        if (count == 0) return {};

        // Single-threaded for small DBs
        if (count < PARALLEL_THRESHOLD || num_threads == 1) {
            PQ pq;
            for (int i = 0; i < count; ++i) {
                if (deleted_flags[i]) continue;
                const float* vec_ptr = _manager->get_float_ptr(i);
                float dist = Distance::l2(vec_ptr, query.data(), dimension, use_avx2);
                if ((int)pq.size() < N)              pq.push({ dist, i });
                else if (dist < pq.top().first) { pq.pop(); pq.push({ dist, i }); }
            }
            std::vector<int> result;
            result.reserve(pq.size());
            while (!pq.empty()) {
                result.push_back(static_cast<int>(_manager->get_id(pq.top().second)));
                pq.pop();
            }
            std::reverse(result.begin(), result.end());
            return result;
        }

        // Each thread maintains its own local top-N priority queue
        std::vector<PQ> local_pqs(num_threads);

        auto worker = [&](size_t tid, int start, int end) {
            PQ& pq = local_pqs[tid];
            for (int i = start; i < end; ++i) {
                if (deleted_flags[i]) continue;
                const float* vec_ptr = _manager->get_float_ptr(i);
                float dist = Distance::l2(vec_ptr, query.data(), dimension, use_avx2);
                if ((int)pq.size() < N)              pq.push({ dist, i });
                else if (dist < pq.top().first) { pq.pop(); pq.push({ dist, i }); }
            }
        };

        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        int slice = count / (int)num_threads;
        for (size_t t = 0; t < num_threads; ++t) {
            int start = (int)t * slice;
            int end   = (t == num_threads - 1) ? count : start + slice;
            threads.emplace_back(worker, t, start, end);
        }
        for (auto& th : threads) th.join();

        // Merge all local top-N queues into one global top-N
        PQ global_pq;
        for (auto& pq : local_pqs) {
            while (!pq.empty()) {
                auto [dist, slot] = pq.top(); pq.pop();
                if ((int)global_pq.size() < N)              global_pq.push({ dist, slot });
                else if (dist < global_pq.top().first) { global_pq.pop(); global_pq.push({ dist, slot }); }
            }
        }

        std::vector<int> result;
        result.reserve(global_pq.size());
        while (!global_pq.empty()) {
            result.push_back(static_cast<int>(_manager->get_id(global_pq.top().second)));
            global_pq.pop();
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

        float* dst = _manager->get_float_ptr_mut(static_cast<int>(it->second));
        std::memcpy(dst, vec.data(), dimension * sizeof(float));
        return true;
    }

} 


// ============================================================
// StorageManager — Columnar layout (version 1)
// ============================================================
namespace StorageManager {

    // Helper: set up id_block and float_block pointers from map_base
    static void setup_pointers(void* map_base, uint64_t capacity,
                                CoreEngine::SpecificMetadata*& header,
                                uint64_t*& id_block, float*& float_block)
    {
        header      = (CoreEngine::SpecificMetadata*)map_base;
        id_block    = (uint64_t*)((char*)map_base + sizeof(CoreEngine::SpecificMetadata));
        float_block = (float*)((char*)id_block + capacity * sizeof(uint64_t));
    }

    Manager::Manager(const std::string& db_file, size_t dimensions, int initial_capacity)
        : allocated_size(initial_capacity), filename(db_file),
          hFile(NULL), hMapFile(NULL), map_base(nullptr),
          header(nullptr), id_block(nullptr), float_block(nullptr)
    {
        hFile = CreateFileA(filename.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not open file");

        LARGE_INTEGER fileSize;
        GetFileSizeEx(hFile, &fileSize);
        size_t current_size = (size_t)fileSize.QuadPart;

        // Columnar layout size:
        //   header + (capacity * 8) + (capacity * dim * 4)
        size_t id_block_bytes    = (size_t)initial_capacity * sizeof(uint64_t);
        size_t float_block_bytes = (size_t)initial_capacity * dimensions * sizeof(float);
        size_t required_size     = sizeof(CoreEngine::SpecificMetadata)
                                 + id_block_bytes + float_block_bytes;

        if (current_size == 0) {
            LARGE_INTEGER distance;
            distance.QuadPart = (LONGLONG)required_size;
            if (!SetFilePointerEx(hFile, distance, NULL, FILE_BEGIN))
                throw std::runtime_error("Resize failed");
            if (!SetEndOfFile(hFile))
                throw std::runtime_error("SetEndOfFile failed");
        }

        hMapFile = CreateFileMappingA(hFile, NULL, PAGE_READWRITE, 0, 0, NULL);
        if (!hMapFile) throw std::runtime_error("CreateFileMapping failed");
        map_base = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!map_base) throw std::runtime_error("MapViewOfFile failed");

        setup_pointers(map_base, (uint64_t)initial_capacity, header, id_block, float_block);

        if (current_size == 0) {
            header->vector_count   = 0;
            header->max_capacity   = initial_capacity;
            header->dimensions     = dimensions;
            header->data_type_size = sizeof(float);
            header->next_id        = 1;
            header->version        = CoreEngine::SpecificMetadata::CURRENT_VERSION;
        }
        else {
            if (header->version != CoreEngine::SpecificMetadata::CURRENT_VERSION) {
                UnmapViewOfFile(map_base); CloseHandle(hMapFile); CloseHandle(hFile);
                throw std::runtime_error(
                    "Legacy database layout detected (version " +
                    std::to_string(header->version) +
                    "). Please recreate the database.");
            }
            if (header->dimensions != dimensions) {
                UnmapViewOfFile(map_base); CloseHandle(hMapFile); CloseHandle(hFile);
                throw std::runtime_error("DB dimension mismatch! File has " +
                    std::to_string(header->dimensions));
            }
        }
    }

    uint64_t Manager::next_id() { return header->next_id++; }
    // SO अहीले लाइ, we are just incremeting the id
    // but what happens if a vector is deleted? नसोध मलाइ
    // so definately need a way to get id's any other way then incrementing from a base value!
    // maybe look for deleted id's first? idk फ्युचर मी पर्रोबल्म

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

        size_t slot = header->vector_count;
        id_block[slot] = id;
        float* dst = float_block + slot * header->dimensions;
        std::memcpy(dst, vec.data(), header->dimensions * sizeof(float));
        header->vector_count++;
    }

    const float* Manager::get_float_ptr(int index) const {
        if (index >= (int)header->vector_count)
            throw std::out_of_range("Index out of bounds");
        return float_block + (size_t)index * header->dimensions;
    }

    float* Manager::get_float_ptr_mut(int index) {
        if (index >= (int)header->vector_count)
            throw std::out_of_range("Index out of bounds");
        return float_block + (size_t)index * header->dimensions;
    }

    uint64_t Manager::get_id(int index) const {
        if (index >= (int)header->vector_count)
            throw std::out_of_range("Index out of bounds");
        return id_block[index];
    }

    uint64_t Manager::get_count() const { return header->vector_count; }

} // namespace StorageManager