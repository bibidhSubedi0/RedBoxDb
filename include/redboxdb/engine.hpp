#include <vector>
#include "VectorPoint.hpp"
#include "redboxdb/storage_manager.hpp"

namespace CoreEngine {

    struct SpecificMetadata { // Total size = 8 bytes + 8 bytes + 8 bytes 
        size_t dimentions; // 64 bits system ko lagi size_t is 8 bytes, we can just assume 64 bits fuck it -> 
        // Say each vector is 1024 dim, then i use float (32 bits -> 4 bytes), then each vector is of size dimentons * sizeof(float) = 1024 * 4 = 4096 Bytes or 4 KB 
        size_t allocated_size; // We keep this in bytes nai i guess? 
        size_t data_type_size;
    };

    class RedBoxVector {
    private:
        static constexpr int default_size = 10240;
        std::vector<VectorPoint> storage; 
        size_t dimension;
        std::unique_ptr<StorageManager::Manager> _manager;
        SpecificMetadata _metadata;

    public:
        RedBoxVector(size_t dim, int size = default_size);
        void insert(uint64_t id, const std::vector<float>& vec);
        void remove(uint64_t id);
        int search(const std::vector<float>& query);
    };

}