#include <vector>
#include <unordered_set>
#include "VectorPoint.hpp"
#include "redboxdb/storage_manager.hpp"

namespace CoreEngine {


    class RedBoxVector {
    private:
        static constexpr int default_capacity = 1000; // this is no. of vectors to store!
        size_t dimension;
        std::unique_ptr<StorageManager::Manager> _manager;
        std::string file_name;


        // For soft deletion
        std::string tombstone_file;
        std::unordered_set<uint64_t> deleted_ids;
        
    public:
        RedBoxVector(std::string file_name, size_t dim, int capacity = default_capacity);

        void insert(uint64_t id, const std::vector<float>& vec);
        int search(const std::vector<float>& query);
        bool remove(uint64_t id);


        // Soft deletion mechanism
        void load_tombstones();
        void append_tombstone(uint64_t id);


        // Just for status print, have no use at this point as everything is automatically handeled
        void saveToDisk(const std::string& filename);
        void loadFromDisk(const std::string& filename);
    };

}