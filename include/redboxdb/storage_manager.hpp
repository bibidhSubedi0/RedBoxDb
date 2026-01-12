#pragma once

namespace StorageManager {
    class Manager {
    private:
        int allocated_size;

    public:
        Manager(int size);
    };
}
/*
    
    So every database file's size will ofc depened on the database

  [ Header (24 bytes) ] [ Vector 0 ] [ Vector 1 ] [ Vector 2 ] ...
    ^                      ^
    Start of mmap          Start of data = (Start + 24)  Next vector = (Start + 24 + 4096)

    Every vector will be metadata.dimentions * metadata.data_type_size = say 1024 * 4byte = 4096 byte or 1024 * 32 bits = 32768 bits
    I can just work with bytes

*/