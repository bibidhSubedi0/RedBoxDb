#include <iostream>
#include <vector>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <unordered_map> 
#include <memory>
#include "redboxdb/engine.hpp"

#pragma comment(lib, "Ws2_32.lib")

const int PORT = 8080;

// --- PROTOCOL ---
const uint8_t CMD_INSERT = 1;
const uint8_t CMD_SEARCH = 2;
const uint8_t CMD_DELETE = 3;
const uint8_t CMD_SELECT_DB = 4;
const uint8_t CMD_UPDATE = 5;
const uint8_t CMD_INSERT_AUTO = 6;
const uint8_t CMD_SEARCH_N = 7;

// alias for the Catalog
using DbCatalog = std::unordered_map<std::string, std::unique_ptr<CoreEngine::RedBoxVector>>;

// Remove the global 'const int DIMENSIONS = 128;'

void handle_client(SOCKET client_socket, DbCatalog& catalog) {
    std::cout << "[SERVER] Client connected." << std::endl;

    CoreEngine::RedBoxVector* active_db = nullptr;
    char header_buffer[5];

    while (true) {
        int bytes_received = recv(client_socket, header_buffer, 5, 0);
        if (bytes_received <= 0) break;

        uint8_t cmd = header_buffer[0];
        uint32_t meta_data = 0;
        memcpy(&meta_data, &header_buffer[1], 4);

        // --- HANDSHAKE / SELECT DB ---
        if (cmd == CMD_SELECT_DB) {
            uint32_t name_len = meta_data;
            std::string db_name(name_len, ' ');

            // 1. Read the Name
            int total_read = 0;
            while (total_read < (int)name_len) {
                int n = recv(client_socket, &db_name[total_read], name_len - total_read, 0);
                if (n <= 0) return;
                total_read += n;
            }

            // 2. NEW: Read the Requested Dimension (4 bytes)
            uint32_t requested_dim = 0;
            recv(client_socket, (char*)&requested_dim, 4, 0);

            std::cout << "[SERVER] Req DB: " << db_name << " (Dim: " << requested_dim << ")" << std::endl;

            // 3. Load or Create
            if (catalog.find(db_name) == catalog.end()) { // if not found create new
                std::cout << "   -> New/Loading..." << std::endl;
                std::string filename = db_name + ".db";
                // Use the CLIENT PROVIDED dimension!
                // also, for now we are just using a hard limit of 100k
                int static_capacity = 100000;

                catalog[db_name] = std::make_unique<CoreEngine::RedBoxVector>(filename, requested_dim, static_capacity);
            }

            active_db = catalog[db_name].get(); // if found use tei

            // Safety Check: Did client ask for 128 dim but open a 1024 dim file?
            if (active_db->get_dim() != requested_dim) {
                std::cerr << "   [WARNING] Dimension mismatch! File is " << active_db->get_dim() << std::endl;
            }

            send(client_socket, "1", 1, 0);
            continue;
        }

        if (!active_db) return; // Error: No DB selected

        // --- DYNAMIC DIMENSION LOGIC ---
        // Ask the loaded DB how big its vectors are
        int current_dim = active_db->get_dim();
        int vec_byte_size = current_dim * sizeof(float);

        if (cmd == CMD_INSERT) {
            std::vector<float> vec(current_dim);
            int total = 0;
            while (total < vec_byte_size) {
                int n = recv(client_socket, (char*)vec.data() + total, vec_byte_size - total, 0);
                if (n <= 0) return;
                total += n;
            }
            active_db->insert(meta_data, vec);
            send(client_socket, "1", 1, 0);
        }
        else if (cmd == CMD_SEARCH) {
            std::vector<float> query(current_dim);
            int total = 0;
            while (total < vec_byte_size) {
                int n = recv(client_socket, (char*)query.data() + total, vec_byte_size - total, 0);
                if (n <= 0) return;
                total += n;
            }
            int result_id = active_db->search(query);
            send(client_socket, (char*)&result_id, 4, 0);
        }
        else if (cmd == CMD_DELETE) {
            bool success = active_db->remove(meta_data);
            char resp = success ? '1' : '0';
            send(client_socket, &resp, 1, 0);
        }
        else if (cmd == CMD_UPDATE) {
            // Logic is identical to INSERT, but calls db->update()
            // 1. Determine size
            // already done mathi

            std::vector<float> vec(current_dim);
            int total = 0;

            // 2. Read Vector Payload
            while (total < vec_byte_size) {
                int n = recv(client_socket, (char*)vec.data() + total, vec_byte_size - total, 0);
                if (n <= 0) return;
                total += n;
            }

            // 3. Perform Update
            bool success = active_db->update(meta_data, vec);
            
            // 4. Send Response
            // '1' = Success (Updated)
            // '0' = Failure (ID not found or Deleted)
            char resp = success ? '1' : '0'; 
            send(client_socket, &resp, 1, 0);
        }
        else if (cmd == CMD_INSERT_AUTO) {
            std::vector<float> vec(current_dim);
            int total = 0;
            while (total < vec_byte_size) {
                int n = recv(client_socket, (char*)vec.data() + total, vec_byte_size - total, 0);
                if (n <= 0) return;
                total += n;
            }
            uint64_t assigned_id = active_db->insert_auto(vec);
            send(client_socket, (char*)&assigned_id, sizeof(assigned_id), 0);  // send back 8 bytes
        }
        else if (cmd == CMD_SEARCH_N) {
            int n = static_cast<int>(meta_data);  // N comes in the META field

            std::vector<float> query(current_dim);
            int total = 0;
            while (total < vec_byte_size) {
                int recv_n = recv(client_socket, (char*)query.data() + total, vec_byte_size - total, 0);
                if (recv_n <= 0) return;
                total += recv_n;
            }

            std::vector<int> results = active_db->search_N(query, n);

            // Send: [result_count (uint32)] [id_0 (int32)] [id_1] ...
            uint32_t count = static_cast<uint32_t>(results.size());
            send(client_socket, (char*)&count, sizeof(count), 0);
            if (count > 0) {
                send(client_socket, (char*)results.data(), count * sizeof(int), 0);
            }
        }
    }
}
int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;

    // --- CATALOG (Persists across clients, is this a good thing? IDFK, IDFC) ---
    DbCatalog catalog;
    // catalog["default"] = std::make_unique<CoreEngine::RedBoxVector>("default.db", DIMENSIONS);

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_socket, 1);

    std::cout << "[SERVER] Multi-Tenant Manager Listening on Port " << PORT << "..." << std::endl;

    while (true) {
        SOCKET client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket != INVALID_SOCKET) {
            handle_client(client_socket, catalog);
            closesocket(client_socket);
        }
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}

/*
    So the basic idea is as follow
    -> We make a custom binary protocol over TCP
    -> no headers, no json, nothing
    -> Little Endian

    Request Packet Structure (Client -> Server)
    [ 1 Byte Command ] [ 4 Bytes Meta ] [ ... Optional Payload ... ]


    Offset    Field    Type      Size      Description
    0         CMD       uint8    1 Byte    The Operation Code (OpCode).
    1         META      uint32   4 Bytes   "Context-dependent integer (ID     Size     etc.)."
    5         PAYLOAD   bytes    N Bytes   The raw data (Vector floats or String name).



    CMD ID    Name        META Field        Payload                              Server Response
    1        INSERT        Vector ID        Raw Float Data(Dim×4 bytes)          1 (Ack)
    2        SEARCH        (Ignored)        Raw Float Data (Dim×4 bytes)        Result ID (4 bytes)
    3        DELETE        Vector ID        (None)                               1 or 0 (Success/Fail)
    4        SELECT_DB     Name Length      Database Name (UTF-8 String)         1 (Ack)
*/