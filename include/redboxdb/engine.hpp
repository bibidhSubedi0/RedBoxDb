#include <vector>
#include "VectorPoint.hpp"   

namespace CoreEngine {

    class SimpleVectorDB {
    private:
        std::vector<VectorPoint> storage; 
        int dimension;

    public:
        SimpleVectorDB(int dim);
        void insert(uint64_t id, const std::vector<float>& vec);
        int search(const std::vector<float>& query);
    };

}