#include <vector>
#include "VectorPoint.hpp"
#include "redboxdb/storage_manager.hpp"

namespace CoreEngine {


    class RedBoxVector {
    private:
        static constexpr int default_size = 10240;
        size_t dimension;
        std::unique_ptr<StorageManager::Manager> _manager;
        std::string file_name;
        std::vector<float> temp;
        
    public:
        RedBoxVector(std::string file_name, size_t dim, int size = default_size);
        void saveToDisk(const std::string& filename);
        void loadFromDisk(const std::string& filename);
        void insert(uint64_t id, const std::vector<float>& vec);
        int search(const std::vector<float>& query);
    };

}