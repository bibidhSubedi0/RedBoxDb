#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <windows.h>
#include <redboxdb/SpecificMetadata.hpp>

namespace StorageManager {

    class Manager {
    private:
        size_t      allocated_size;
        std::string filename;
        HANDLE      hFile;
        HANDLE      hMapFile;
        void*       map_base;

        CoreEngine::SpecificMetadata* header;

        // Clustered columnar layout (version 2):
        //   [ Header (128 bytes)                        ]
        //   [ centroid_block:      K x dim x 4 bytes    ]
        //   [ cluster_count_block: K x 8 bytes          ]
        //   [ cluster_block:       capacity x 2 bytes   ]
        //   [ id_block:            capacity x 8 bytes   ]
        //   [ float_block:         capacity x dim x 4   ]
        float*    centroid_block;
        uint64_t* cluster_count_block;
        uint16_t* cluster_block;
        uint64_t* id_block;
        float*    float_block;

    public:
        Manager(const std::string& db_file, uint64_t dimensions, int initial_capacity, uint8_t num_clusters = 100, uint8_t num_probes = 1);
        ~Manager();

        void             add_vector(uint64_t id, const std::vector<float>& vec, uint16_t cluster);
        const float*     get_float_ptr(int index) const;
        float*           get_float_ptr_mut(int index);
        uint64_t         get_id(int index) const;
        uint16_t         get_cluster(int index) const;
        void             set_cluster(int index, uint16_t c);
        uint64_t         get_count() const;
        uint64_t         next_id();

        float*       get_centroid_block()       { return centroid_block; }
        uint64_t*    get_cluster_count_block()  { return cluster_count_block; }
        uint16_t*    get_cluster_block()        { return cluster_block; }

        bool    is_cluster_initialized() const  { return header->is_initialized != 0; }
        void    set_cluster_initialized()       { header->is_initialized = 1; }
        uint8_t get_num_clusters() const        { return header->num_clusters; }
        uint8_t get_num_probes()   const        { return header->num_probes; }
    };
}


/*
    Clustered columnar layout (version 2):

    [ Header (128 bytes)                      ]
    [ centroid_block: K x dim x 4 bytes       ]  <- K centroids, row-major
    [ cluster_count_block: K x 8 bytes        ]  <- vector count per cluster
    [ cluster_block: capacity x uint16_t      ]  <- which cluster each slot belongs to
    [ id_block: capacity x uint64_t           ]
    [ float_block: capacity x dim x float     ]

    Slot i:
      cluster_block[i]             -> cluster assignment
      id_block[i]                  -> vector's user-facing ID
      float_block + i * dimensions -> pointer to float data
*/
/*
    I am on windows! Forgive me.
*/


/*
    Columnar layout (version 1):

    [ Header (128 bytes) ] [ ID block ] [ Float block ]
      ^                      ^             ^
      Start of mmap          +128          +128 + capacity*8

    ID block:    capacity * 8  bytes  (one uint64_t per slot)
    Float block: capacity * dim * 4 bytes (dim floats per slot)

    Slot i:
      id_block[i]                          -> the vector's ID
      float_block + i * dimensions         -> pointer to its floats

    Search loop only touches float_block — IDs are never read during scan.
*/