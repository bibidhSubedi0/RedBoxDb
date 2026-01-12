#include <iostream>
#include <vector>
#include <filesystem>
#include "redboxdb/engine.hpp" 

const std::string DB_FILE = "sanity_check.db";
const int DIM = 3;

void log_test(const std::string& msg) {
    std::cout << "[TEST] " << msg << std::endl;
}

int main() {
    // Cleanup previous run
    if (std::filesystem::exists(DB_FILE)) std::filesystem::remove(DB_FILE);
    if (std::filesystem::exists(DB_FILE + ".del")) std::filesystem::remove(DB_FILE + ".del");

    log_test("Initializing DB...");

    // 1. Setup Data
    {
        CoreEngine::RedBoxVector db(DB_FILE, DIM, 100);
        // ID 1: Target (At Origin)
        db.insert(1, { 0.0f, 0.0f, 0.0f });
        // ID 2: Distractor (Far away)
        db.insert(2, { 10.0f, 10.0f, 10.0f });

        // Search should find ID 1
        int res = db.search({ 0.1f, 0.1f, 0.1f });
        std::cout << "-> Initial Search (Expected 1): Found " << res << std::endl;

        // 2. Perform Delete
        log_test("Deleting ID 1...");
        db.remove(1);

        // Search again immediately
        res = db.search({ 0.1f, 0.1f, 0.1f });
        std::cout << "-> Post-Delete Search (Expected 2): Found " << res << std::endl;
    } // Destructor runs here

    // 3. Verify Persistence (Restart)
    log_test("Simulating App Restart...");
    {
        CoreEngine::RedBoxVector db(DB_FILE, DIM, 100);

        // Search again
        int res = db.search({ 0.1f, 0.1f, 0.1f });
        std::cout << "-> Post-Restart Search (Expected 2): Found " << res << std::endl;

        if (res == 2) std::cout << "\nSUCCESS: Deletion persisted correctly." << std::endl;
        else std::cout << "\nFAILURE: ID 1 came back from the dead!" << std::endl;
    }

    return 0;
}