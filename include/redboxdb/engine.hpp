#include <vector>
#include "VectorPoint.hpp"   

namespace CoreEngine {

    class RedBoxVector {
    private:
        std::vector<VectorPoint> storage; 
        int dimension;

    public:
        RedBoxVector(int dim);
        void insert(uint64_t id, const std::vector<float>& vec);
        int search(const std::vector<float>& query);
    };

}