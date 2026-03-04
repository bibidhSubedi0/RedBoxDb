#include <iostream>
#include <vector>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <unordered_map>
#include <memory>
#include <thread>
#include <mutex>
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


using DbCatalog = std::unordered_map<std::string, std::unique_ptr<CoreEngine::RedBoxVector>>;
using MutexMap = std::unordered_map<std::string, std::unique_ptr<std::mutex>>;

struct SharedState {
    DbCatalog  catalog;
    MutexMap   db_mutexes;
    std::mutex catalog_mutex; // guards catalog + db_mutexes maps themselves
};

// -----------------------------------------------------------------------
// handle_client — runs on its own thread, owns the client socket lifetime
// -----------------------------------------------------------------------
void handle_client(SOCKET client_socket, SharedState& state) {
    std::cout << "[SERVER] Client connected (thread " << std::this_thread::get_id() << ")\n";

    CoreEngine::RedBoxVector* active_db = nullptr;
    std::mutex* active_mtx = nullptr;

    char header_buffer[5];

    auto recv_all = [&](char* buf, int len) -> bool {
        int total = 0;
        while (total < len) {
            int n = recv(client_socket, buf + total, len - total, 0);
            if (n <= 0) return false;
            total += n;
        }
        return true;
        };

    while (true) {
        if (!recv_all(header_buffer, 5)) break;

        uint8_t  cmd = header_buffer[0];
        uint32_t meta_data = 0;
        memcpy(&meta_data, &header_buffer[1], 4);

        // --- HANDSHAKE / SELECT DB ---
        if (cmd == CMD_SELECT_DB) {
            uint32_t name_len = meta_data;
            std::string db_name(name_len, ' ');

            if (!recv_all(&db_name[0], (int)name_len)) break;

            uint32_t requested_dim = 0;
            if (!recv_all((char*)&requested_dim, 4)) break;

            std::cout << "[SERVER] Req DB: " << db_name
                << " (Dim: " << requested_dim << ")\n";

            {
                // Lock catalog only long enough to load/create the entry
                std::lock_guard<std::mutex> lock(state.catalog_mutex);

                if (state.catalog.find(db_name) == state.catalog.end()) {
                    std::cout << "   -> New/Loading...\n";
                    std::string filename = db_name + ".db";
                    state.catalog[db_name] = std::make_unique<CoreEngine::RedBoxVector>(
                        filename, requested_dim, 100000);
                    state.db_mutexes[db_name] = std::make_unique<std::mutex>();
                }

                active_db = state.catalog[db_name].get();
                active_mtx = state.db_mutexes[db_name].get();
            }

            if (active_db->get_dim() != requested_dim) {
                std::cerr << "   [WARNING] Dimension mismatch! File is "
                    << active_db->get_dim() << "\n";
            }

            send(client_socket, "1", 1, 0);
            continue;
        }

        if (!active_db) break; // No DB selected — drop the client

        int current_dim = active_db->get_dim();
        int vec_byte_size = current_dim * sizeof(float);

        if (cmd == CMD_INSERT) {
            std::vector<float> vec(current_dim);
            if (!recv_all((char*)vec.data(), vec_byte_size)) break;
            { std::lock_guard<std::mutex> lk(*active_mtx); active_db->insert(meta_data, vec); }
            send(client_socket, "1", 1, 0);
        }
        else if (cmd == CMD_SEARCH) {
            std::vector<float> query(current_dim);
            if (!recv_all((char*)query.data(), vec_byte_size)) break;
            int result_id;
            { std::lock_guard<std::mutex> lk(*active_mtx); result_id = active_db->search(query); }
            send(client_socket, (char*)&result_id, 4, 0);
        }
        else if (cmd == CMD_DELETE) {
            bool success;
            { std::lock_guard<std::mutex> lk(*active_mtx); success = active_db->remove(meta_data); }
            char resp = success ? '1' : '0';
            send(client_socket, &resp, 1, 0);
        }
        else if (cmd == CMD_UPDATE) {
            std::vector<float> vec(current_dim);
            if (!recv_all((char*)vec.data(), vec_byte_size)) break;
            bool success;
            { std::lock_guard<std::mutex> lk(*active_mtx); success = active_db->update(meta_data, vec); }
            char resp = success ? '1' : '0';
            send(client_socket, &resp, 1, 0);
        }
        else if (cmd == CMD_INSERT_AUTO) {
            std::vector<float> vec(current_dim);
            if (!recv_all((char*)vec.data(), vec_byte_size)) break;
            uint64_t assigned_id;
            { std::lock_guard<std::mutex> lk(*active_mtx); assigned_id = active_db->insert_auto(vec); }
            send(client_socket, (char*)&assigned_id, sizeof(assigned_id), 0);
        }
        else if (cmd == CMD_SEARCH_N) {
            int n = static_cast<int>(meta_data);
            std::vector<float> query(current_dim);
            if (!recv_all((char*)query.data(), vec_byte_size)) break;
            std::vector<int> results;
            { std::lock_guard<std::mutex> lk(*active_mtx); results = active_db->search_N(query, n); }
            uint32_t count = static_cast<uint32_t>(results.size());
            send(client_socket, (char*)&count, sizeof(count), 0);
            if (count > 0)
                send(client_socket, (char*)results.data(), count * sizeof(int), 0);
        }
    }

    closesocket(client_socket);
    std::cout << "[SERVER] Client disconnected (thread " << std::this_thread::get_id() << ")\n";
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;

    SharedState state;

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);

    // Allow port reuse so restart doesn't give "address already in use"
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_socket, SOMAXCONN); // was 1 — allow a real backlog

    std::cout << "[SERVER] Multi-Tenant Manager Listening on Port " << PORT
        << " (multi-threaded)...\n";

    while (true) {
        SOCKET client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) continue;

        // Each client gets its own detached thread.
        // The thread owns the socket and closes it when done.
        std::thread([client_socket, &state]() {
            handle_client(client_socket, state);
            }).detach();
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