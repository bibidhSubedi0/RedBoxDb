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
#include "redboxdb/hnsw_manager.hpp"
#include "redboxdb/logger.hpp"
#include <cstring>


namespace CoreEngine {

    RedBoxVector::RedBoxVector(std::string file_name, size_t dim, int capacity, uint16_t k, uint8_t num_probes) : dimension(dim), file_name(file_name), tombstone_file(file_name + ".del")
    {
        _manager = std::make_unique<StorageManager::Manager>(file_name, dim, capacity, k, num_probes);
        load_tombstones();

        use_avx2    = Platform::has_avx2();
        num_threads = std::max(1u, std::thread::hardware_concurrency());

        int existing = static_cast<int>(_manager->get_count());
        deleted_flags.resize(existing, 0);
        cluster_index.resize(k);

        for (int i = 0; i < existing; ++i) {
            uint64_t id = _manager->get_id(i);
            if (deleted_ids.count(id)) {
                deleted_flags[i] = 1;
            } else {
                id_to_index[id] = i;
                if (_manager->is_cluster_initialized()) {
                    uint16_t c = _manager->get_cluster(i);
                    if (c < k) cluster_index[c].push_back(i);
                }
            }
        }

        if (_manager->is_cluster_initialized()) {
            size_t max_cluster = 0;
            for (auto& v : cluster_index) max_cluster = std::max(max_cluster, v.size());
            Log::info("Max cluster size: " + std::to_string(max_cluster));
        }

        Log::info("AVX2: " + std::string(use_avx2 ? "enabled" : "disabled")
                  + " | Threads: " + std::to_string(num_threads)
                  + " | Clusters: " + std::to_string(static_cast<int>(_manager->get_num_clusters()))
                  + " | Probes: "   + std::to_string(static_cast<int>(_manager->get_num_probes())));
    }

    RedBoxVector::RedBoxVector(std::string file_name, size_t dim, int capacity,
                               uint8_t hnsw_M, uint16_t hnsw_ef_construction)
        : dimension(dim), file_name(file_name), tombstone_file(file_name + ".del"),
          hnsw_rng(std::random_device{}())
    {
        _manager = std::make_unique<StorageManager::Manager>(
            file_name, dim, capacity, 100, 1,
            IndexType::HNSW, hnsw_M, hnsw_ef_construction);
        load_tombstones();

        use_avx2    = Platform::has_avx2();
        num_threads = std::max(1u, std::thread::hardware_concurrency());

        int existing = static_cast<int>(_manager->get_count());
        deleted_flags.resize(existing, 0);

        for (int i = 0; i < existing; ++i) {
            uint64_t id = _manager->get_id(i);
            if (deleted_ids.count(id)) {
                deleted_flags[i] = 1;
            } else {
                id_to_index[id] = i;
            }
        }

        Log::info("HNSW initialized | AVX2: " + std::string(use_avx2 ? "enabled" : "disabled")
                  + " | M=" + std::to_string((int)hnsw_M)
                  + " | ef_construction=" + std::to_string((int)hnsw_ef_construction)
                  + " | ef_search=" + std::to_string((int)_manager->get_hnsw_ef_search())
                  + " | Nodes: " + std::to_string(existing));
    }

    // -----------------------------------------------------------------------
    void RedBoxVector::insert(uint64_t id, const std::vector<float>& vec) {
        std::unique_lock<std::shared_mutex> lk(rw_mutex);

        bool is_hnsw = (_manager->get_index_type() == IndexType::HNSW);

        // Re-insert after delete
        if (deleted_ids.count(id)) {
            deleted_ids.erase(id);

            int old_slot = -1;
            int total = static_cast<int>(_manager->get_count());
            for (int i = 0; i < total; ++i) {
                if (deleted_flags[i] && _manager->get_id(i) == id) {
                    old_slot = i;
                    break;
                }
            }

            if (old_slot != -1) {
                float* dst = _manager->get_float_ptr_mut(old_slot);
                std::memcpy(dst, vec.data(), dimension * sizeof(float));

                if (!is_hnsw) {
                    uint16_t k = _manager->get_num_clusters();
                    uint16_t c = 0;
                    if (_manager->is_cluster_initialized()) {
                        c = ClusterManager::find_nearest_centroid(
                            vec.data(), _manager->get_centroid_block(),
                            k, dimension, use_avx2);
                        ClusterManager::update_centroid(
                            _manager->get_centroid_block(),
                            _manager->get_cluster_count_block(),
                            c, vec.data(), dimension);
                        cluster_index[c].push_back(old_slot);
                    }
                    _manager->set_cluster(old_slot, c);
                }
                // HNSW: re-insert into graph
                else {
                    HnswManager::hnsw_insert(
                        static_cast<uint32_t>(old_slot), vec.data(),
                        _manager->get_header(), _manager->get_float_ptr_mut(0),
                        _manager->get_hnsw_edge_block(), _manager->get_hnsw_level_block(),
                        dimension, use_avx2, deleted_flags.data(), hnsw_rng,
                        hnsw_insert_visited_buf, hnsw_insert_visit_gen, hnsw_insert_nb_cands);
                }

                deleted_flags[old_slot] = 0;
                id_to_index[id] = old_slot;
                return;
            }
        }

        // Fresh insert
        try {
            size_t slot = _manager->get_count();
            if (slot >= _manager->get_header()->max_capacity) {
                return;
            }
            deleted_flags.push_back(0);

            if (!is_hnsw) {
                uint16_t k = _manager->get_num_clusters();
                uint16_t c = 0;

                if (!_manager->is_cluster_initialized()) {
                    c = 0;
                    _manager->add_vector(id, vec, c);

                    if ((uint64_t)(slot + 1) >= KMEANS_INIT_THRESHOLD) {
                        ClusterManager::kmeans_plus_plus_init(
                            _manager->get_centroid_block(),
                            _manager->get_cluster_count_block(),
                            _manager->get_cluster_block(),
                            _manager->get_float_ptr(0),
                            k, slot+1, dimension, use_avx2);
                        _manager->set_cluster_initialized();

                        for (auto& v : cluster_index) v.clear();
                        for (int i = 0; i <= (int)slot; ++i) {
                            if (!deleted_flags[i]) {
                                uint16_t ci = _manager->get_cluster(i);
                                if (ci < k) cluster_index[ci].push_back(i);
                            }
                        }

                        Log::info("K-Means++ initialized with K=" + std::to_string((int)k));
                    }
                } else {
                    c = ClusterManager::find_nearest_centroid(
                        vec.data(),
                        _manager->get_centroid_block(),
                        k, dimension, use_avx2);
                    _manager->add_vector(id, vec, c);
                    ClusterManager::update_centroid(
                        _manager->get_centroid_block(),
                        _manager->get_cluster_count_block(),
                        c, vec.data(), dimension);

                    cluster_index[c].push_back(static_cast<int>(slot));
                }
            } else {
                // HNSW insert
                _manager->add_vector(id, vec, 0);
                HnswManager::hnsw_insert(
                    static_cast<uint32_t>(slot), vec.data(),
                    _manager->get_header(), _manager->get_float_ptr_mut(0),
                    _manager->get_hnsw_edge_block(), _manager->get_hnsw_level_block(),
                    dimension, use_avx2, deleted_flags.data(), hnsw_rng,
                    hnsw_insert_visited_buf, hnsw_insert_visit_gen, hnsw_insert_nb_cands);
            }

            id_to_index[id] = slot;
        }
        catch (const std::exception& e) {
            Log::error("Insert failed: " + std::string(e.what()));
        }
    }

    uint64_t RedBoxVector::insert_auto(const std::vector<float>& vec) {
        // next_id() mutates header->next_id. insert() acquires the write lock too.
        // To avoid a deadlock we get the id under a brief lock, then call insert()
        // which will acquire the lock itself.
        uint64_t new_id;
        {
            std::unique_lock<std::shared_mutex> lk(rw_mutex);
            new_id = _manager->next_id();
        }
        insert(new_id, vec);
        return new_id;
    }

    void RedBoxVector::saveToDisk([[maybe_unused]] const std::string& filename) {
        Log::info("Persistence handled by StorageManager (Auto-Save active).");
    }

    void RedBoxVector::loadFromDisk(const std::string& filename) {
        Log::info("Database attached to: " + filename);
        Log::info("Current Record Count: " + std::to_string(_manager->get_count()));
    }

    // -----------------------------------------------------------------------
    int RedBoxVector::search(const std::vector<float>& query) {
        int count = static_cast<int>(_manager->get_count());
        if (count == 0) return -1;

        if (_manager->get_index_type() == IndexType::HNSW) {
            thread_local std::vector<uint8_t> hnsw_visited_buf;
            thread_local uint32_t hnsw_visit_gen = 0;
            uint32_t best_slot = HnswManager::hnsw_search_1(
                query.data(), _manager->get_header(),
                _manager->get_float_ptr(0), _manager->get_hnsw_edge_block(),
                dimension, use_avx2, deleted_flags.data(),
                hnsw_visited_buf, hnsw_visit_gen, 8);
            if (best_slot == HnswManager::EMPTY) return -1;
            return static_cast<int>(_manager->get_id(best_slot));
        }

        // IVF path
        uint16_t k         = _manager->get_num_clusters();
        uint8_t num_probes = _manager->get_num_probes();
        bool initialized   = _manager->is_cluster_initialized();
        const float* float_block_snap = _manager->get_float_ptr(0);

        // Thread-local buffers: no heap alloc per query
        thread_local std::vector<std::pair<float, uint16_t>> centroid_dists;
        thread_local std::vector<int> candidates;
        candidates.clear();

        if (initialized) {
            const float* centroid_block = _manager->get_centroid_block();

            if (num_probes == 1) {
                // Fast path: single linear scan for minimum, no sort needed
                float    best_d = std::numeric_limits<float>::max();
                uint16_t best_c = 0;
                for (uint16_t c = 0; c < k; ++c) {
                    float d = Distance::l2(query.data(), centroid_block + (size_t)c * dimension,
                                           dimension, use_avx2);
                    if (d < best_d) { best_d = d; best_c = c; }
                }
                for (int slot : cluster_index[best_c])
                    if (!deleted_flags[slot]) candidates.push_back(slot);
            } else {
                centroid_dists.resize(k);
                for (uint16_t c = 0; c < k; ++c) {
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
            }
        } else {
            candidates.reserve(count);
            for (int i = 0; i < count; ++i)
                if (!deleted_flags[i]) candidates.push_back(i);
        }

        if (candidates.empty()) return -1;

        float min_dist  = std::numeric_limits<float>::max();
        int   best_slot = -1;
        for (int slot : candidates) {
            const float* vec_ptr = float_block_snap + (size_t)slot * dimension;
            float dist = Distance::l2(vec_ptr, query.data(), dimension, use_avx2);
            if (dist < min_dist) { min_dist = dist; best_slot = slot; }
        }

        if (best_slot == -1) return -1;
        return static_cast<int>(_manager->get_id(best_slot));
    }

    // -----------------------------------------------------------------------
    std::vector<int> RedBoxVector::search_N(const std::vector<float>& query, int N) {
        using PQ = std::priority_queue<std::pair<float, int>>;

        int count = static_cast<int>(_manager->get_count());
        if (count == 0) return {};

        if (_manager->get_index_type() == IndexType::HNSW) {
            thread_local std::vector<uint8_t> hnsw_visited_buf;
            thread_local uint32_t hnsw_visit_gen = 0;
            std::vector<std::pair<float, uint32_t>> hnsw_results;
            HnswManager::hnsw_search(
                query.data(), N, _manager->get_header(),
                _manager->get_float_ptr(0), _manager->get_hnsw_edge_block(),
                dimension, use_avx2, deleted_flags.data(), hnsw_results,
                hnsw_visited_buf, hnsw_visit_gen);

            std::vector<int> result;
            int limit = std::min(N, (int)hnsw_results.size());
            result.reserve(limit);
            for (int i = 0; i < limit; ++i) {
                result.push_back(static_cast<int>(_manager->get_id(hnsw_results[i].second)));
            }
            return result;
        }

        // IVF path
        uint16_t k         = _manager->get_num_clusters();
        uint8_t num_probes = _manager->get_num_probes();
        bool initialized   = _manager->is_cluster_initialized();
        const float* float_block_snap = _manager->get_float_ptr(0);

        thread_local std::vector<std::pair<float, uint16_t>> centroid_dists;
        thread_local std::vector<int> candidates;
        candidates.clear();

        if (initialized) {
            const float* centroid_block = _manager->get_centroid_block();

            if (num_probes == 1) {
                float    best_d = std::numeric_limits<float>::max();
                uint16_t best_c = 0;
                for (uint16_t c = 0; c < k; ++c) {
                    float d = Distance::l2(query.data(), centroid_block + (size_t)c * dimension,
                                           dimension, use_avx2);
                    if (d < best_d) { best_d = d; best_c = c; }
                }
                for (int slot : cluster_index[best_c])
                    if (!deleted_flags[slot]) candidates.push_back(slot);
            } else {
                centroid_dists.resize(k);
                for (uint16_t c = 0; c < k; ++c) {
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
            }
        } else {
            candidates.reserve(count);
            for (int i = 0; i < count; ++i)
                if (!deleted_flags[i]) candidates.push_back(i);
        }

        if (candidates.empty()) return {};

        PQ pq;
        for (int slot : candidates) {
            const float* vec_ptr = float_block_snap + (size_t)slot * dimension;
            float dist = Distance::l2(vec_ptr, query.data(), dimension, use_avx2);
            if ((int)pq.size() < N)                    pq.push({ dist, slot });
            else if (dist < pq.top().first) { pq.pop(); pq.push({ dist, slot }); }
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
                Log::error("compact_tombstones: could not open " + tmp_file);
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
            Log::error("compact_tombstones: rename failed");
            return;
        }

        size_t old_count = tombstone_entries_on_disk;
        tombstone_entries_on_disk = deleted_ids.size();
        Log::info("Tombstone compacted: " + std::to_string(old_count)
            + " entries -> " + std::to_string(tombstone_entries_on_disk));
    }

    // -----------------------------------------------------------------------
    bool RedBoxVector::remove(uint64_t id) {
        std::unique_lock<std::shared_mutex> lk(rw_mutex);

        if (deleted_ids.count(id)) return false;

        auto it = id_to_index.find(id);
        if (it == id_to_index.end()) return false;

        deleted_flags[it->second] = 1;
        id_to_index.erase(it);

        deleted_ids.insert(id);
        append_tombstone(id);

        if (tombstone_entries_on_disk > deleted_ids.size() + TOMBSTONE_COMPACT_SLACK) {
            compact_tombstones();
        }

        return true;
    }

    // -----------------------------------------------------------------------
    uint32_t RedBoxVector::get_dim() const {
        std::shared_lock<std::shared_mutex> lk(rw_mutex);
        return static_cast<uint32_t>(dimension);
    }

    bool RedBoxVector::update(uint64_t id, const std::vector<float>& vec) {
        std::unique_lock<std::shared_mutex> lk(rw_mutex);

        if (deleted_ids.count(id)) return false;

        auto it = id_to_index.find(id);
        if (it == id_to_index.end()) return false;

        float* dst = _manager->get_float_ptr_mut(static_cast<int>(it->second));
        std::memcpy(dst, vec.data(), dimension * sizeof(float));
        return true;
    }

    void RedBoxVector::set_num_probes(uint8_t p) {
        _manager->set_num_probes(p);
    }

    void RedBoxVector::set_hnsw_ef_search(uint16_t ef) {
        _manager->set_hnsw_ef_search(ef);
    }

    void RedBoxVector::warm_pages() {
        if (!_manager || _manager->get_count() == 0) return;

        volatile uint64_t sink = 0;
        size_t cap = _manager->get_header()->max_capacity;

        // Touch every vector (float_block) — 4KB stride = page
        const float* fblk = _manager->get_float_ptr(0);
        size_t dim = dimension;
        for (size_t i = 0; i < cap; ++i) {
            sink += *reinterpret_cast<const uint64_t*>(&fblk[i * dim]);
        }

        // Touch every edge entry
        if (_manager->get_index_type() == IndexType::HNSW) {
            const uint32_t* eblk = _manager->get_hnsw_edge_block();
            uint8_t M = _manager->get_header()->hnsw_M;
            size_t epn = HnswManager::edges_per_node(M);
            for (size_t i = 0; i < cap; ++i) {
                sink += *reinterpret_cast<const uint64_t*>(&eblk[i * epn]);
            }
        }

        (void)sink;
    }

} 


// ============================================================
// StorageManager
// ============================================================
namespace StorageManager {

    static size_t calc_required_size(
        uint64_t dimensions, int initial_capacity, uint16_t num_clusters,
        CoreEngine::IndexType idx_type, uint8_t hnsw_M)
    {
        size_t base = sizeof(CoreEngine::SpecificMetadata)
                    + (size_t)initial_capacity * sizeof(uint64_t)    // id_block
                    + (size_t)initial_capacity * dimensions * sizeof(float); // float_block

        if (idx_type == CoreEngine::IndexType::IVF) {
            base += (size_t)num_clusters * dimensions * sizeof(float)  // centroids
                  + (size_t)num_clusters * sizeof(uint64_t)           // cluster counts
                  + (size_t)initial_capacity * sizeof(uint16_t);      // cluster block
        } else {
            // HNSW: level_block + edge_block after float_block
            base += (size_t)initial_capacity * sizeof(uint8_t)                // level_block
                  + (size_t)initial_capacity * HnswManager::edges_per_node(hnsw_M) * sizeof(uint32_t);
        }
        return base;
    }

    Manager::Manager(const std::string& db_file, uint64_t dimensions,
                     int initial_capacity, uint16_t num_clusters, uint8_t num_probes,
                     CoreEngine::IndexType index_type, uint8_t hnsw_M, uint16_t hnsw_ef_construction)
        : allocated_size(initial_capacity), filename(db_file),
#ifdef _WIN32
          hFile(NULL), hMapFile(NULL),
#else
          fd(-1),
#endif
          map_base(nullptr),
          header(nullptr), centroid_block(nullptr), cluster_count_block(nullptr),
          cluster_block(nullptr), id_block(nullptr), float_block(nullptr),
          hnsw_level_block(nullptr), hnsw_edge_block(nullptr)
    {
        bool is_hnsw = (index_type == CoreEngine::IndexType::HNSW);
        size_t required_size = calc_required_size(dimensions, initial_capacity, num_clusters, index_type, hnsw_M);
        size_t current_size = 0;

#ifdef _WIN32
        hFile = CreateFileA(filename.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not open file");

        LARGE_INTEGER fileSize;
        GetFileSizeEx(hFile, &fileSize);
        current_size = (size_t)fileSize.QuadPart;

        if (current_size < required_size) {
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
#else
        fd = open(filename.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd < 0) throw std::runtime_error("Could not open file: " + filename);

        struct stat st;
        if (fstat(fd, &st) < 0) { close(fd); throw std::runtime_error("fstat failed"); }
        current_size = (size_t)st.st_size;

        if (current_size < required_size) {
            if (ftruncate(fd, (off_t)required_size) < 0) { close(fd); throw std::runtime_error("ftruncate failed"); }
        }

        map_base = mmap(nullptr, required_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map_base == MAP_FAILED) { close(fd); throw std::runtime_error("mmap failed"); }
        madvise(map_base, required_size, MADV_RANDOM);
#endif

        // Setup pointers based on index type
        header = (CoreEngine::SpecificMetadata*)map_base;

        if (!is_hnsw) {
            // IVF layout: [Header][centroids][cluster_counts][cluster_block][id_block][float_block]
            centroid_block      = (float*)((char*)map_base + sizeof(CoreEngine::SpecificMetadata));
            cluster_count_block = (uint64_t*)(centroid_block + (size_t)num_clusters * dimensions);
            cluster_block       = (uint16_t*)(cluster_count_block + num_clusters);
            id_block            = (uint64_t*)(cluster_block + initial_capacity);
            float_block         = (float*)(id_block + initial_capacity);
            hnsw_level_block    = nullptr;
            hnsw_edge_block     = nullptr;
        } else {
            // HNSW layout: [Header][id_block][float_block][level_block][edge_block]
            centroid_block      = nullptr;
            cluster_count_block = nullptr;
            cluster_block       = nullptr;
            id_block            = (uint64_t*)((char*)map_base + sizeof(CoreEngine::SpecificMetadata));
            float_block         = (float*)(id_block + initial_capacity);
            hnsw_level_block    = (uint8_t*)(float_block + (size_t)initial_capacity * dimensions);
            hnsw_edge_block     = (uint32_t*)(hnsw_level_block + initial_capacity);
        }

        if (current_size == 0) {
            std::memset(header, 0, sizeof(CoreEngine::SpecificMetadata));
            header->vector_count   = 0;
            header->max_capacity   = initial_capacity;
            header->dimensions     = dimensions;
            header->data_type_size = sizeof(float);
            header->next_id        = 1;
            header->version        = CoreEngine::SpecificMetadata::CURRENT_VERSION;
            header->num_clusters   = num_clusters;
            header->is_initialized = 0;
            header->num_probes     = num_probes;
            header->index_type     = static_cast<uint8_t>(index_type);

            if (is_hnsw) {
                header->hnsw_M               = hnsw_M;
                header->hnsw_ef_construction  = hnsw_ef_construction;
                header->hnsw_ef_search        = 256;
                header->hnsw_max_level        = 0;
                header->hnsw_entry_point      = HnswManager::EMPTY;
                header->hnsw_graph_version    = 0;

                // Initialize all edge slots to EMPTY
                size_t total_edges = (size_t)initial_capacity * HnswManager::edges_per_node(hnsw_M);
                for (size_t i = 0; i < total_edges; ++i)
                    hnsw_edge_block[i] = HnswManager::EMPTY;
            }
        }
        else {
            if (header->version != CoreEngine::SpecificMetadata::CURRENT_VERSION) {
#ifdef _WIN32
                UnmapViewOfFile(map_base); CloseHandle(hMapFile); CloseHandle(hFile);
#else
                munmap(map_base, required_size); close(fd);
#endif
                throw std::runtime_error(
                    "Legacy database layout detected (version " +
                    std::to_string(header->version) +
                    "). Please recreate the database.");
            }
            if (header->dimensions != dimensions) {
#ifdef _WIN32
                UnmapViewOfFile(map_base); CloseHandle(hMapFile); CloseHandle(hFile);
#else
                munmap(map_base, required_size); close(fd);
#endif
                throw std::runtime_error("DB dimension mismatch! File has " +
                    std::to_string(header->dimensions));
            }
        }
    }

    uint64_t Manager::next_id() { return header->next_id++; }

    Manager::~Manager() {
        if (map_base) {
#ifdef _WIN32
            FlushViewOfFile(map_base, 0); UnmapViewOfFile(map_base);
#else
            size_t total;
            if (header->index_type == static_cast<uint8_t>(CoreEngine::IndexType::HNSW)) {
                total = sizeof(CoreEngine::SpecificMetadata)
                      + (size_t)header->max_capacity * sizeof(uint64_t)
                      + (size_t)header->max_capacity * header->dimensions * sizeof(float)
                      + (size_t)header->max_capacity * sizeof(uint8_t)
                      + (size_t)header->max_capacity * HnswManager::edges_per_node(header->hnsw_M) * sizeof(uint32_t);
            } else {
                total = sizeof(CoreEngine::SpecificMetadata)
                      + (size_t)header->num_clusters * header->dimensions * sizeof(float)
                      + (size_t)header->num_clusters * sizeof(uint64_t)
                      + (size_t)header->max_capacity * sizeof(uint16_t)
                      + (size_t)header->max_capacity * sizeof(uint64_t)
                      + (size_t)header->max_capacity * header->dimensions * sizeof(float);
            }
            msync(map_base, total, MS_SYNC);
            munmap(map_base, total);
#endif
        }
#ifdef _WIN32
        if (hMapFile)  CloseHandle(hMapFile);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
#else
        if (fd >= 0) close(fd);
#endif
    }

    void Manager::add_vector(uint64_t id, const std::vector<float>& vec, uint16_t cluster) {
        if (vec.size() != header->dimensions)
            throw std::invalid_argument("Vector dimension mismatch");
        if (header->vector_count >= header->max_capacity)
            throw std::runtime_error("Database full");

        size_t slot = header->vector_count;
        id_block[slot]      = id;
        float* dst = float_block + slot * header->dimensions;
        std::memcpy(dst, vec.data(), header->dimensions * sizeof(float));

        if (header->index_type == static_cast<uint8_t>(CoreEngine::IndexType::IVF)) {
            cluster_block[slot] = cluster;
        }

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