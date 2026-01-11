#include <iostream>
#include "redboxdb/engine.hpp"


#include <vector>
#include <cmath>
#include <fstream>



CoreEngine::SimpleVectorDB::SimpleVectorDB(int dim) : dimension(dim) {}

// INSERT (The "Add Data" Function)
void CoreEngine::SimpleVectorDB::insert(uint64_t id, const std::vector<float>& vec) {
    if (vec.size() != dimension) {
        std::cerr << "Error: Vector dimension mismatch!" << std::endl;
        return;
    }
    storage.push_back({ id, vec });
}

// SEARCH (Brute Force)
int CoreEngine::SimpleVectorDB::search(const std::vector<float>& query) {
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
