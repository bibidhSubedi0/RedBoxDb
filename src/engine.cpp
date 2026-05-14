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
#include "redboxdb/cluster_manager.hpp"
#include <cstring>


namespace CoreEngine {

    RedBoxVector::RedBoxVector(std::string file_name, size_t dim, int capacity, uint8_t k, uint8_t num_probes) : dimension(dim), file_name(file_name), tombstone_file(file_name + ".del")
    {
        _manager = std::make_unique<StorageManager::Manager>(file_name, dim, capacity, k, num_probes);
        load_tombstones();

        use_avx2    = Platform::has_avx2();
        num_threads = std::max(1u, std::thread::hardware_concurrency());

        // Build ID->index, deleted_flags, and cluster_index from existing file contents
        int existing = static_cast<int>(_manager->get_count());
        deleted_flags.resize(existing, 0);
        cluster_index.resize(k);

        for (int i = 0; i < existing; ++i) {
            uint64_t id = _manager->get_id(i);
            if (deleted_ids.count(id)) {
                deleted_flags[i] = 1;
            } else {
                id_to_index[id] = i;
                // Rebuild in-memory cluster index from mmap cluster_block
                if (_manager->is_cluster_initialized()) {
                    uint16_t c = _manager->get_cluster(i);
                    if (c < k) cluster_index[c].push_back(i);
                }
            }
        }

        if (_manager->is_cluster_initialized()) {
            size_t max_cluster = 0;
            for (auto& v : cluster_index) max_cluster = std::max(max_cluster, v.size());
            std::cout << "[DB] Max cluster size: " << max_cluster << "\n";
        }

        std::cout << "[DB] AVX2: " << (use_avx2 ? "enabled" : "disabled")
                  << " | Threads: " << num_threads
                  << " | Clusters: " << static_cast<int>(_manager->get_num_clusters())
                  << " | Probes: "   << static_cast<int>(_manager->get_num_probes()) << "\n";
    }

    // -----------------------------------------------------------------------
    void RedBoxVector::insert(uint64_t id, const std::vector<float>& vec) {
        if (deleted_ids.count(id)) {
            deleted_ids.erase(id);
        }
        try {
            uint8_t  k    = _manager->get_num_clusters();
            size_t   slot = _manager->get_count();
            uint16_t c    = 0;

            if (!_manager->is_cluster_initialized()) {
                // Pre-initialization: assign to cluster 0
                c = 0;
                _manager->add_vector(id, vec, c);

                if ((uint64_t)(slot + 1) >= KMEANS_INIT_THRESHOLD) {
                    // Hit K vectors — run K-Means++ and assign all slots
                    ClusterManager::kmeans_plus_plus_init(
                        _manager->get_centroid_block(),
                        _manager->get_cluster_count_block(),
                        _manager->get_cluster_block(),
                        _manager->get_float_ptr(0),
                        k, slot+1, dimension, use_avx2);
                    _manager->set_cluster_initialized();

                    // Rebuild cluster_index from scratch now that assignments are real
                    for (auto& v : cluster_index) v.clear();
                    for (int i = 0; i <= (int)slot; ++i) {
                        if (!deleted_flags[i]) {
                            uint16_t ci = _manager->get_cluster(i);
                            if (ci < k) cluster_index[ci].push_back(i);
                        }
                    }

                    std::cout << "[DB] K-Means++ initialized with K=" << (int)k << "\n";
                }
            } else {
                // Normal path: find nearest centroid, assign, update centroid online
                c = ClusterManager::find_nearest_centroid(
                    vec.data(),
                    _manager->get_centroid_block(),
                    k, dimension, use_avx2);
                _manager->add_vector(id, vec, c);
                ClusterManager::update_centroid(
                    _manager->get_centroid_block(),
                    _manager->get_cluster_count_block(),
                    c, vec.data(), dimension);

                // Update in-memory inverted index
                cluster_index[c].push_back(static_cast<int>(slot));
            }

            id_to_index[id] = slot;
            deleted_flags.push_back(0);
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

        uint8_t k           = _manager->get_num_clusters();
        uint8_t num_probes  = _manager->get_num_probes();
        bool    initialized = _manager->is_cluster_initialized();

        // --- Build candidate list ---
        // If initialized: O(K) centroid scan + O(cluster_size) lookup via cluster_index
        // If not: full slot list (pre-init fallback)
        std::vector<int> candidates;

        if (initialized) {
            const float* centroid_block = _manager->get_centroid_block();

            // Score all K centroids, pick nearest num_probes
            std::vector<std::pair<float, uint16_t>> centroid_dists(k);
            for (uint8_t c = 0; c < k; ++c) {
                float d = Distance::l2(query.data(), centroid_block + (size_t)c * dimension,
                                       dimension, use_avx2);
                centroid_dists[c] = { d, c };
            }
            std::partial_sort(centroid_dists.begin(),
                              centroid_dists.begin() + num_probes,
                              centroid_dists.end());

            // Collect candidates directly from inverted index — no full scan
            size_t reserve_size = 0;
            for (int p = 0; p < num_probes; ++p)
                reserve_size += cluster_index[centroid_dists[p].second].size();
            candidates.reserve(reserve_size);

            for (int p = 0; p < num_probes; ++p) {
                uint16_t c = centroid_dists[p].second;
                for (int slot : cluster_index[c])
                    if (!deleted_flags[slot]) candidates.push_back(slot);
            }
        } else {
            candidates.reserve(count);
            for (int i = 0; i < count; ++i)
                if (!deleted_flags[i]) candidates.push_back(i);
        }

        if (candidates.empty()) return -1;

        // --- Distance scan over candidates only ---
        float min_dist  = 1e9f;
        int   best_slot = -1;
        for (int i : candidates) {
            const float* vec_ptr = _manager->get_float_ptr(i);
            float dist = Distance::l2(vec_ptr, query.data(), dimension, use_avx2);
            if (dist < min_dist) { min_dist = dist; best_slot = i; }
        }

        if (best_slot == -1) return -1;
        return static_cast<int>(_manager->get_id(best_slot));
    }

    // -----------------------------------------------------------------------
    std::vector<int> RedBoxVector::search_N(const std::vector<float>& query, int N) {
        using PQ = std::priority_queue<std::pair<float, int>>;
        int count = static_cast<int>(_manager->get_count());
        if (count == 0) return {};

        uint8_t k           = _manager->get_num_clusters();
        uint8_t num_probes  = _manager->get_num_probes();
        bool    initialized = _manager->is_cluster_initialized();

        // --- Build candidate list ---
        std::vector<int> candidates;

        if (initialized) {
            const float* centroid_block = _manager->get_centroid_block();

            std::vector<std::pair<float, uint16_t>> centroid_dists(k);
            for (uint8_t c = 0; c < k; ++c) {
                float d = Distance::l2(query.data(), centroid_block + (size_t)c * dimension,
                                       dimension, use_avx2);
                centroid_dists[c] = { d, c };
            }
            std::partial_sort(centroid_dists.begin(),
                              centroid_dists.begin() + num_probes,
                              centroid_dists.end());

            size_t reserve_size = 0;
            for (int p = 0; p < num_probes; ++p)
                reserve_size += cluster_index[centroid_dists[p].second].size();
            candidates.reserve(reserve_size);

            for (int p = 0; p < num_probes; ++p) {
                uint16_t c = centroid_dists[p].second;
                for (int slot : cluster_index[c])
                    if (!deleted_flags[slot]) candidates.push_back(slot);
            }
        } else {
            candidates.reserve(count);
            for (int i = 0; i < count; ++i)
                if (!deleted_flags[i]) candidates.push_back(i);
        }

        if (candidates.empty()) return {};

        // --- Distance scan over candidates only ---
        PQ pq;
        for (int i : candidates) {
            const float* vec_ptr = _manager->get_float_ptr(i);
            float dist = Distance::l2(vec_ptr, query.data(), dimension, use_avx2);
            if ((int)pq.size() < N)                    pq.push({ dist, i });
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

    void RedBoxVector::compact_tombstones() {
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
        }

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

        auto it = id_to_index.find(id);
        if (it != id_to_index.end()) {
            deleted_flags[it->second] = 1;
            // Note: we don't remove from cluster_index — deleted_flags check handles it.
            // cluster_index entries for deleted slots are simply skipped during search.
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
// StorageManager — Clustered columnar layout (version 2)
// ============================================================
namespace StorageManager {

    static void setup_pointers(
        void*     map_base,
        uint64_t  capacity,
        uint8_t   k,
        uint64_t  dim,
        CoreEngine::SpecificMetadata*& header,
        float*&    centroid_block,
        uint64_t*& cluster_count_block,
        uint16_t*& cluster_block,
        uint64_t*& id_block,
        float*&    float_block)
    {
        header              = (CoreEngine::SpecificMetadata*)map_base;
        centroid_block      = (float*)((char*)map_base + sizeof(CoreEngine::SpecificMetadata));
        cluster_count_block = (uint64_t*)(centroid_block + (size_t)k * dim);
        cluster_block       = (uint16_t*)(cluster_count_block + k);
        id_block            = (uint64_t*)(cluster_block + capacity);
        float_block         = (float*)(id_block + capacity);
    }

    Manager::Manager(const std::string& db_file, uint64_t dimensions,
                     int initial_capacity, uint8_t num_clusters, uint8_t num_probes)
        : allocated_size(initial_capacity), filename(db_file),
          hFile(NULL), hMapFile(NULL), map_base(nullptr),
          header(nullptr), centroid_block(nullptr), cluster_count_block(nullptr),
          cluster_block(nullptr), id_block(nullptr), float_block(nullptr)
    {
        hFile = CreateFileA(filename.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not open file");

        LARGE_INTEGER fileSize;
        GetFileSizeEx(hFile, &fileSize);
        size_t current_size = (size_t)fileSize.QuadPart;

        size_t centroid_bytes      = (size_t)num_clusters * dimensions * sizeof(float);
        size_t cluster_count_bytes = (size_t)num_clusters * sizeof(uint64_t);
        size_t cluster_block_bytes = (size_t)initial_capacity * sizeof(uint16_t);
        size_t id_block_bytes      = (size_t)initial_capacity * sizeof(uint64_t);
        size_t float_block_bytes   = (size_t)initial_capacity * dimensions * sizeof(float);
        size_t required_size       = sizeof(CoreEngine::SpecificMetadata)
                                   + centroid_bytes + cluster_count_bytes
                                   + cluster_block_bytes + id_block_bytes + float_block_bytes;

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

        setup_pointers(map_base, (uint64_t)initial_capacity, num_clusters, dimensions,
                       header, centroid_block, cluster_count_block,
                       cluster_block, id_block, float_block);

        if (current_size == 0) {
            header->vector_count   = 0;
            header->max_capacity   = initial_capacity;
            header->dimensions     = dimensions;
            header->data_type_size = sizeof(float);
            header->next_id        = 1;
            header->version        = CoreEngine::SpecificMetadata::CURRENT_VERSION;
            header->num_clusters   = num_clusters;
            header->is_initialized = 0;
            header->num_probes     = num_probes;
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

    Manager::~Manager() {
        if (map_base) { FlushViewOfFile(map_base, 0); UnmapViewOfFile(map_base); }
        if (hMapFile)  CloseHandle(hMapFile);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    }

    void Manager::add_vector(uint64_t id, const std::vector<float>& vec, uint16_t cluster) {
        if (vec.size() != header->dimensions)
            throw std::invalid_argument("Vector dimension mismatch");
        if (header->vector_count >= header->max_capacity)
            throw std::runtime_error("Database full");

        size_t slot = header->vector_count;
        cluster_block[slot] = cluster;
        id_block[slot]      = id;
        float* dst = float_block + slot * header->dimensions;
        std::memcpy(dst, vec.data(), header->dimensions * sizeof(float));
        header->vector_count++;
    }

    const float* Manager::get_float_ptr(int index) const {
        if (index >= (int)header->vector_count) throw std::out_of_range("Index out of bounds");
        return float_block + (size_t)index * header->dimensions;
    }

    float* Manager::get_float_ptr_mut(int index) {
        if (index >= (int)header->vector_count) throw std::out_of_range("Index out of bounds");
        return float_block + (size_t)index * header->dimensions;
    }

    uint64_t Manager::get_id(int index) const {
        if (index >= (int)header->vector_count) throw std::out_of_range("Index out of bounds");
        return id_block[index];
    }

    uint16_t Manager::get_cluster(int index) const {
        if (index >= (int)header->vector_count) throw std::out_of_range("Index out of bounds");
        return cluster_block[index];
    }

    void Manager::set_cluster(int index, uint16_t c) {
        if (index >= (int)header->vector_count) throw std::out_of_range("Index out of bounds");
        cluster_block[index] = c;
    }

    uint64_t Manager::get_count() const { return header->vector_count; }

} // namespace StorageManager