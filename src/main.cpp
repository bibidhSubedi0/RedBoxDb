#include <iostream>
#include <vector>
#include <filesystem>
#include "redboxdb/engine.hpp" 

const std::string DB_FILE = "sanity_search_n.db";
const int DIM = 3;

void print_vec(const std::vector<int>& ids) {
    std::cout << "[ ";
    for (int id : ids) std::cout << id << " ";
    std::cout << "]" << std::endl;
}

int main() {
    // Cleanup
    if (std::filesystem::exists(DB_FILE)) std::filesystem::remove(DB_FILE);
    if (std::filesystem::exists(DB_FILE + ".del")) std::filesystem::remove(DB_FILE + ".del");

    std::cout << "--- SEARCH_N SANITY CHECK ---" << std::endl;

    CoreEngine::RedBoxVector db(DB_FILE, DIM, 100);

    // Scenario: Query will be at [0,0,0]
    // 1. Gold Medal   (Dist=1)
    db.insert(1, { 1.0f, 0.0f, 0.0f });
    // 2. Silver Medal (Dist=4)
    db.insert(2, { 2.0f, 0.0f, 0.0f });
    // 3. Bronze Medal (Dist=9)
    db.insert(3, { 3.0f, 0.0f, 0.0f });
    // 4. Loser        (Dist=10000)
    db.insert(99, { 100.0f, 0.0f, 0.0f });

    // TEST 1: Get Top 3
    std::cout << "Querying Top 3 (Expect: 1 2 3)..." << std::endl;
    auto results = db.search_N({ 0.0f, 0.0f, 0.0f }, 3);
    print_vec(results);

    // TEST 2: Get Top 1 (Should just be ID 1)
    std::cout << "Querying Top 1 (Expect: 1)..." << std::endl;
    results = db.search_N({ 0.0f, 0.0f, 0.0f }, 1);
    print_vec(results);

    // TEST 3: Soft Deletion Interaction
    std::cout << "Deleting ID 2 (Silver Medal)..." << std::endl;
    db.remove(2);

    std::cout << "Querying Top 3 again (Expect: 1 3 99)..." << std::endl;
    // Since 2 is gone, 99 should get promoted to 3rd place
    results = db.search_N({ 0.0f, 0.0f, 0.0f }, 3);
    print_vec(results);

    return 0;
}