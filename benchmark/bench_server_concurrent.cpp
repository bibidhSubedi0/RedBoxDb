#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <random>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <csignal>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
    #define SOCKET_TYPE SOCKET
    #define CLOSE_SOCKET closesocket
    inline bool socket_valid(SOCKET_TYPE s) { return s != INVALID_SOCKET; }
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define SOCKET_TYPE int
    #define CLOSE_SOCKET close
    inline bool socket_valid(SOCKET_TYPE s) { return s >= 0; }
#endif

// ==========================================
// PROTOCOL HELPERS
// ==========================================
const uint8_t CMD_INSERT      = 1;
const uint8_t CMD_SEARCH      = 2;
const uint8_t CMD_DELETE      = 3;
const uint8_t CMD_SELECT_DB   = 4;
const uint8_t CMD_INSERT_AUTO = 6;
const uint8_t CMD_SEARCH_N    = 7;

bool send_all(SOCKET_TYPE sock, const char* buf, int len) {
    int total = 0;
    while (total < len) {
        int n = send(sock, buf + total, len - total, 0);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

bool recv_all(SOCKET_TYPE sock, char* buf, int len) {
    int total = 0;
    while (total < len) {
        int n = recv(sock, buf + total, len - total, 0);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

SOCKET_TYPE connect_to_server(const char* host, int port) {
    SOCKET_TYPE sock = socket(AF_INET, SOCK_STREAM, 0);
    if (!socket_valid(sock)) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        CLOSE_SOCKET(sock);
        return -1;
    }

    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
    return sock;
}

bool select_db(SOCKET_TYPE sock, const std::string& name, uint32_t dim, uint32_t cap) {
    uint8_t cmd = CMD_SELECT_DB;
    uint32_t name_len = name.size();
    if (!send_all(sock, (char*)&cmd, 1)) return false;
    if (!send_all(sock, (char*)&name_len, 4)) return false;
    if (!send_all(sock, name.c_str(), (int)name_len)) return false;
    if (!send_all(sock, (char*)&dim, 4)) return false;
    if (!send_all(sock, (char*)&cap, 4)) return false;
    char ack;
    if (!recv_all(sock, &ack, 1)) return false;
    return ack == '1';
}

bool insert_vec(SOCKET_TYPE sock, uint64_t id, const std::vector<float>& vec) {
    uint8_t cmd = CMD_INSERT;
    if (!send_all(sock, (char*)&cmd, 1)) return false;
    uint32_t meta = (uint32_t)id;
    if (!send_all(sock, (char*)&meta, 4)) return false;
    if (!send_all(sock, (char*)vec.data(), (int)(vec.size() * sizeof(float)))) return false;
    char ack;
    if (!recv_all(sock, &ack, 1)) return false;
    return ack == '1';
}

int search_vec(SOCKET_TYPE sock, const std::vector<float>& vec) {
    uint8_t cmd = CMD_SEARCH;
    uint32_t meta = 0;
    if (!send_all(sock, (char*)&cmd, 1)) return -2;
    if (!send_all(sock, (char*)&meta, 4)) return -2;
    if (!send_all(sock, (char*)vec.data(), (int)(vec.size() * sizeof(float)))) return -2;
    int result_id;
    if (!recv_all(sock, (char*)&result_id, 4)) return -2;
    return result_id;
}

// ==========================================
// BENCHMARK
// ==========================================
using Clock = std::chrono::high_resolution_clock;

std::mt19937 rng(42);
std::uniform_real_distribution<float> dis(0.0f, 1.0f);

std::vector<float> rand_vec(size_t dim) {
    std::vector<float> v(dim);
    for (auto& x : v) x = dis(rng);
    return v;
}

struct Stats {
    double min, avg, p50, p95, p99, max;
};

Stats compute_stats(std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    size_t n = samples.size();
    return { samples.front(), sum / n, samples[n*50/100], samples[n*95/100], samples[n*99/100], samples.back() };
}

void print_stats(const Stats& s) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "     Min  : " << s.min << " ms\n";
    std::cout << "     Avg  : " << s.avg << " ms\n";
    std::cout << "     P50  : " << s.p50 << " ms\n";
    std::cout << "     P95  : " << s.p95 << " ms\n";
    std::cout << "     P99  : " << s.p99 << " ms\n";
    std::cout << "     Max  : " << s.max << " ms\n";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#else
    // Ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif

    const char* host = "127.0.0.1";
    int port = 8080;
    int num_clients = 8;
    int ops_per_client = 500;
    size_t dim = 128;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i+1 < argc) port = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--clients") == 0 && i+1 < argc) num_clients = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--ops") == 0 && i+1 < argc) ops_per_client = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--dim") == 0 && i+1 < argc) dim = std::atoi(argv[++i]);
        else {
            std::cerr << "Usage: " << argv[0] << " [--port P] [--clients N] [--ops N] [--dim D]\n";
            return 1;
        }
    }

    std::cout << "===============================================\n";
    std::cout << "   RedBoxDb CONCURRENT SERVER BENCHMARK\n";
    std::cout << "===============================================\n";
    std::cout << "   Server    : " << host << ":" << port << "\n";
    std::cout << "   Clients   : " << num_clients << "\n";
    std::cout << "   Ops/client: " << ops_per_client << "\n";
    std::cout << "   Dimension : " << dim << "\n";
    std::cout << "===============================================\n\n";

    // Verify server is reachable
    SOCKET_TYPE test_sock = connect_to_server(host, port);
    if (!socket_valid(test_sock)) {
        std::cerr << "   [ERROR] Cannot connect to server at " << host << ":" << port << "\n";
        std::cerr << "   Make sure the server is running: ./RedBoxServer\n";
        return 1;
    }
    CLOSE_SOCKET(test_sock);
    std::cout << "   Server connection verified.\n\n";

    // --- PHASE 1: Multi-client search QPS ---
    {
        std::cout << "[1] MULTI-CLIENT SEARCH QPS\n";
        std::cout << "-----------------------------------------------\n";

        // Pre-populate DB via one connection
        {
            SOCKET_TYPE setup_sock = connect_to_server(host, port);
            if (!socket_valid(setup_sock)) { std::cerr << "Failed to connect\n"; return 1; }

            select_db(setup_sock, "bench_concurrent", (uint32_t)dim, 200'000);

            std::cout << "   Inserting 100k vectors...\n";
            for (int i = 0; i < 100'000; ++i) {
                auto v = rand_vec(dim);
                if (!insert_vec(setup_sock, i + 1, v)) {
                    std::cerr << "Insert failed at " << i << "\n";
                    break;
                }
            }
            CLOSE_SOCKET(setup_sock);
        }
        std::cout << "   Setup complete.\n";

        // Warmup: 100 searches from a single connection
        {
            SOCKET_TYPE warmup_sock = connect_to_server(host, port);
            if (socket_valid(warmup_sock)) {
                select_db(warmup_sock, "bench_concurrent", (uint32_t)dim, 200'000);
                for (int i = 0; i < 100; ++i)
                    search_vec(warmup_sock, rand_vec(dim));
                CLOSE_SOCKET(warmup_sock);
            }
        }

        // Spawn concurrent clients doing search
        std::atomic<int64_t> total_ops{0};
        std::atomic<int64_t> total_errors{0};
        std::vector<std::vector<double>> per_client_latencies(num_clients);

        auto bench_start = Clock::now();

        std::vector<std::thread> threads;
        for (int c = 0; c < num_clients; ++c) {
            threads.emplace_back([&, c]() {
                SOCKET_TYPE sock = connect_to_server(host, port);
                if (!socket_valid(sock)) { total_errors++; return; }

                select_db(sock, "bench_concurrent", (uint32_t)dim, 200'000);

                per_client_latencies[c].reserve(ops_per_client);

                for (int op = 0; op < ops_per_client; ++op) {
                    auto q = rand_vec(dim);
                    auto t0 = Clock::now();
                    int result = search_vec(sock, q);
                    auto t1 = Clock::now();

                    per_client_latencies[c].push_back(
                        std::chrono::duration<double, std::milli>(t1 - t0).count());

                    if (result < -1) total_errors++;
                    total_ops++;
                }

                CLOSE_SOCKET(sock);
            });
        }

        for (auto& th : threads) th.join();
        auto bench_end = Clock::now();

        double bench_secs = std::chrono::duration<double>(bench_end - bench_start).count();

        // Merge all latencies
        std::vector<double> all_latencies;
        for (auto& lats : per_client_latencies)
            all_latencies.insert(all_latencies.end(), lats.begin(), lats.end());

        Stats s = compute_stats(all_latencies);

        std::cout << "\n   Results:\n";
        std::cout << "   Total ops  : " << total_ops.load() << "\n";
        std::cout << "   Errors     : " << total_errors.load() << "\n";
        std::cout << "   Wall time  : " << std::fixed << std::setprecision(3) << bench_secs << " s\n";
        std::cout << "   Throughput : " << std::setprecision(0) << (total_ops.load() / bench_secs) << " queries/sec\n";
        print_stats(s);

        // Per-client breakdown
        std::cout << "\n   Per-client QPS:\n";
        for (int c = 0; c < num_clients; ++c) {
            double client_secs = std::accumulate(
                per_client_latencies[c].begin(), per_client_latencies[c].end(), 0.0) / 1000.0;
            double client_qps = per_client_latencies[c].size() / client_secs;
            std::cout << "     Client " << c << ": " << std::setprecision(0) << client_qps << " qps\n";
        }
    }

    // --- PHASE 2: Mixed workload (search + insert + delete) on SHARED DB ---
    {
        std::cout << "\n[2] MULTI-CLIENT MIXED WORKLOAD (70% search, 20% insert, 10% delete) — SHARED DB\n";
        std::cout << "-----------------------------------------------\n";

        // Pre-populate shared DB
        {
            SOCKET_TYPE setup_sock = connect_to_server(host, port);
            if (!socket_valid(setup_sock)) { std::cerr << "Failed to connect\n"; return 1; }

            select_db(setup_sock, "bench_mixed_shared", (uint32_t)dim, 200'000);

            std::cout << "   Inserting 10k vectors into shared DB...\n";
            for (int i = 0; i < 10'000; ++i) {
                auto v = rand_vec(dim);
                if (!insert_vec(setup_sock, i + 1, v)) {
                    std::cerr << "Insert failed at " << i << "\n";
                    break;
                }
            }
            CLOSE_SOCKET(setup_sock);
        }
        std::cout << "   Setup complete.\n";

        std::atomic<int64_t> total_ops{0};
        std::atomic<int64_t> search_ops{0};
        std::atomic<int64_t> insert_ops{0};
        std::atomic<int64_t> delete_ops{0};
        std::atomic<int64_t> total_errors{0};
        std::atomic<uint64_t> insert_id_counter{10'001};
        std::vector<double> all_latencies;

        // Warmup
        {
            SOCKET_TYPE warmup_sock = connect_to_server(host, port);
            if (socket_valid(warmup_sock)) {
                select_db(warmup_sock, "bench_mixed_shared", (uint32_t)dim, 200'000);
                for (int i = 0; i < 100; ++i)
                    search_vec(warmup_sock, rand_vec(dim));
                CLOSE_SOCKET(warmup_sock);
            }
        }

        auto bench_start = Clock::now();

        std::vector<std::thread> threads;
        for (int c = 0; c < num_clients; ++c) {
            threads.emplace_back([&, c]() {
                SOCKET_TYPE sock = connect_to_server(host, port);
                if (!socket_valid(sock)) { total_errors++; return; }

                select_db(sock, "bench_mixed_shared", (uint32_t)dim, 200'000);

                std::mt19937 local_rng(c * 1000);
                std::uniform_int_distribution<int> op_dis(1, 10);
                std::uniform_int_distribution<uint64_t> id_dis(1, 10'000);

                for (int op = 0; op < ops_per_client; ++op) {
                    int op_type = op_dis(local_rng);
                    auto t0 = Clock::now();

                    if (op_type <= 7) {
                        // Search
                        (void)search_vec(sock, rand_vec(dim));
                        search_ops++;
                    } else if (op_type <= 9) {
                        // Insert with unique ID
                        uint64_t new_id = insert_id_counter.fetch_add(1);
                        insert_vec(sock, new_id, rand_vec(dim));
                        insert_ops++;
                    } else {
                        // Delete
                        uint8_t cmd = CMD_DELETE;
                        uint32_t meta = (uint32_t)id_dis(local_rng);
                        if (send_all(sock, (char*)&cmd, 1) && send_all(sock, (char*)&meta, 4)) {
                            char ack;
                            recv_all(sock, &ack, 1);
                        }
                        delete_ops++;
                    }

                    auto t1 = Clock::now();
                    all_latencies.push_back(
                        std::chrono::duration<double, std::milli>(t1 - t0).count());
                    total_ops++;
                }

                CLOSE_SOCKET(sock);
            });
        }

        for (auto& th : threads) th.join();
        auto bench_end = Clock::now();

        double bench_secs = std::chrono::duration<double>(bench_end - bench_start).count();
        Stats s = compute_stats(all_latencies);

        std::cout << "   Total ops  : " << total_ops.load() << "\n";
        std::cout << "   Breakdown  : " << search_ops.load() << " search | "
                  << insert_ops.load() << " insert | " << delete_ops.load() << " delete\n";
        std::cout << "   Errors     : " << total_errors.load() << "\n";
        std::cout << "   Wall time  : " << std::fixed << std::setprecision(3) << bench_secs << " s\n";
        std::cout << "   Throughput : " << std::setprecision(0) << (total_ops.load() / bench_secs) << " ops/sec\n";
        print_stats(s);
    }

    // Cleanup
    {
        SOCKET_TYPE sock = connect_to_server(host, port);
        if (socket_valid(sock)) {
            uint8_t cmd = 8; // CMD_DROP_DB
            uint32_t meta = 0;
            char ack;

            select_db(sock, "bench_concurrent", (uint32_t)dim, 200'000);
            send_all(sock, (char*)&cmd, 1);
            send_all(sock, (char*)&meta, 4);
            recv_all(sock, &ack, 1);

            select_db(sock, "bench_mixed_shared", (uint32_t)dim, 200'000);
            send_all(sock, (char*)&cmd, 1);
            send_all(sock, (char*)&meta, 4);
            recv_all(sock, &ack, 1);

            CLOSE_SOCKET(sock);
        }
    }

    std::cout << "\n===============================================\n";
    std::cout << "   CONCURRENT SERVER BENCHMARK COMPLETE\n";
    std::cout << "===============================================\n";

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
