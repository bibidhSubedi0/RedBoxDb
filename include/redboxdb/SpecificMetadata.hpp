#pragma once
#include <cstdint>

namespace CoreEngine {

    enum class IndexType : uint8_t {
        IVF  = 0,
        HNSW = 1
    };

    struct SpecificMetadata {
        // --- Core fields (bytes 0-39) ---
        uint64_t vector_count;
        uint64_t max_capacity;
        uint64_t dimensions;
        uint64_t data_type_size;  // 4 (float)
        uint64_t next_id;
        // --- IVF fields (bytes 40-44) ---
        uint16_t  num_clusters;
        uint8_t  version;
        uint8_t  is_initialized;
        uint8_t  num_probes;
        // --- HNSW fields (bytes 45-63) ---
        uint8_t  index_type;       // 0 = IVF, 1 = HNSW
        uint8_t  hnsw_M;
        uint16_t hnsw_ef_construction;
        uint16_t hnsw_ef_search;
        uint8_t  hnsw_max_level;
        uint8_t  _pad0;
        uint32_t hnsw_entry_point;
        uint32_t hnsw_graph_version;
        // --- Padding (bytes 64-127) ---
        uint8_t  _padding[64];

        static constexpr uint8_t CURRENT_VERSION = 4;
        static constexpr uint32_t UINT32_MAX_SENTINEL = 0xFFFFFFFF;
    };

    static_assert(sizeof(SpecificMetadata) == 128, "SpecificMetadata must be exactly 128 bytes");
}