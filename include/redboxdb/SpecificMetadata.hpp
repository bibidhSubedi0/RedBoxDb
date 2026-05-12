#pragma once
#include <cstdint>

namespace CoreEngine {
    struct SpecificMetadata {
        uint64_t vector_count;
        uint64_t max_capacity;
        uint64_t dimensions;      // DYNAMIC!
        uint64_t data_type_size;  // 4 (float)
        uint64_t next_id;
        uint8_t  version;         // Layout version. 0 = legacy interleaved, 1 = columnar
        uint8_t  _padding[87];    // Future proofing — struct stays 128 bytes

        static constexpr uint8_t CURRENT_VERSION = 1;
    };

    static_assert(sizeof(SpecificMetadata) == 128, "SpecificMetadata must be exactly 128 bytes");
}