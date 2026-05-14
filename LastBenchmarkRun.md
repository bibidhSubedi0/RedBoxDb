# RedBoxDb Benchmark Results

**Platform:** Windows x64 | **Compiler:** MinGW g++ | **Config:** Release
**CPU Feature:** AVX2 enabled
**Threads:** 12
**Indexing:** K-Means Clustering (K=10, Probes=1)
**RNG Seed:** 42 (fixed <-- results are reproducible)

---

## Configuration

| Parameter               | Value   |
| ----------------------- | ------- |
| Vectors                 | 100,000 |
| Dimensions              | 128     |
| Queries per search test | 1,000   |
| Data size               | 48.8 MB |

---

## [1/6] Insert Throughput

| Metric       | Value                      |
| ------------ | -------------------------- |
| Time         | 0.099 s                    |
| Throughput   | **~1,010,000 vectors/sec** |
| Data written | 48.8 MB                    |

> RNG generation excluded from timing.

---

## [2/6] Search Latency <-- Single Nearest Neighbor

> Hot-cache performance (dataset paged in by Bench 1)

| Metric  | Value                  |
| ------- | ---------------------- |
| QPS     | **~1,140 queries/sec** |
| Min     | 0.685 ms               |
| Avg     | 0.874 ms               |
| P50     | 0.821 ms               |
| P95     | 1.163 ms               |
| **P99** | **~1.46 ms**           |
| Max     | 2.106 ms               |

---

## [3/6] Search Latency <-- Top-10 Nearest Neighbors (search_N)

| Metric  | Value                  |
| ------- | ---------------------- |
| K       | 10                     |
| QPS     | **~1,062 queries/sec** |
| Min     | 0.708 ms               |
| Avg     | 0.939 ms               |
| P50     | 0.867 ms               |
| P95     | 1.325 ms               |
| **P99** | **~1.61 ms**           |
| Max     | 2.202 ms               |

> search_N is only ~7% slower than single search on average <-- priority queue overhead remains negligible vs scan cost.

---

## [4/6] Update Throughput <-- O(1) via id_to_index

| Metric     | Value                    |
| ---------- | ------------------------ |
| Updates    | 1,000                    |
| Throughput | **~276,000 updates/sec** |
| Min        | 0.000 ms                 |
| Avg        | 0.003 ms                 |
| P50        | 0.003 ms                 |
| P95        | 0.004 ms                 |
| **P99**    | **~0.005 ms**            |
| Max        | 0.017 ms                 |

> Direct hash lookup via `id_to_index` <-- no linear scan of stored vectors.

---

## [5/6] Mixed Workload <-- 70% Search / 20% Insert / 10% Delete

| Metric     | Value              |
| ---------- | ------------------ |
| Total ops  | 10,000             |
| Searches   | 6,959              |
| Inserts    | 2,043              |
| Deletes    | 998                |
| Total time | ~1.24 s            |
| Throughput | **~8,040 ops/sec** |

---

## [6/6] Search Under Heavy Deletion <-- 40% of DB Deleted

| Metric           | Value                  |
| ---------------- | ---------------------- |
| Inserted vectors | 100,000                |
| Deleted vectors  | 40,000                 |
| Live rows        | 60,000                 |
| QPS              | **~1,980 queries/sec** |
| Min              | 0.404 ms               |
| Avg              | 0.502 ms               |
| P50              | 0.482 ms               |
| P95              | 0.609 ms               |
| **P99**          | **~0.86 ms**           |
| Max              | 1.198 ms               |

> Compare against Bench 2 to estimate deleted_flags overhead under heavy tombstoning.

---

## Summary

| Operation             | Throughput      | P99 Latency |
| --------------------- | --------------- | ----------- |
| Insert                | ~1,010,000 /sec | —           |
| Search (top-1)        | ~1,140 QPS      | ~1.46 ms    |
| Search (top-10)       | ~1,062 QPS      | ~1.61 ms    |
| Update (indexed)      | ~276,000 /sec   | ~0.005 ms   |
| Mixed workload        | ~8,040 ops/sec  | —           |
| Heavy deletion search | ~1,980 QPS      | ~0.86 ms    |
