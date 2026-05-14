# RedBoxDb: IVF Clustering Implementation Report

## Background

RedBoxDb is a persistent vector database built on memory-mapped files with a columnar layout. Prior to this work, search used brute-force linear scan, every query computed L2 distance against all N vectors, giving O(N x dim) work per query. At 100k vectors, dim=128, with AVX2 SIMD and 12-thread parallelism, this produced P50=2.26ms, P99=3.24ms, QPS=431.

The goal was sub-millisecond P50 search latency without sacrificing insert throughput.

---

## Approach: Inverted File Index (IVF)

IVF partitions the vector space into K clusters, each with a centroid. At query time:

1. Score all K centroids: O(K x dim)
2. Retrieve candidates from the nearest cluster: O(N/K)
3. Compute exact distances on candidates only: O(N/K x dim)

Total work per query drops from O(N) to O(K + N/K), a theoretical reduction of ~10x at K=10, N=100k.

---

## Data Structures

**Persistent (mmap):**

```
[ Header (128B)                      ]
[ centroid_block: K x dim x 4B       ]  row-major, centroid c at offset c x dim
[ cluster_count_block: K x 8B        ]  per-cluster vector counts for online updates
[ cluster_block: capacity x uint16_t ]  cluster assignment per slot
[ id_block: capacity x uint64_t      ]
[ float_block: capacity x dim x 4B   ]
```

`cluster_block[i]` stores the cluster assignment for slot i, parallel to `id_block` and `float_block`. This is the ground truth, persisted across restarts.

**In-memory:**

`cluster_index`: `vector<vector<int>>` of size K. `cluster_index[c]` holds the slot numbers of all vectors assigned to cluster c. Built on open by scanning `cluster_block` once. This is the critical structure: candidate lookup becomes O(cluster_size) with no scan of the full float block.

---

## Initialization: K-Means++

Triggered once when `vector_count` reaches `KMEANS_INIT_THRESHOLD` (10,000). Runs on all vectors inserted so far.

**Algorithm:**
1. Pick first centroid uniformly at random from N vectors
2. For each subsequent centroid: compute each vector's distance to the nearest already-chosen centroid, sample next centroid proportional to D^2, maximizing spread
3. Two-pass assignment: assign all N vectors to nearest centroid accumulating per-cluster sums, then recompute centroids as true means

The two-pass design is critical. An earlier implementation used an online running mean during assignment, which left `cluster_count_block` reflecting only the K init vectors. Post-init inserts used a denominator of 1 to 3, destroying centroids: all 99,900 subsequent vectors collapsed into one cluster (verified: max cluster size = 99,900). The two-pass fix computes exact means; `cluster_count_block` reflects true counts and post-init online updates are stable.

---

## Insert Path (post-init)

```
insert(id, vec):
  1. find_nearest_centroid(vec, centroid_block, K)  -> c   [O(K x dim), AVX2]
  2. cluster_block[slot] = c                               [mmap write]
  3. id_block[slot] = id, memcpy vec -> float_block        [mmap write]
  4. update_centroid(c, vec): centroid[c] += (vec - centroid[c]) / ++count[c]
  5. cluster_index[c].push_back(slot)                      [in-memory]
```

Step 4 is an exact running mean, no full recompute, O(dim) per insert.

---

## Search Path

```
search(query):
  1. Score K centroids -> partial_sort -> nearest num_probes centroids
  2. candidates = cluster_index[c]  (no slot scan)
  3. L2(query, float_block[slot]) for slot in candidates
  4. Return id_block[best_slot]
```

An earlier attempt built candidates by iterating all N slots checking `cluster_block[i]`: O(N) with a cheap comparison, but still touching 100k memory locations per query, giving identical latency to brute force. The inverted index eliminates that scan entirely.

---

## Bottleneck Analysis

Insert microbenchmark (K=10, dim=128, AVX2, on power):

| Operation | Avg latency | Implied cap |
|-----------|-------------|-------------|
| Raw memcpy (heap) | 144ns | 6.9M/s |
| unordered_map insert | 148ns | 6.7M/s |
| Centroid scan K=10 | ~220ns | ~4.5M/s |
| Full pipeline (heap) | 256ns | 3.9M/s |
| Real insert (mmap) | ~940ns | ~1.06M/s |

The 4x gap between heap pipeline (3.9M/s theoretical) and real insert (1.06M/s) is mmap page fault cost: first writes to each page go through the OS, not captured by heap microbenchmarks. This is the hard floor for insert throughput with a persistent mmap design.

A background thread decoupling insert from centroid scan was implemented and benchmarked. It added mutex and condition variable overhead that cost more than the centroid scan itself, netting worse performance than the synchronous path.

---

## Results

Benchmarked on Ryzen 5 5500U, AVX2, 12 threads, 100k vectors, dim=128, K=10.

| Metric | Brute Force | IVF K=10 | Delta |
|--------|-------------|----------|-------|
| Search P50 | 2.26ms | 0.79ms | -65% |
| Search P99 | 3.24ms | 1.23ms | -62% |
| QPS | 431 | 1,184 | +2.7x |
| Insert throughput | 1.6M/s | 1.06M/s | -34% |
| Search under 40% deletion P50 | 2.03ms | 0.48ms | -76% |
| Mixed workload ops/sec | 2,805 | 8,335 | +2.9x |

Insert regression is an inherent tradeoff of clustered indexing: every insert pays O(K x dim) for centroid assignment on top of the mmap write cost.

---

## Known Limitations

**Cluster imbalance:** Online K-Means updates drift over time as data distribution shifts. No rebalancing is performed between restarts. At 100k random vectors K=10, max cluster size is 13,786 vs ideal 10,000, a 38% imbalance. Periodic background reclustering is documented as future work.

**Approximate search:** IVF with `num_probes=1` may miss the true nearest neighbor if it falls in an adjacent cluster. Increasing `num_probes` improves recall at linear cost to search latency.

**Insert throughput:** 1.06M/s is bounded by mmap page fault cost on first write, not by the clustering logic. This is a one-time cost per page: a warm DB (pages already faulted) would show higher sustained throughput.