#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <unordered_map>
#include <string>
#include <cstring>

using Clock = std::chrono::high_resolution_clock;
using Ns    = std::chrono::duration<double, std::nano>;

const size_t  DIM        = 128;
const int     N_ITERS    = 100'000;
const int     CAPACITY   = 100'000;

std::mt19937 rng(42);
std::uniform_real_distribution<float> dis(0.0f, 1.0f);

struct Stats {
    double min, avg, p50, p95, p99, max;
    long long implied_cap;
};

Stats compute(std::vector<double>& t) {
    std::sort(t.begin(), t.end());
    double sum = std::accumulate(t.begin(), t.end(), 0.0);
    size_t n = t.size();
    double avg = sum / n;
    return {
        t.front(), avg,
        t[n*50/100], t[n*95/100], t[n*99/100],
        t.back(),
        (long long)(1e9 / avg)
    };
}

void print(const std::string& label, Stats s) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "--- " << label << " ---\n";
    std::cout << "  Min  : " << s.min    << " ns\n";
    std::cout << "  Avg  : " << s.avg    << " ns\n";
    std::cout << "  P50  : " << s.p50    << " ns\n";
    std::cout << "  P95  : " << s.p95    << " ns\n";
    std::cout << "  P99  : " << s.p99    << " ns\n";
    std::cout << "  Max  : " << s.max    << " ns\n";
    std::cout << "  -> Implied cap: " << s.implied_cap << " ops/sec\n\n";
}

int main() {
    std::cout << "DIM=" << DIM << " | N=" << N_ITERS << " | CAPACITY=" << CAPACITY << "\n\n";

    // Pre-generate vectors
    std::vector<std::vector<float>> vecs(N_ITERS, std::vector<float>(DIM));
    for (auto& v : vecs) for (auto& x : v) x = dis(rng);

    // -----------------------------------------------
    // TEST 1: raw memcpy into flat float buffer (mmap write baseline)
    // -----------------------------------------------
    {
        std::vector<float> float_block(CAPACITY * DIM);
        std::vector<double> times; times.reserve(N_ITERS);

        for (int i = 0; i < N_ITERS; ++i) {
            float* dst = float_block.data() + (size_t)i * DIM;
            auto t0 = Clock::now();
            std::memcpy(dst, vecs[i].data(), DIM * sizeof(float));
            auto t1 = Clock::now();
            times.push_back(Ns(t1 - t0).count());
        }
        print("Raw memcpy into flat buffer (mmap write)", compute(times));
    }

    // -----------------------------------------------
    // TEST 2: unordered_map<uint64_t, size_t> insert
    // -----------------------------------------------
    {
        std::unordered_map<uint64_t, size_t> map;
        map.reserve(CAPACITY);
        std::vector<double> times; times.reserve(N_ITERS);

        for (int i = 0; i < N_ITERS; ++i) {
            auto t0 = Clock::now();
            map[(uint64_t)i + 1] = (size_t)i;
            auto t1 = Clock::now();
            times.push_back(Ns(t1 - t0).count());
        }
        print("unordered_map<uint64_t,size_t> insert (current id_to_index)", compute(times));
    }

    // -----------------------------------------------
    // TEST 3: flat vector<int> index by ID (proposed replacement)
    // -----------------------------------------------
    {
        std::vector<int> flat(CAPACITY + 1, -1); // id_to_index[id] = slot, IDs start at 1
        std::vector<double> times; times.reserve(N_ITERS);

        for (int i = 0; i < N_ITERS; ++i) {
            auto t0 = Clock::now();
            flat[(size_t)i + 1] = i;
            auto t1 = Clock::now();
            times.push_back(Ns(t1 - t0).count());
        }
        print("flat vector<int> write by ID (proposed id_to_index replacement)", compute(times));
    }

    // -----------------------------------------------
    // TEST 4: vector<uint8_t> push_back without pre-reserve
    // -----------------------------------------------
    {
        std::vector<double> times; times.reserve(N_ITERS);

        for (int iter = 0; iter < N_ITERS; ++iter) {
            std::vector<uint8_t> flags;
            auto t0 = Clock::now();
            flags.push_back(0);
            auto t1 = Clock::now();
            times.push_back(Ns(t1 - t0).count());
        }
        // More realistic: single growing vector
        std::vector<uint8_t> flags;
        times.clear();
        for (int i = 0; i < N_ITERS; ++i) {
            auto t0 = Clock::now();
            flags.push_back(0);
            auto t1 = Clock::now();
            times.push_back(Ns(t1 - t0).count());
        }
        print("vector<uint8_t> push_back without reserve (deleted_flags)", compute(times));
    }

    // -----------------------------------------------
    // TEST 5: vector<uint8_t> push_back WITH pre-reserve
    // -----------------------------------------------
    {
        std::vector<uint8_t> flags;
        flags.reserve(CAPACITY);
        std::vector<double> times; times.reserve(N_ITERS);

        for (int i = 0; i < N_ITERS; ++i) {
            auto t0 = Clock::now();
            flags.push_back(0);
            auto t1 = Clock::now();
            times.push_back(Ns(t1 - t0).count());
        }
        print("vector<uint8_t> push_back WITH reserve (deleted_flags optimized)", compute(times));
    }

    // -----------------------------------------------
    // TEST 6: cluster_index[c].push_back without reserve
    // -----------------------------------------------
    {
        std::vector<std::vector<int>> cluster_index(10);
        std::vector<double> times; times.reserve(N_ITERS);
        std::uniform_int_distribution<int> cdis(0, 9);

        for (int i = 0; i < N_ITERS; ++i) {
            int c = cdis(rng);
            auto t0 = Clock::now();
            cluster_index[c].push_back(i);
            auto t1 = Clock::now();
            times.push_back(Ns(t1 - t0).count());
        }
        print("cluster_index[c].push_back without reserve (K=10)", compute(times));
    }

    // -----------------------------------------------
    // TEST 7: cluster_index[c].push_back WITH reserve
    // -----------------------------------------------
    {
        std::vector<std::vector<int>> cluster_index(10);
        for (auto& v : cluster_index) v.reserve(CAPACITY / 10 * 2); // 2x headroom
        std::vector<double> times; times.reserve(N_ITERS);
        std::uniform_int_distribution<int> cdis(0, 9);

        for (int i = 0; i < N_ITERS; ++i) {
            int c = cdis(rng);
            auto t0 = Clock::now();
            cluster_index[c].push_back(i);
            auto t1 = Clock::now();
            times.push_back(Ns(t1 - t0).count());
        }
        print("cluster_index[c].push_back WITH reserve (K=10, optimized)", compute(times));
    }

    // -----------------------------------------------
    // TEST 8: full simulated insert pipeline (current)
    // memcpy + unordered_map + push_back x2
    // -----------------------------------------------
    {
        std::vector<float>              float_block(CAPACITY * DIM);
        std::unordered_map<uint64_t, size_t> id_to_index;
        id_to_index.reserve(CAPACITY);
        std::vector<uint8_t>            deleted_flags;
        std::vector<std::vector<int>>   cluster_index(10);
        std::vector<double> times; times.reserve(N_ITERS);
        std::uniform_int_distribution<int> cdis(0, 9);

        for (int i = 0; i < N_ITERS; ++i) {
            float* dst = float_block.data() + (size_t)i * DIM;
            uint64_t id = (uint64_t)i + 1;
            int c = cdis(rng);
            auto t0 = Clock::now();
            std::memcpy(dst, vecs[i].data(), DIM * sizeof(float));
            id_to_index[id] = (size_t)i;
            deleted_flags.push_back(0);
            cluster_index[c].push_back(i);
            auto t1 = Clock::now();
            times.push_back(Ns(t1 - t0).count());
        }
        print("Full pipeline: current (memcpy + unordered_map + 2x push_back)", compute(times));
    }

    // -----------------------------------------------
    // TEST 9: full simulated insert pipeline (optimized)
    // memcpy + flat array + push_back x2 with reserve
    // -----------------------------------------------
    {
        std::vector<float>            float_block(CAPACITY * DIM);
        std::vector<int>              id_to_index(CAPACITY + 1, -1);
        std::vector<uint8_t>          deleted_flags;
        deleted_flags.reserve(CAPACITY);
        std::vector<std::vector<int>> cluster_index(10);
        for (auto& v : cluster_index) v.reserve(CAPACITY / 10 * 2);
        std::vector<double> times; times.reserve(N_ITERS);
        std::uniform_int_distribution<int> cdis(0, 9);

        for (int i = 0; i < N_ITERS; ++i) {
            float* dst = float_block.data() + (size_t)i * DIM;
            int c = cdis(rng);
            auto t0 = Clock::now();
            std::memcpy(dst, vecs[i].data(), DIM * sizeof(float));
            id_to_index[(size_t)i + 1] = i;
            deleted_flags.push_back(0);
            cluster_index[c].push_back(i);
            auto t1 = Clock::now();
            times.push_back(Ns(t1 - t0).count());
        }
        print("Full pipeline: optimized (memcpy + flat array + 2x reserved push_back)", compute(times));
    }

    return 0;
}