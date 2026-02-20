#pragma once

namespace CoreEngine {
    struct SpecificMetadata {
        uint64_t vector_count;
        uint64_t max_capacity;
        uint64_t dimensions;      // DYNAMIC!
        uint64_t data_type_size;  // 4 (float)
        uint64_t next_id;
        uint8_t _padding[88];    // Future proofing
    };
}