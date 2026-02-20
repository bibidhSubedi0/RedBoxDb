# RedBoxDb Benchmark Results

**Platform:** Windows x64 | **Compiler:** MSVC 19.43 | **Config:** Release  
**CPU Feature:** AVX2 enabled  
**RNG Seed:** 42 (fixed <-- results are reproducible)

---

## Configuration

| Parameter | Value |
|---|---|
| Vectors | 100,000 |
| Dimensions | 128 |
| Queries per search test | 1,000 |
| Data size | 48.8 MB |

---

## [1/5] Insert Throughput

| Metric | Value |
|---|---|
| Time | 0.175 s |
| Throughput | **570,220 vectors/sec** |
| Data written | 48.8 MB |

---

## [2/5] Search Latency <-- Single Nearest Neighbor

> Hot-cache performance (dataset paged in by Bench 1)

| Metric | Value |
|---|---|
| QPS | **224.6 queries/sec** |
| Min | 4.334 ms |
| Avg | 4.449 ms |
| P50 | 4.435 ms |
| P95 | 4.586 ms |
| **P99** | **4.887 ms** |
| Max | 5.643 ms |

---

## [3/5] Search Latency <-- Top-10 Nearest Neighbors (search_N)

| Metric | Value |
|---|---|
| K | 10 |
| QPS | **210.9 queries/sec** |
| Min | 4.579 ms |
| Avg | 4.739 ms |
| P50 | 4.716 ms |
| P95 | 4.907 ms |
| **P99** | **4.956 ms** |
| Max | 5.078 ms |

> search_N is only ~6% slower than single search <-- priority queue overhead is negligible vs scan cost.

---

## [4/5] Update Throughput <-- O(1) via id_to_index

| Metric | Value |
|---|---|
| Updates | 1,000 |
| Throughput | **341,227 updates/sec** |
| Min | 0.000 ms |
| Avg | 0.002 ms |
| P50 | 0.002 ms |
| P95 | 0.003 ms |
| **P99** | **0.003 ms** |
| Max | 0.017 ms |

> Direct hash lookup via `id_to_index` <-- no linear scan of stored vectors.

---

## [5/5] Mixed Workload <-- 70% Search / 20% Insert / 10% Delete

| Metric | Value |
|---|---|
| Total ops | 10,000 |
| Searches | 6,959 |
| Inserts | 2,043 |
| Deletes | 998 |
| Total time | 5.182 s |
| Throughput | **1,930 ops/sec** |

---

## Summary

| Operation | Throughput | P99 Latency |
|---|---|---|
| Insert (auto-ID) | 570,220 /sec | — |
| Search (top-1) | 224.6 QPS | 4.887 ms |
| Search (top-10) | 210.9 QPS | 4.956 ms |
| Update (indexed) | 341,227 /sec | 0.003 ms |
| Mixed workload | 1,930 ops/sec | — |