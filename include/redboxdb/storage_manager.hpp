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

        // Columnar layout (version 1):
        //   [ Header (128 bytes) ]
        //   [ ID block:    capacity * sizeof(uint64_t) ]
        //   [ Float block: capacity * dimensions * sizeof(float) ]
        uint64_t* id_block;    // points to start of ID column
        float*    float_block; // points to start of float column

    public:
        Manager(const std::string& db_file, uint64_t dimensions, int initial_capacity);
        ~Manager();

        void             add_vector(uint64_t id, const std::vector<float>& vec);
        const float*     get_float_ptr(int index) const;
        float*           get_float_ptr_mut(int index);
        uint64_t         get_id(int index) const;
        uint64_t         get_count() const;
        uint64_t         next_id();
    };
}


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