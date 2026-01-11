#pragma once
#include <vector>
#include <cstdint>

namespace CoreEngine {

    struct VectorPoint {
        uint64_t id;
        std::vector<float> values;
    };

}
