#pragma once

namespace CoreEngine {
    struct SpecificMetadata {
        uint64_t vector_count;
        uint64_t max_capacity;
        uint64_t dimensions;      // DYNAMIC!
        uint64_t data_type_size;  // 4 (float)
        uint8_t _padding[96];
    };
    // No VectorPoint struct here! We build it manually.
}