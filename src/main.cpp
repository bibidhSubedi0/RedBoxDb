#include <iostream>
#include <vector>
#include <filesystem>
#include "redboxdb/engine.hpp" 



int main() {
    const std::string DB_FILE = "redbox_dev.db";
    const int DIM = 3;

    // --- CLEAN SLATE ---
    if (std::filesystem::exists(DB_FILE)) {
        std::filesystem::remove(DB_FILE);
        std::cout << "[SETUP] Cleared previous DB file." << std::endl;
    }

    // =========================================================
    // [PHASE 1] Write Data
    // =========================================================
    {
        std::cout << "\n[PHASE 1] Initializing DB..." << std::endl;
        // Capacity = 100 vectors
        CoreEngine::RedBoxVector db(DB_FILE, DIM, 100);

        std::cout << "-> Inserting King (ID 101)..." << std::endl;
        db.insert(101, { 0.9f, 0.1f, 0.1f });

        std::cout << "-> Inserting Queen (ID 102)..." << std::endl;
        db.insert(102, { 0.1f, 0.9f, 0.1f });

        // db destructor runs here -> flushes Map to Disk
    }

    std::cout << "---------------------------------------------------" << std::endl;

    // =========================================================
    // [PHASE 2] Read Data
    // =========================================================
    {
        std::cout << "[PHASE 2] Re-opening DB..." << std::endl;
        CoreEngine::RedBoxVector db(DB_FILE, DIM, 100);

        
        std::cout << "-> Searching for vector close to { 0.95, 0.1, 0.1 }..." << std::endl;

        int result = db.search({ 0.95f, 0.1f, 0.1f }); // Should find 101

        if (result == 101) {
            std::cout << "SUCCESS: Persistence verified." << std::endl;
        }
        else {
            std::cout << "FAILURE: Could not recover ID 101." << std::endl;
        }
    }

    return 0;
}