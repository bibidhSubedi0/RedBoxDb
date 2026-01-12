#include <iostream>
#include <vector>
#include <filesystem> // C++17, for checking file existence
#include "redboxdb/engine.hpp" // Ensure this matches your header name

void print_result(int id) {
    if (id != -1) std::cout << "RESULT: Found ID: " << id << std::endl;
    else std::cout << "RESULT: Not Found" << std::endl;
}

int main() {
    const std::string DB_FILE = "redbox_mmap.db";
    const int DIM = 3;

    // --- CLEAN SLATE (Optional) ---
    // Remove the file to ensure we are testing fresh creation
    if (std::filesystem::exists(DB_FILE)) {
        std::filesystem::remove(DB_FILE);
        std::cout << "[SETUP] Cleared previous DB file." << std::endl;
    }

    // =========================================================
    // [PHASE 1] Initializing DB and Writing Data (The "Server")
    // =========================================================
    {
        std::cout << "\n[PHASE 1] Initializing DB (Memory Mapped)..." << std::endl;
        CoreEngine::RedBoxVector db(DB_FILE, DIM);

        std::cout << "-> Inserting King (ID 101)..." << std::endl;
        db.insert(101, { 0.9f, 0.1f, 0.1f });

        std::cout << "-> Inserting Queen (ID 102)..." << std::endl;
        db.insert(102, { 0.1f, 0.9f, 0.1f });

        std::cout << "[PHASE 1] Complete. Exiting scope." << std::endl;
        // db destructor runs here -> calls _manager destructor -> flushes Map to Disk
    }

    std::cout << "---------------------------------------------------" << std::endl;

    // =========================================================
    // [PHASE 2] Re-opening DB (The "Restart")
    // =========================================================
    {
        std::cout << "[PHASE 2] Re-opening DB..." << std::endl;
        // Constructor automatically re-maps the existing file
        CoreEngine::RedBoxVector db(DB_FILE, DIM);

        // Optional: Check status (since loadFromDisk is now just a status printer)
        db.loadFromDisk(DB_FILE);

        std::cout << "-> Searching for vector close to { 0.95, 0.1, 0.1 }..." << std::endl;

        // NOTE: Ensure your search function uses _manager->get_vector(i)!
        int result = db.search({ 0.95f, 0.1f, 0.1f });

        print_result(result);

        if (result == 101) {
            std::cout << "SUCCESS: Persistence verified." << std::endl;
        }
        else {
            std::cout << "FAILURE: Could not recover ID 101." << std::endl;
        }
    }

    return 0;
}