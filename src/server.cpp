#include <iostream>
#include <vector>
#include <string>
#include <exception>
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif
#include <unordered_map>
#include <memory>
#include <thread>
#include <mutex>
#include "redboxdb/engine.hpp"
#include "redboxdb/metadata_store.hpp"
#include <filesystem>
#include <cstring>
#include <cctype>
#include <cstdlib>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifdef _MSC_VER
        #pragma comment(lib, "Ws2_32.lib")
    #endif
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <signal.h>
    #define SOCKET int
    #define INVALID_SOCKET (-1)
    #define closesocket close
#endif

const int PORT = 8080;

// --- PROTOCOL ---
const uint8_t CMD_INSERT = 1;
const uint8_t CMD_SEARCH = 2;
const uint8_t CMD_DELETE = 3;
const uint8_t CMD_SELECT_DB = 4;
const uint8_t CMD_UPDATE = 5;
const uint8_t CMD_INSERT_AUTO = 6;
const uint8_t CMD_SEARCH_N = 7;
const uint8_t CMD_DROP_DB  = 8;
const uint8_t CMD_SET_PROBES = 9;
const uint8_t CMD_CREATE_HNSW_DB = 10;
const uint8_t CMD_SET_HNSW_EF = 11;
const uint8_t CMD_LIST_DBS = 12;
const uint8_t CMD_DB_INFO = 13;

constexpr size_t MAX_DB_NAME_LEN = 64;
inline bool is_valid_db_name(const std::string& name) {
    if (name.empty() || name.size() > MAX_DB_NAME_LEN) return false;
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) return false;
    }
    return true;
}


using DbCatalog = std::unordered_map<std::string, std::unique_ptr<CoreEngine::RedBoxVector>>;
using MutexMap = std::unordered_map<std::string, std::unique_ptr<std::mutex>>;

struct SharedState {
    DbCatalog  catalog;
    MutexMap   db_mutexes;
    std::mutex catalog_mutex;
    Metadata::Store* meta = nullptr;
};

// -----------------------------------------------------------------------
// handle_client � runs on its own thread, owns the client socket lifetime
// -----------------------------------------------------------------------
#ifdef _WIN32
void handle_client(SOCKET client_socket, SharedState& state) {
#else
void handle_client(int client_socket, SharedState& state) {
#endif
    std::cout << "[SERVER] Client connected (thread " << std::this_thread::get_id() << ")\n";

    try {

    CoreEngine::RedBoxVector* active_db = nullptr;
    std::mutex* active_mtx = nullptr;
    std::string active_db_name;

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

    auto send_all = [&](const char* buf, int len) -> bool {
        int total = 0;
        while (total < len) {
            int n = send(client_socket, buf + total, len - total, 0);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                return false;
            }
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
            if (name_len > MAX_DB_NAME_LEN) {
                std::cerr << "   [REJECTED] name_len=" << name_len << " exceeds limit\n";
                break;
            }
            std::string db_name(name_len, ' ');

            if (!recv_all(&db_name[0], (int)name_len)) break;

            if (!is_valid_db_name(db_name)) {
                std::cerr << "   [REJECTED] Invalid db_name: " << db_name << "\n";
                char zero = 0;
                if (!send_all(&zero, 1)) break;
                continue;
            }

            uint32_t requested_dim = 0;
            if (!recv_all((char*)&requested_dim, 4)) break;

            uint32_t requested_capacity = 0;
            if (!recv_all((char*)&requested_capacity, 4)) break;

            std::cout << "[SERVER] Req DB: " << db_name
                << " (Dim: " << requested_dim << ")\n";
            std::cout << "[SERVER] Req Capacity: " << db_name
                << " (Capacity: " << requested_capacity << ")\n";

            {
                // Lock catalog only long enough to load/create the entry
                std::lock_guard<std::mutex> lock(state.catalog_mutex);

                if (state.catalog.find(db_name) == state.catalog.end()) {
                    std::cout << "   -> New/Loading...\n";
                    std::string filename = db_name + ".db";
                    state.catalog[db_name] = std::make_unique<CoreEngine::RedBoxVector>(
                        filename, requested_dim, (int)requested_capacity);
                    state.db_mutexes[db_name] = std::make_unique<std::mutex>();

                    if (state.meta) {
                        state.meta->create_database(db_name, requested_dim,
                            CoreEngine::IndexType::IVF, requested_capacity,
                            *state.catalog[db_name]->get_header());
                    }
                }

                active_db = state.catalog[db_name].get();
                active_mtx = state.db_mutexes[db_name].get();
                active_db_name = db_name;
            }

            if (active_db->get_dim() != requested_dim) {
                std::cerr << "   [WARNING] Dimension mismatch! File is "
                    << active_db->get_dim() << "\n";
            }

            if (!send_all("1", 1)) break;
            continue;
        }

        // --- HANDSHAKE / CREATE HNSW DB ---
        if (cmd == CMD_CREATE_HNSW_DB) {
            uint32_t name_len = meta_data;
            if (name_len > MAX_DB_NAME_LEN) {
                std::cerr << "   [REJECTED] name_len=" << name_len << " exceeds limit\n";
                break;
            }
            std::string db_name(name_len, ' ');
            if (!recv_all(&db_name[0], (int)name_len)) break;

            if (!is_valid_db_name(db_name)) {
                std::cerr << "   [REJECTED] Invalid db_name: " << db_name << "\n";
                char zero = 0;
                if (!send_all(&zero, 1)) break;
                continue;
            }

            uint32_t requested_dim = 0;
            if (!recv_all((char*)&requested_dim, 4)) break;
            uint32_t requested_capacity = 0;
            if (!recv_all((char*)&requested_capacity, 4)) break;
            uint8_t hnsw_M = 16;
            if (!recv_all((char*)&hnsw_M, 1)) break;
            uint16_t hnsw_ef_construction = 200;
            if (!recv_all((char*)&hnsw_ef_construction, 2)) break;

            std::cout << "[SERVER] Create HNSW DB: " << db_name
                << " (Dim=" << requested_dim << " M=" << (int)hnsw_M
                << " ef_c=" << hnsw_ef_construction << ")\n";

            {
                std::lock_guard<std::mutex> lock(state.catalog_mutex);
                if (state.catalog.find(db_name) == state.catalog.end()) {
                    std::string filename = db_name + ".db";
                    state.catalog[db_name] = std::make_unique<CoreEngine::RedBoxVector>(
                        filename, requested_dim, (int)requested_capacity,
                        hnsw_M, hnsw_ef_construction);
                    state.db_mutexes[db_name] = std::make_unique<std::mutex>();

                    if (state.meta) {
                        state.meta->create_database(db_name, requested_dim,
                            CoreEngine::IndexType::HNSW, requested_capacity,
                            *state.catalog[db_name]->get_header());
                    }
                }
                active_db = state.catalog[db_name].get();
                active_mtx = state.db_mutexes[db_name].get();
                active_db_name = db_name;
            }

            if (!send_all("1", 1)) break;
            continue;
        }

        if (!active_db) break;

        int current_dim = active_db->get_dim();
        int vec_byte_size = current_dim * sizeof(float);

        if (cmd == CMD_INSERT) {
            std::vector<float> vec(current_dim);
            if (!recv_all((char*)vec.data(), vec_byte_size)) break;
            { std::lock_guard<std::mutex> lk(*active_mtx); active_db->insert(meta_data, vec); }
            if (state.meta) {
                state.meta->update_counts(active_db_name, active_db->get_count(), active_db->get_next_id());
                state.meta->log_operation(active_db_name, "INSERT", meta_data);
            }
            if (!send_all("1", 1)) break;
        }
        else if (cmd == CMD_SEARCH) {
            std::vector<float> query(current_dim);
            if (!recv_all((char*)query.data(), vec_byte_size)) break;
            int result_id;
            { std::lock_guard<std::mutex> lk(*active_mtx); result_id = active_db->search(query); }
            if (state.meta) {
                state.meta->log_operation(active_db_name, "SEARCH", 0);
            }
            if (!send_all((char*)&result_id, 4)) break;
        }
        else if (cmd == CMD_DELETE) {
            bool success;
            { std::lock_guard<std::mutex> lk(*active_mtx); success = active_db->remove(meta_data); }
            if (success && state.meta) {
                state.meta->update_counts(active_db_name, active_db->get_count(), active_db->get_next_id());
                state.meta->log_operation(active_db_name, "DELETE", meta_data);
            }
            char resp = success ? '1' : '0';
            if (!send_all(&resp, 1)) break;
        }
        else if (cmd == CMD_UPDATE) {
            std::vector<float> vec(current_dim);
            if (!recv_all((char*)vec.data(), vec_byte_size)) break;
            bool success;
            { std::lock_guard<std::mutex> lk(*active_mtx); success = active_db->update(meta_data, vec); }
            if (success && state.meta) {
                state.meta->log_operation(active_db_name, "UPDATE", meta_data);
            }
            char resp = success ? '1' : '0';
            if (!send_all(&resp, 1)) break;
        }
        else if (cmd == CMD_INSERT_AUTO) {
            std::vector<float> vec(current_dim);
            if (!recv_all((char*)vec.data(), vec_byte_size)) break;
            uint64_t assigned_id;
            { std::lock_guard<std::mutex> lk(*active_mtx); assigned_id = active_db->insert_auto(vec); }
            if (state.meta) {
                state.meta->update_counts(active_db_name, active_db->get_count(), active_db->get_next_id());
                state.meta->log_operation(active_db_name, "INSERT_AUTO", assigned_id);
            }
            if (!send_all((char*)&assigned_id, sizeof(assigned_id))) break;
        }
        else if (cmd == CMD_SEARCH_N) {
            int n = static_cast<int>(meta_data);
            std::vector<float> query(current_dim);
            if (!recv_all((char*)query.data(), vec_byte_size)) break;
            if (n <= 0) {
                // Bad input — send zero results, don't crash
                uint32_t count = 0;
                if (!send_all((char*)&count, sizeof(count))) break;
            } else {
                std::vector<int> results;
                { std::lock_guard<std::mutex> lk(*active_mtx); results = active_db->search_N(query, n); }
                uint32_t count = static_cast<uint32_t>(results.size());
                if (!send_all((char*)&count, sizeof(count))) break;
                if (count > 0)
                    if (!send_all((char*)results.data(), count * sizeof(int))) break;
            }
        }
        else if (cmd == CMD_DROP_DB) {
            bool success = false;
            std::string db_to_drop;

            // active_db is already selected; grab its name from the catalog
            {
                std::lock_guard<std::mutex> lock(state.catalog_mutex);
                for (auto& [name, db_ptr] : state.catalog) {
                    if (db_ptr.get() == active_db) {
                        db_to_drop = name;
                        break;
                    }
                }
                if (!db_to_drop.empty()) {
                    if (state.meta) {
                        state.meta->drop_database(db_to_drop);
                    }
                    state.catalog.erase(db_to_drop);
                    state.db_mutexes.erase(db_to_drop);
                    std::filesystem::remove(db_to_drop + ".db");
                    std::filesystem::remove(db_to_drop + ".db.del");
                    active_db  = nullptr;
                    active_mtx = nullptr;
                    active_db_name.clear();
                    success    = true;
                }
            }

            char resp = success ? '1' : '0';
            if (!send_all(&resp, 1)) break;
        }
        else if (cmd == CMD_SET_PROBES) {
            uint8_t new_probes = static_cast<uint8_t>(meta_data);
            if (active_db && new_probes > 0) {
                active_db->set_num_probes(new_probes);
                std::cout << "[SERVER] Set num_probes = " << (int)new_probes << "\n";
            }
            char resp = '1';
            if (!send_all(&resp, 1)) break;
        }
        else if (cmd == CMD_SET_HNSW_EF) {
            uint16_t new_ef = static_cast<uint16_t>(meta_data);
            if (active_db) {
                active_db->set_hnsw_ef_search(new_ef);
                std::cout << "[SERVER] Set hnsw_ef_search = " << (int)new_ef << "\n";
            }
            char resp = '1';
            if (!send_all(&resp, 1)) break;
        }
        else if (cmd == CMD_LIST_DBS) {
            if (!state.meta) {
                uint32_t count = 0;
                if (!send_all((char*)&count, sizeof(count))) break;
            } else {
                std::vector<Metadata::DbInfo> dbs;
                state.meta->list_databases(dbs);
                uint32_t count = static_cast<uint32_t>(dbs.size());
                if (!send_all((char*)&count, sizeof(count))) break;
                for (auto& db : dbs) {
                    uint8_t name_len = static_cast<uint8_t>(db.name.size());
                    if (!send_all((char*)&name_len, 1)) break;
                    if (!send_all(db.name.c_str(), name_len)) break;
                    if (!send_all((char*)&db.dimensions, sizeof(db.dimensions))) break;
                    uint8_t idx = static_cast<uint8_t>(db.index_type);
                    if (!send_all((char*)&idx, 1)) break;
                    if (!send_all((char*)&db.vector_count, sizeof(db.vector_count))) break;
                }
            }
            continue;
        }
        else if (cmd == CMD_DB_INFO) {
            if (!active_db || !state.meta) {
                char zero = 0;
                if (!send_all(&zero, 1)) break;
            } else {
                char ok = 1;
                if (!send_all(&ok, 1)) break;
                uint64_t vc = active_db->get_count();
                uint64_t cap = active_db->get_header()->max_capacity;
                uint64_t nid = active_db->get_next_id();
                uint8_t idx = static_cast<uint8_t>(active_db->get_index_type());
                uint32_t dim = active_db->get_dim();
                if (!send_all((char*)&vc, sizeof(vc))) break;
                if (!send_all((char*)&cap, sizeof(cap))) break;
                if (!send_all((char*)&nid, sizeof(nid))) break;
                if (!send_all((char*)&idx, 1)) break;
                if (!send_all((char*)&dim, sizeof(dim))) break;
            }
            continue;
        }
        else {
            std::cerr << "[SERVER] Unknown cmd=" << (int)cmd << " — sending error response\n";
            char resp = '0';
            if (!send_all(&resp, 1)) break;
        }
    }

    } catch (const std::exception& e) {
        std::cerr << "[SERVER] Client handler exception (thread " << std::this_thread::get_id() << "): " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[SERVER] Client handler unknown exception (thread " << std::this_thread::get_id() << ")\n";
    }

#ifdef _WIN32
    closesocket(client_socket);
#else
    close(client_socket);
#endif
    std::cout << "[SERVER] Client disconnected (thread " << std::this_thread::get_id() << ")\n";
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    SharedState state;

    // --- PostgreSQL metadata store (optional) ---
    const char* pg_host     = std::getenv("REDBOX_PG_HOST");
    const char* pg_port     = std::getenv("REDBOX_PG_PORT");
    const char* pg_dbname   = std::getenv("REDBOX_PG_DBNAME");
    const char* pg_user     = std::getenv("REDBOX_PG_USER");
    const char* pg_password = std::getenv("REDBOX_PG_PASSWORD");
    const char* pg_data_dir = std::getenv("REDBOX_DATA_DIR");
    std::string data_dir = pg_data_dir ? pg_data_dir : ".";

    if (pg_host && pg_host[0] != '\0') {
        std::string conninfo = "host=" + std::string(pg_host)
                             + " port=" + std::string(pg_port ? pg_port : "5432")
                             + " dbname=" + std::string(pg_dbname ? pg_dbname : "redbox")
                             + " user=" + std::string(pg_user ? pg_user : "redbox")
                             + " password=" + std::string(pg_password ? pg_password : "");
        try {
            state.meta = new Metadata::Store(conninfo, 8);
            state.meta->run_migrations("sql/schema.sql");
            state.meta->seed_from_files(data_dir);

            std::vector<Metadata::DbInfo> dbs;
            state.meta->list_databases(dbs);
            for (auto& db : dbs) {
                CoreEngine::SpecificMetadata params;
                state.meta->load_database(db.name, params);
                std::string filename = data_dir + "/" + db.name + ".db";
                std::lock_guard<std::mutex> lock(state.catalog_mutex);
                if (params.index_type == static_cast<uint8_t>(CoreEngine::IndexType::HNSW)) {
                    state.catalog[db.name] = std::make_unique<CoreEngine::RedBoxVector>(
                        filename, params.dimensions, (int)params.max_capacity,
                        params.hnsw_M, params.hnsw_ef_construction);
                } else {
                    state.catalog[db.name] = std::make_unique<CoreEngine::RedBoxVector>(
                        filename, params.dimensions, (int)params.max_capacity,
                        params.num_clusters, params.num_probes);
                }
                state.db_mutexes[db.name] = std::make_unique<std::mutex>();
                std::cout << "[SERVER] Loaded DB from metadata: " << db.name
                          << " (dim=" << db.dimensions << " count=" << db.vector_count << ")\n";
            }
            std::cout << "[SERVER] PostgreSQL metadata store connected.\n";
        } catch (const std::exception& e) {
            std::cerr << "[SERVER] WARNING: Failed to connect to PostgreSQL: " << e.what() << "\n";
            std::cerr << "[SERVER] Running without metadata persistence.\n";
            state.meta = nullptr;
        }
    } else {
        std::cout << "[SERVER] REDBOX_PG_HOST not set. Running without metadata persistence.\n";
    }

#ifdef _WIN32
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
#else
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
#endif

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
#ifdef _WIN32
        SOCKET client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) continue;
#else
        int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket < 0) continue;
#endif

        // Disable Nagle on the accepted socket to avoid ~40ms delayed-ACK latency
        int flag = 1;
        setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));

        // Each client gets its own detached thread.
        // The thread owns the socket and closes it when done.
        try {
            std::thread([client_socket, &state]() {
                handle_client(client_socket, state);
            }).detach();
        } catch (const std::exception& e) {
            std::cerr << "[SERVER] Failed to create thread: " << e.what() << "\n";
            closesocket(client_socket);
        }
    }

    closesocket(server_socket);
    delete state.meta;
#ifdef _WIN32
    WSACleanup();
#endif
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
    1        INSERT        Vector ID        Raw Float Data(Dim*4 bytes)          1 (Ack)
    2        SEARCH        (Ignored)        Raw Float Data (Dim*4 bytes)        Result ID (4 bytes)
    3        DELETE        Vector ID        (None)                               1 or 0 (Success/Fail)
    4        SELECT_DB     Name Length      Database Name (UTF-8 String)         1 (Ack)
    5        UPDATE        Vector ID        Raw Float Data (Dim*4 bytes)        1 or 0 (Success/Fail)
    6        INSERT_AUTO   (Ignored)        Raw Float Data (Dim*4 bytes)        Assigned ID (8 bytes)
    7        SEARCH_N      N (count)        Raw Float Data (Dim*4 bytes)        Count(4) + IDs(4*N)
    8        DROP_DB       (Ignored)        (None)                               1 or 0
    9        SET_PROBES    Probe count      (None)                               1 (Ack)
    10       CREATE_HNSW   Name Length      Name + Dim(4) + Cap(4) + M(1) +     1 (Ack)
                                            ef_c(2)
    11       SET_HNSW_EF   ef value         (None)                               1 (Ack)
    12       LIST_DBS      (Ignored)        (None)                               Count(4) + Entries
    13       DB_INFO       (Ignored)        (None)                               OK(1) + VC(8)+Cap(8)+NID(8)+Type(1)+Dim(4)
*/