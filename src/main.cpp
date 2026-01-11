#include <iostream>
#include "redboxdb/engine.hpp"
#include "redboxdb/version.hpp"

int main() {
    std::cout << "RedBoxDb v" << redboxdb::version << '\n';
    

        // 1. INITIALIZE DB
        CoreEngine::RedBoxVector db(3);

        std::vector<float> vector_king = { 0.9f, 0.1f, 0.1f };

        // 3. ADD TO DB
        std::cout << "Adding 'King' to Database..." << std::endl;
        db.insert(101, vector_king);

        // 4. ADD ANOTHER (To test search)
        std::vector<float> vector_queen = { 0.8f, 0.2f, 0.1f };
        db.insert(102, vector_queen);

        // 5. TEST SEARCH
        std::vector<float> query = { 0.95f, 0.1f, 0.1f };
        int result_id = db.search(query);

        std::cout << "Closest Vector ID: " << result_id << std::endl; // Should be 101

        return 0;
    
}
