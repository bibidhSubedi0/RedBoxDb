#include <iostream>
#include "redboxdb/engine.hpp"
#include "redboxdb/storage_manager.hpp"
#include "redboxdb/VectorPoint.hpp"
#include <vector>
#include <cmath>
#include <fstream>
#include <queue>
#include <algorithm>


namespace CoreEngine{
    RedBoxVector::RedBoxVector(std::string file_name, size_t dim, int capacity) : file_name(file_name),tombstone_file(file_name + ".del"), dimension(dim)
    {
        _manager = std::make_unique<StorageManager::Manager>(file_name,dim, capacity);
        load_tombstones();
    }

    void RedBoxVector::insert(uint64_t id, const std::vector<float>& vec) {
        if (deleted_ids.count(id)) {
            deleted_ids.erase(id);
            // ideally we should remove it from the .del file too, 
            // but for a simple append only log, we can ignore this for now.
        }
        try {
            _manager->add_vector(id, vec);
        }
        catch (const std::exception& e) {
            std::cerr << "Insert Error: " << e.what() << std::endl;
        }
    }

    void RedBoxVector::saveToDisk([[maybe_unused]]const std::string& filename) {
        // NOTE: With StorageManager (Memory Mapped Files), explicit saving is not required.
        // The Operating System automatically flushes changes to disk.
        // The Manager destructor will also ensure the view is flushed when the app closes.

        std::cout << "-> Persistence handled by StorageManager (Auto-Save active)." << std::endl;
    }

    void RedBoxVector::loadFromDisk(const std::string& filename) {
        // NOTE: The StorageManager constructor already re-opened the file 
        // and mapped the existing data into memory.

        std::cout << "-> Database attached to: " << filename << std::endl;
        std::cout << "-> Current Record Count: " << _manager->get_count() << std::endl;
    }

    int CoreEngine::RedBoxVector::search(const std::vector<float>& query) {
        float min_dist = 1e9;
        int best_id = -1;

        // Cache the loop count to avoid calling function every iteration
        int count = static_cast<int>(_manager->get_count());

        for (int i = 0; i < count; ++i) {

            // --- ZERO COPY READ ---
            // get a pointer directly to the Memory Map
            auto record = _manager->get_vector_raw(i);

            uint64_t id = record.first;

            // --- DELETION CHECK ---
            if (deleted_ids.count(id)) {
                continue; // Skip this vector, it is dead.
            }

            // I HAVE ABSOUTELY NO CLUE, IF THIS IS THE RIGHT WAY TO DO THIS BUT FUCK IT WE BALL
            // (I can already think of 10 problems this can cause *cry emoji)
            // ----------------------

            const float* vec_ptr = record.second; // No std::vector

            // Euclidean Distance using raw pointers
            float dist = 0.0f;
            for (size_t d = 0; d < dimension; ++d) {
                // Access memory directly
                float diff = vec_ptr[d] - query[d];
                dist += diff * diff;
            }

            if (dist < min_dist) {
                min_dist = dist;
                best_id = static_cast<int>(id);
            }
        }
        return best_id;
    }

    void RedBoxVector::load_tombstones() {
        std::ifstream f(tombstone_file, std::ios::binary);
        if (!f.is_open()) return; 

        uint64_t id;
        while (f.read(reinterpret_cast<char*>(&id), sizeof(id))) {
            deleted_ids.insert(id);
        }
        // std::cout << "[DB] Loaded " << deleted_ids.size() << " tombstones." << std::endl;
    }

    void RedBoxVector::append_tombstone(uint64_t id) {
        // Append to disk immediately so it survives a crash
        std::ofstream f(tombstone_file, std::ios::binary | std::ios::app);
        if (f.is_open()) {
            f.write(reinterpret_cast<const char*>(&id), sizeof(id));
        }
    }

    bool RedBoxVector::remove(uint64_t id) {
        // 1. If already deleted, do nothing
        if (deleted_ids.count(id)) return false;

        // 2. Add to memory (for immediate effect)
        deleted_ids.insert(id);

        // 3. Persist to disk
        append_tombstone(id);

        return true;
    }

    std::vector<int> RedBoxVector::search_N(const std::vector<float>& query, int N) {
        // It automatically keeps the LARGEST distance at the top.
        std::priority_queue<std::pair<float, int>> pq;

        int count = static_cast<int>(_manager->get_count());

        for (int i = 0; i < count; ++i) {

            // --- ZERO COPY READ ---
            auto record = _manager->get_vector_raw(i);
            uint64_t id = record.first;

            // --- DELETION CHECK ---
            if (deleted_ids.count(id)) continue;

            // --- DISTANCE CALCULATION ---
            const float* vec_ptr = record.second;
            float dist = 0.0f;

            for (size_t d = 0; d < dimension; ++d) {
                float diff = vec_ptr[d] - query[d];
                dist += diff * diff;
            }

            // --- THE "TOP N" LOGIC ---
            if (pq.size() < N) {
                pq.push({ dist, static_cast<int>(id) });
            }
            else if (dist < pq.top().first) {
                pq.pop();  // Kick out the worst one
                pq.push({ dist, static_cast<int>(id) }); // Add the new one
            }
        }

        // --- EXTRACT RESULTS ---
        std::vector<int> result;
        result.reserve(pq.size());

        while (!pq.empty()) {
            result.push_back(pq.top().second);
            pq.pop();
        }

        std::reverse(result.begin(), result.end());

        return result;
    }
}

namespace StorageManager {
    Manager::Manager(const std::string& db_file, size_t dimensions, int initial_capacity)
        : filename(db_file),allocated_size(initial_capacity), hFile(NULL), hMapFile(NULL), map_base(nullptr) {

        // 1. CALCULATE STRIDE (The size of one "Row")
        // 8 bytes for ID + (Dim * 4 bytes for floats)
        /* Because
            struct VectorPoint {
                uint64_t id;
                std::vector<float> values;
             };
        */
        row_size_bytes = sizeof(uint64_t) + (dimensions * sizeof(float));

        // 2. OPEN FILE
        hFile = CreateFileA(filename.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not open file");

        // 3. CHECK SIZE
        LARGE_INTEGER fileSize;
        GetFileSizeEx(hFile, &fileSize);
        size_t current_size = (size_t)fileSize.QuadPart;

        // Total Size = Header + (RowSize * Capacity)
        size_t required_size = sizeof(CoreEngine::SpecificMetadata) + (row_size_bytes * initial_capacity);

        if (current_size == 0) {
            LARGE_INTEGER distance;
            distance.QuadPart = required_size;
            if (!SetFilePointerEx(hFile, distance, NULL, FILE_BEGIN)) throw std::runtime_error("Resize failed");
            if (!SetEndOfFile(hFile)) throw std::runtime_error("SetEndOfFile failed");
        }

        // 4. MAP
        hMapFile = CreateFileMappingA(hFile, NULL, PAGE_READWRITE, 0, 0, NULL);
        map_base = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);

        header = (CoreEngine::SpecificMetadata*)map_base;
        data_start = (char*)map_base + sizeof(CoreEngine::SpecificMetadata);

        // 5. INITIALIZE HEADER
        if (current_size == 0) {
            header->vector_count = 0;
            header->max_capacity = initial_capacity;
            header->dimensions = dimensions;       // STORE THE DYNAMIC DIM!
            header->data_type_size = sizeof(float);
        }
        else {
            // SAFETY CHECK: If loading existing DB, ensure dimensions match!
            if (header->dimensions != dimensions) {
                throw std::runtime_error("DB Dimension mismatch! File has " + std::to_string(header->dimensions));
            }
        }
    }
    
    Manager::~Manager() {
        if (map_base) { FlushViewOfFile(map_base, 0); UnmapViewOfFile(map_base); }
        if (hMapFile) CloseHandle(hMapFile);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    }

    void Manager::add_vector(uint64_t id, const std::vector<float>& vec) {
        // Validation
        if (vec.size() != header->dimensions) {
            throw std::invalid_argument("Vector dimension mismatch");
        }
        if (header->vector_count >= header->max_capacity) {
            throw std::runtime_error("Database full");
        }

        // --- MANUAL POINTER ARITHMETIC ---
        // This is BAD, like BAD BAD

        // 1. Calculate where this row starts
        // Offset = Index * Bytes_Per_Row
        size_t offset = header->vector_count * row_size_bytes;

        // 2. Get the pointer to the start of this row
        char* row_ptr = data_start + offset;

        // 3. Write the ID (First 8 bytes)
        // We cast the char* to a uint64_t* to write the ID
        *(uint64_t*)row_ptr = id;

        // 4. Write the Floats (Next X bytes)
        // Move pointer past the ID
        float* vec_ptr = (float*)(row_ptr + sizeof(uint64_t));

        // Copy the raw floats
        std::memcpy(vec_ptr, vec.data(), header->dimensions * sizeof(float));

        header->vector_count++;
    }

    std::pair<uint64_t, const float*> StorageManager::Manager::get_vector_raw(int index) {
        // Safety check (optional for raw speed, but good for stability)
        if (index >= header->vector_count) throw std::out_of_range("Index out of bounds");

        // 1. Calculate the exact memory address of this row
        size_t offset = index * row_size_bytes;
        char* row_ptr = data_start + offset;

        // 2. Read ID (Direct cast)
        uint64_t id = *(uint64_t*)row_ptr;

        // 3. Get Pointer to Floats (Jump past the 8-byte ID)
        const float* vec_ptr = (const float*)(row_ptr + sizeof(uint64_t));

        // Return the ID and the address of the data (No Copy!)
        return { id, vec_ptr };
    }

    uint64_t Manager::get_count() const { return header->vector_count; }
}