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
| Time | 0.099 s |
| Throughput | **~1,000,000 vectors/sec** |
| Data written | 48.8 MB |

---

## [2/5] Search Latency <-- Single Nearest Neighbor

> Hot-cache performance (dataset paged in by Bench 1)

| Metric | Value |
|---|---|
| QPS | **~310 queries/sec** |
| Min | 2.893 ms |
| Avg | 3.154 ms |
| P50 | 3.090 ms |
| P95 | 3.520 ms |
| **P99** | **~4.1 ms** |
| Max | 5.002 ms |

---

## [3/5] Search Latency <-- Top-10 Nearest Neighbors (search_N)

| Metric | Value |
|---|---|
| K | 10 |
| QPS | **~293 queries/sec** |
| Min | 2.987 ms |
| Avg | 3.410 ms |
| P50 | 3.269 ms |
| P95 | 3.971 ms |
| **P99** | **~4.6 ms (see note)** |
| Max | 14.692 ms |

> search_N is only ~7% slower than single search on average <-- priority queue overhead is negligible vs scan cost.  
> **Note:** Search_N P99 tail latency is unstable across runs, observed spiking to 7-14ms. Median and average are stable. Root cause unknown — flagged for investigation.

---

## [4/5] Update Throughput <-- O(1) via id_to_index

| Metric | Value |
|---|---|
| Updates | 1,000 |
| Throughput | **~400,000 updates/sec** |
| Min | 0.000 ms |
| Avg | 0.002 ms |
| P50 | 0.001 ms |
| P95 | 0.003 ms |
| **P99** | **~0.004 ms** |
| Max | 0.107 ms |

> Direct hash lookup via `id_to_index` <-- no linear scan of stored vectors.

---

## [5/5] Mixed Workload <-- 70% Search / 20% Insert / 10% Delete

| Metric | Value |
|---|---|
| Total ops | 10,000 |
| Searches | 6,959 |
| Inserts | 2,043 |
| Deletes | 998 |
| Total time | ~3.4 s |
| Throughput | **~3,000 ops/sec** |

---

## Summary

| Operation | Throughput | P99 Latency |
|---|---|---|
| Insert | ~1,000,000 /sec | — |
| Search (top-1) | ~310 QPS | ~4.1 ms |
| Search (top-10) | ~293 QPS | ~4.6 ms (unstable) |
| Update (indexed) | ~400,000 /sec | ~0.004 ms |
| Mixed workload | ~3,000 ops/sec | — |