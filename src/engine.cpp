#include <iostream>
#include "redboxdb/engine.hpp"
#include "redboxdb/storage_manager.hpp"
#include "redboxdb/VectorPoint.hpp"
#include <vector>
#include <cmath>
#include <fstream>




CoreEngine::RedBoxVector::RedBoxVector(std::string file_name, size_t dim, int size) : file_name(file_name), dimension(dim)
{
    // Initialize the Storage Manager (Assuming it takes the filename)
    _manager = std::make_unique<StorageManager::Manager>(file_name,dim,size);

    // Reserve memory for the flat vector
    temp.reserve(size * (dim + 1));
}

void CoreEngine::RedBoxVector::insert(uint64_t id, const std::vector<float>& vec) {
    try {
        _manager->add_vector(id, vec);
    }
    catch (const std::exception& e) {
        std::cerr << "Insert Error: " << e.what() << std::endl;
    }
}

void CoreEngine::RedBoxVector::saveToDisk([[maybe_unused]]const std::string& filename) {
    // NOTE: With StorageManager (Memory Mapped Files), explicit saving is not required.
    // The Operating System automatically flushes changes to disk.
    // The Manager destructor will also ensure the view is flushed when the app closes.

    std::cout << "-> Persistence handled by StorageManager (Auto-Save active)." << std::endl;
}


void CoreEngine::RedBoxVector::loadFromDisk(const std::string& filename) {
    // NOTE: The StorageManager constructor already re-opened the file 
    // and mapped the existing data into memory.

    std::cout << "-> Database attached to: " << filename << std::endl;
    std::cout << "-> Current Record Count: " << _manager->get_count() << std::endl;
}

int CoreEngine::RedBoxVector::search(const std::vector<float>& query) {
    float min_dist = 1e9;
    int best_id = -1;

    // Iterate through all records in the Manager
    uint64_t count = _manager->get_count();

    for (uint64_t i = 0; i < count; ++i) {
        // Fetch data from Memory Map
        std::pair<uint64_t, std::vector<float>> record = _manager->get_vector(static_cast<int>(i));

        uint64_t id = record.first;
        const std::vector<float>& vec = record.second;

        // Euclidean Distance
        float dist = 0.0f;
        for (size_t d = 0; d < dimension; ++d) {
            float diff = vec[d] - query[d];
            dist += diff * diff;
        }

        if (dist < min_dist) {
            min_dist = dist;
            best_id = static_cast<int>(id);
        }
    }
    return best_id;
}

StorageManager::Manager::Manager(const std::string& db_file, uint64_t dimensions, int initial_capacity)
    : filename(db_file), hFile(NULL), hMapFile(NULL), map_base(nullptr) {

    // 1. CALCULATE STRIDE (The size of one "Row")
    // 8 bytes for ID + (Dim * 4 bytes for floats)
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
StorageManager::Manager::~Manager() {
    if (map_base) { FlushViewOfFile(map_base, 0); UnmapViewOfFile(map_base); }
    if (hMapFile) CloseHandle(hMapFile);
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
}

void StorageManager::Manager::add_vector(uint64_t id, const std::vector<float>& vec) {
    // Validation
    if (vec.size() != header->dimensions) {
        throw std::invalid_argument("Vector dimension mismatch");
    }
    if (header->vector_count >= header->max_capacity) {
        throw std::runtime_error("Database full");
    }

    // --- MANUAL POINTER ARITHMETIC ---

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

std::pair<uint64_t, std::vector<float>> StorageManager::Manager::get_vector(int index) {
    if (index >= header->vector_count) throw std::out_of_range("Index out of bounds");

    // 1. Find the row
    size_t offset = index * row_size_bytes;
    char* row_ptr = data_start + offset;

    // 2. Read ID
    uint64_t id = *(uint64_t*)row_ptr;

    // 3. Read Floats
    float* vec_ptr = (float*)(row_ptr + sizeof(uint64_t));

    std::vector<float> vec(header->dimensions);
    std::memcpy(vec.data(), vec_ptr, header->dimensions * sizeof(float));

    return { id, vec };
}

uint64_t StorageManager::Manager::get_count() const { return header->vector_count; }