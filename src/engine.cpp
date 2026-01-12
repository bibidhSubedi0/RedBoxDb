#include <iostream>
#include "redboxdb/engine.hpp"
#include "redboxdb/storage_manager.hpp"

#include <vector>
#include <cmath>
#include <fstream>



CoreEngine::RedBoxVector::RedBoxVector(size_t dim, int size) : dimension(dim) {
    _manager =  std::make_unique<StorageManager::Manager>(size);
    _metadata.allocated_size = size;
    _metadata.allocated_size = dim;
    _metadata.data_structure_size = sizeof(float);
}


// INSERT
void CoreEngine::RedBoxVector::insert(uint64_t id, const std::vector<float>& vec) {
    if (vec.size() != dimension) {
        std::cerr << "Error: Vector dimension mismatch!" << std::endl;
        return;
    }
    storage.push_back({ id, vec });

    // Insertion happens through _manager
}

// DELETE
void CoreEngine::RedBoxVector::remove(uint64_t id)
{
    for (auto i = 0;i < storage.size();i++) {
        if (id == storage[i].id)
        {
            storage.erase(storage.begin() + i);
            return;
        }
    }
    std::cerr << "Error: ID not found!" << std::endl;
}

// SEARCH (Brute Force)
int CoreEngine::RedBoxVector::search(const std::vector<float>& query) {
        float min_dist = 1e9;
        int best_id = -1;

        for (const auto& point : storage) {
            float dist = 0.0f;
            // Euclidean Distance Calculation
            for (size_t i = 0; i < dimension; ++i) {
                float diff = point.values[i] - query[i];
                dist += diff * diff;
            }

            if (dist < min_dist) {
                min_dist = dist;
                best_id = static_cast<int>(point.id);
            }
        }
        return best_id;
    }


//StorageManager::Manager::Manager(int size=default_size)
StorageManager::Manager::Manager(int size) {
    allocated_size = size;
}