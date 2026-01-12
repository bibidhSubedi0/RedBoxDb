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
        size_t allocated_size;
        std::string filename;
        HANDLE hFile;
        HANDLE hMapFile;
        void* map_base;

        CoreEngine::SpecificMetadata* header;

        char* data_start;
        size_t row_size_bytes;

    public:
        Manager(const std::string& db_file, uint64_t dimensions, int initial_capacity);
        ~Manager();

        void add_vector(uint64_t id, const std::vector<float>& vec);
        std::pair<uint64_t, std::vector<float>> get_vector(int index);
        uint64_t get_count() const;
    };
}



/*
    I am on windows! Forgive me.
*/




/*
    
    So every database file's size will ofc depened on the database

  [ Header (24 bytes) ] [ Vector 0 ] [ Vector 1 ] [ Vector 2 ] ...
    ^                      ^
    Start of mmap          Start of data = (Start + 24)  Next vector = (Start + 24 + 4096)

    Every vector will be metadata.dimentions * metadata.data_type_size = say 1024 * 4byte = 4096 byte or 1024 * 32 bits = 32768 bits
    I can just work with bytes

*/