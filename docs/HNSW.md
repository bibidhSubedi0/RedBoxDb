# HNSW in RedBoxDb: Complete Technical Documentation

## Table of Contents

1. [What is HNSW?](#1-what-is-hnsw)
2. [Architecture Overview](#2-architecture-overview)
3. [On-Disk Format (mmap Layout)](#3-on-disk-format-mmap-layout)
4. [Metadata Header (SpecificMetadata)](#4-metadata-header-specificmetadata)
5. [Core Algorithm: search_layer (Beam Search)](#5-core-algorithm-search_layer-beam-search)
6. [Insert Algorithm (hnsw_insert)](#6-insert-algorithm-hnsw_insert)
7. [Search Algorithms](#7-search-algorithms)
8. [Neighbor Selection](#8-neighbor-selection)
9. [Level Assignment](#9-level-assignment)
10. [Edge Layout and Pointer Arithmetic](#10-edge-layout-and-pointer-arithmetic)
11. [Distance Computation (AVX2 L2)](#11-distance-computation-avx2-l2)
12. [Server Protocol](#12-server-protocol)
13. [Python Client API](#13-python-client-api)
14. [Benchmarks and Recall Testing](#14-benchmarks-and-recall-testing)
15. [Bugs Found and Fixed](#15-bugs-found-and-fixed)
16. [Performance Characteristics](#16-performance-characteristics)
17. [Current Status and Known Issues](#17-current-status-and-known-issues)

---

## 1. What is HNSW?

HNSW (Hierarchical Navigable Small World) is a graph-based approximate nearest neighbor (ANN) algorithm. It builds a multi-layer graph where:

- **Layer 0 (base layer)**: Every node exists here, densely connected. Each node has up to `2*M` neighbors.
- **Layers 1+**: Only a fraction of nodes exist. Higher layers are sparse and act as "highways" for fast long-distance navigation. Each node has up to `M` neighbors.
- **Level assignment**: Probabilistic. Most nodes get level 0, fewer get level 1, very few get level 2, etc. Distribution follows `floor(-ln(r) * (1/ln(M)))`.

**Search**: Start at the entry point (highest-level node), greedily descend through levels, then do a beam search at level 0 to find the final results.

**Insert**: Find the correct position by descending the graph, then connect the new node at each level with bidirectional edges.

**Key parameters**:
- `M` (default 16): Max neighbors per node. Higher = better recall, more memory/slower insert.
- `ef_construction` (default 200): Beam width during insert. Higher = better graph quality, slower insert.
- `ef_search` (default 256): Beam width during search. Higher = better recall, slower search.

---

## 2. Architecture Overview

```
User / Python Client (client.py)
        |
        | TCP binary protocol (little-endian)
        v
Server (server.cpp) -- one thread per client
        |
        | CMD dispatch
        v
RedBoxVector (engine.cpp / engine.hpp) -- public API
        |
        |-- StorageManager (storage_manager.hpp) -- mmap'd file owner
        |       |-- SpecificMetadata header (128 bytes)
        |       |-- id_block, float_block, level_block, edge_block
        |
        |-- HnswManager (hnsw_manager.hpp) -- stateless algorithm library
        |       |-- search_layer(), select_neighbors_heuristic()
        |       |-- hnsw_insert(), hnsw_search(), hnsw_search_1()
        |       |-- compute_level(), edge layout helpers
        |
        |-- Distance::l2() (distance.hpp) -- AVX2-optimized L2 distance
        |
        |-- hnsw_rng (mt19937) -- level assignment randomness
        |-- deleted_flags (vector<uint8_t>) -- soft-deletion bitmap
```

**Key design decisions**:
- **Header-only algorithm**: `hnsw_manager.hpp` is entirely `inline` functions operating on raw pointers. No separate `.cpp` file.
- **mmap persistence**: All graph state lives in an mmap'd file with `MAP_SHARED`. Mutations are automatically persisted. No serialization step.
- **Stateless algorithm**: `HnswManager` has no members. All state comes from the caller (pointers into the mmap'd file).
- **Dual index type**: The same `RedBoxVector` class handles both IVF and HNSW, branching on `header->index_type`.

---

## 3. On-Disk Format (mmap Layout)

For an HNSW database with `capacity` slots, `dim` dimensions, and parameter `M`:

```
Offset 0
  +-----------------------------+
  | SpecificMetadata (128 B)    |  Header with all parameters
  +-----------------------------+
  | id_block (capacity * 8 B)   |  uint64_t per slot -- user-facing IDs
  +-----------------------------+
  | float_block (capacity * dim * 4 B) | float per dimension per slot
  +-----------------------------+
  | level_block (capacity * 1 B)|  uint8_t per slot -- assigned level
  +-----------------------------+
  | edge_block (capacity * E * 4 B) | E = edges_per_node(M)
  +-----------------------------+

Where:
  edges_per_node(M) = 2*M + MAX_LEVEL * M = 2*16 + 16*16 = 352 (for M=16)
  Total edge block = capacity * 352 * 4 bytes = capacity * 1408 bytes
```

**Example**: For capacity=100,000 and dim=128:
- Header: 128 bytes
- id_block: 800 KB
- float_block: 48.8 MB
- level_block: 97.7 KB
- edge_block: 134.2 MB
- **Total: ~183.9 MB**

**Memory layout of the edge_block for a single node**:
```
Node slot S:
  [Level 0 edges: 2*M uint32s] [Level 1 edges: M uint32s] ... [Level MAX_LEVEL edges: M uint32s]
  |<-------- 352 uint32 slots total (1408 bytes) -------->|
```

Unused edge slots are initialized to `EMPTY` (0xFFFFFFFF) and remain so until filled.

---

## 4. Metadata Header (SpecificMetadata)

The 128-byte header at offset 0 of every mmap'd file:

| Field | Type | Bytes | Description |
|---|---|---|---|
| `vector_count` | uint64_t | 0-7 | Current number of vectors (including soft-deleted) |
| `max_capacity` | uint64_t | 8-15 | Maximum allocated slots |
| `dimensions` | uint64_t | 16-23 | Vector dimensionality |
| `data_type_size` | uint64_t | 24-31 | Always sizeof(float) = 4 |
| `next_id` | uint64_t | 32-39 | Auto-increment counter |
| `num_clusters` | uint16_t | 40-41 | IVF: K clusters (unused by HNSW) |
| `version` | uint8_t | 42 | Schema version, must be 4 |
| `is_initialized` | uint8_t | 43 | HNSW: 0 = no entry point yet, 1 = first node inserted |
| `num_probes` | uint8_t | 44 | IVF: cluster probes (unused by HNSW) |
| **HNSW fields** | | | |
| `index_type` | uint8_t | 45 | 0=IVF, 1=HNSW |
| `hnsw_M` | uint8_t | 46 | Max neighbors per node |
| `hnsw_ef_construction` | uint16_t | 47-48 | Build-time beam width |
| `hnsw_ef_search` | uint16_t | 49-50 | Query-time beam width (runtime-tunable) |
| `hnsw_max_level` | uint8_t | 51 | Highest level in the graph |
| `_pad0` | uint8_t | 52 | Padding |
| `hnsw_entry_point` | uint32_t | 53-56 | Slot index of graph entry point |
| `hnsw_graph_version` | uint32_t | 57-60 | Graph version (reserved) |
| `_padding` | uint8[64] | 64-127 | Reserved |

All fields are little-endian. The struct is `static_assert`-ed to be exactly 128 bytes.

---

## 5. Core Algorithm: search_layer (Beam Search)

This is the fundamental building block used by both insert and search. Located in `hnsw_manager.hpp`.

```
search_layer(query, entry_slot, ef, level, ...) -> vector<SearchResult>
```

**Algorithm**:

1. **O(1) visited reset**: Use a generation counter (`visit_gen`). Instead of clearing the entire visited array each call, just increment the generation. A node is "visited" if `visited_buf[node] == visit_gen`. When the buffer needs to be (re)allocated, reset to generation 1.

2. **Initialize**: Push the entry slot onto both:
   - `candidates_pq` (min-heap by distance) -- candidates to explore
   - `results_pq` (max-heap by distance, capped at `ef`) -- current best results

3. **Expand loop**:
   - Pop closest candidate `f` from `candidates_pq`
   - **Early termination**: If `results_pq` already has `ef` entries and `f.dist > results_pq.top().dist`, all remaining candidates are worse -- stop
   - For each neighbor of `f` at this level:
     - Skip if EMPTY, deleted, or already visited
     - Compute L2 distance to query
     - If better than the current ef-th best: add to both heaps
     - If `results_pq` exceeds `ef` entries, pop the worst

4. **Return**: Drain `results_pq` into a vector, reverse to get ascending order (closest first)

**Time complexity**: O(ef * M * log(ef)) for the heap operations, plus O(ef * M) distance computations.

---

## 6. Insert Algorithm (hnsw_insert)

Located in `hnsw_manager.hpp`. Two-phase process.

### Phase 1: Greedy Descent (find insertion region)

```
curr = entry_point
for l = cur_max_level down to (level + 1):
    while True:
        find the neighbor of curr at level l closest to vec
        if no neighbor is closer than curr:
            break
        curr = that neighbor
```

This narrows down the search to the correct local region of the graph, starting from the sparse upper layers.

### Phase 2: Connect at Each Level

```
for l = min(level, cur_max_level) down to 0:
    ef = (l == 0) ? ef_construction * 2 : ef_construction

    // Find neighbors at this level
    results = search_layer(vec, curr, ef, l, ...)
    selected = select_neighbors_heuristic(results, m_max(l, M))

    // Write outgoing edges from new node
    set_neighbors(edge_base, slot, l, M, selected)

    // Bidirectional linking
    for each nb in selected:
        if nb has room at level l:
            append_neighbor(edge_base, nb, l, M, slot)
        else:
            // nb is full -- reselect who nb connects to
            candidates = [slot] + nb's existing neighbors at level l
            pruned = select_neighbors_heuristic(candidates, m_max(l, M))
            set_neighbors(edge_base, nb, l, M, pruned)

    // Descend to the next level through the best neighbor
    curr = selected[0]
```

**Key detail**: At level 0, ef is doubled (`ef_construction * 2`) to create a denser base layer, which is critical for recall.

**Entry point update**: If the new node's level exceeds `cur_max_level`, it becomes the new entry point.

---

## 7. Search Algorithms

### hnsw_search (k-NN, top-N)

```
hnsw_search(query, N, ...) -> vector<pair<float, uint32_t>>
```

1. **Greedy descent** from `cur_max_level` down to level 1 (same as insert Phase 1)
2. **Beam search at level 0**: `ef = max(ef_search, N)`, then `search_layer(query, curr, ef, 0, ...)`
3. Return the `ef` closest results (caller takes the top N)

### hnsw_search_1 (1-NN)

Same as `hnsw_search` but with `ef = max(ef_search, 1)` and only returns the single closest slot.

---

## 8. Neighbor Selection

### select_neighbors_heuristic (Quality-Aware)

This is the HNSW paper's diversity pruning heuristic:

1. Sort candidates by ascending distance
2. For each candidate in sorted order:
   - Accept it **only if** no already-selected neighbor is closer to it than the query is
   - This ensures graph diversity: avoids selecting a cluster of nodes all in the same region
3. **Fallback**: If the heuristic didn't fill M slots, simply take the M closest

**Cost**: O(M^2) pairwise distance checks per call.

### select_closest (Simple Truncation)

Just `partial_sort` + `resize`. No diversity pruning. Currently unused after reverting to the heuristic for recall quality.

---

## 9. Level Assignment

```cpp
int compute_level(uint8_t M, mt19937& rng) {
    double ml = 1.0 / log(M);
    double r = uniform_random(0, 1);
    if (r <= 0) r = 1e-10;  // avoid log(0)
    return (int)floor(-log(r) * ml);
}
```

For M=16: `ml = 1/ln(16) = 0.3607`. Expected distribution for 100k nodes:

| Level | Expected Count | m_max |
|---|---|---|
| 0 | ~94,200 | 2*M = 32 |
| 1 | ~5,400 | M = 16 |
| 2 | ~320 | M = 16 |
| 3 | ~20 | M = 16 |
| 4+ | ~1 | M = 16 |

`MAX_LEVEL = 16` is the hard cap. With M=16 and 100k nodes, the practical max is ~4-5.

---

## 10. Edge Layout and Pointer Arithmetic

```cpp
edges_per_node(M) = 2*M + MAX_LEVEL * M   // 352 for M=16

node_edges(edge_base, S, M) = edge_base + S * edges_per_node(M)

level_edges(node_edges(edge_base, S, M), L, M):
  Level 0: node_ed           // first 2*M slots
  Level L>0: node_ed + 2*M + (L-1)*M

m_max(L, M):
  Level 0: 2*M    // denser base layer
  Level L>0: M
```

**EMPTY sentinel**: `0xFFFFFFFF` -- means "no neighbor here". Initialized at DB creation, checked during search and insert.

---

## 11. Distance Computation (AVX2 L2)

All distance computations use squared L2 (no square root -- doesn't affect ordering).

**AVX2 path** (dim=128):
- Processes 8 floats per iteration: `_mm256_loadu_ps`, `_mm256_sub_ps`, `_mm256_fmadd_ps`
- Horizontal reduction: split 256-bit into two 128-bit halves, `_mm_hadd_ps` pairwise, extract scalar
- Scalar tail for `dim % 8 != 0`

**Runtime dispatch**: `Platform::has_avx2()` checks CPUID leaf 7, bit 5 of EBX. Set once at construction.

**Performance**: ~80-200ns per L2 computation for dim=128.

---

## 12. Server Protocol

### CMD 10 -- CREATE_HNSW_DB

```
Client -> Server:
  [1B cmd=10][4B name_len][name_len B name][4B dim][4B capacity][1B hnsw_M][2B ef_construction]

Server -> Client:
  [1B ack='1']
```

Server creates `RedBoxVector(filename, dim, capacity, hnsw_M, ef_construction)` and adds it to the catalog.

### CMD 11 -- SET_HNSW_EF

```
Client -> Server:
  [1B cmd=11][4B new_ef]

Server -> Client:
  [1B ack='1']
```

Dynamically changes `hnsw_ef_search`. Value is persisted in the mmap'd header.

### Critical: TCP_NODELAY

The server must set `TCP_NODELAY` on accepted sockets to avoid Nagle + delayed-ACK (~40ms per query otherwise). This was a critical bug fix.

---

## 13. Python Client API

### Creating an HNSW Database

```python
client = RedBoxClient.create_hnsw(
    host='127.0.0.1', port=8080,
    db_name='my_db', dim=128, capacity=100_000,
    hnsw_M=16, hnsw_ef_construction=200
)
```

This bypasses `__init__` and sends CMD 10 instead of CMD 4.

### Runtime Tuning

```python
client.set_hnsw_ef(256)  # Change ef_search at runtime
```

### Insert / Search

```python
client.insert(vec_id, vector)          # Manual ID
client.insert_auto(vector)             # Auto-incrementing ID
result = client.search(query)          # 1-NN, returns ID
results = client.search_n(query, k)    # k-NN, returns list of IDs
```

---

## 14. Benchmarks and Recall Testing

### In-Process C++ Benchmark (`benchmark/benchmark.cpp`)

100k vectors, 128 dimensions, M=16, ef_c=200, ef_s=256:

| Metric | Typical Result |
|---|---|
| Insert throughput | 580-714 vec/s (140-172s for 100k) |
| Search QPS (1-NN) | 850-975 QPS (P99 ~1.4ms) |
| Search_N QPS (10-NN) | 825-960 QPS (P99 ~2.9ms) |
| Update throughput | 250-480k updates/s |

### Python TCP Benchmark (`benchmark/client_bench.py`)

Same parameters, but includes TCP round-trip overhead. Measures insert, search, search_N, update.

### Recall Test (`benchmark/recall_test.py`)

Tests recall@K against brute-force ground truth:

- 200 random queries per DB size
- DB sizes: 9,999 / 50,000 / 100,000
- Brute-force: compute true top-K for each query, compare with HNSW results
- **Recall formula**: `recall = total_hits / (QUERIES * K)`
- Recall < 80% flagged as "BAD"

### Current Recall Results

| DB Size | Insert Time | Search Time | Recall@100 |
|---|---|---|---|
| 9,999 | ~10.9s | ~0.3s | 94.0% |
| 50,000 | ~91.6s | ~0.3s | 74.9% |
| 100,000 | ~186s | ~0.3s | ~62% (expected) |

---

## 15. Bugs Found and Fixed

### Bug 1: TCP Nagle + Delayed ACK (~40ms per query)

**Symptom**: Search time was always ~8.8s for 200 queries regardless of DB size.

**Root cause**: Server never set `TCP_NODELAY` on accepted client sockets. Combined with Linux's delayed ACK timer (~40ms), every small request-response pair added ~44ms of latency. 200 queries x 44ms = 8.8s.

**Fix**: Added `setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, ...)` after `accept()` in server.cpp.

**Result**: Search time dropped from ~8.8s to ~0.3s.

### Bug 2: Greedy Descent Using Best-Improvement Instead of First-Improvement

**Symptom**: Recall degradation at larger scales.

**Root cause**: The greedy descent at higher levels was scanning ALL neighbors, picking the best one, and moving there (best-improvement). The HNSW paper uses first-improvement: move to the first neighbor that's closer, then restart.

**Fix attempted**: Initially changed to first-improvement, then reverted to best-improvement (both are valid; the paper's beam search at ef=1 is functionally similar to best-improvement).

### Bug 3: select_closest for Reverse Links Degrading Graph Quality

**Symptom**: Recall dropped from 94% at 10k to 62% at 100k.

**Root cause**: Replaced `select_neighbors_heuristic` with `select_closest` (simple sort+trim) for reverse link updates during insert. This removed the diversity pruning that maintains the small-world graph property. Without it, reverse links cluster around the same local area, degrading graph connectivity at scale.

**Fix**: Restored `select_neighbors_heuristic` for reverse links. Trade-off: slightly slower insert (~172s vs ~140s) but correct graph structure.

### Bug 4: compute_level UB with log(0)

**Symptom**: Potential undefined behavior when the random number generator returns exactly 0.0.

**Root cause**: `log(0.0) = -inf`, causing garbage level assignments.

**Fix**: Clamp: `if (r <= 0) r = 1e-10;`

### Bug 5: search_layer O(n^2) Linear Scan

**Symptom**: Slow beam search due to linear scan + erase on a vector.

**Root cause**: The original `search_layer` used linear scan to find the best candidate and `vector::erase` to remove it -- O(n^2) in the worst case.

**Fix**: Rewrote with `std::priority_queue` (min-heap for candidates, max-heap for results). O(n * log(n)) instead.

### Bug 6: O(capacity) memset per search_layer Call

**Symptom**: Slow search/insert due to clearing the visited buffer every call.

**Root cause**: `std::vector<bool> visited(capacity, false)` allocated and zeroed on every call.

**Fix**: Generation counter -- `vector<uint32_t>` + `uint32_t visit_gen`. O(1) reset per call.

### Bug 7: SIGPIPE Crash on Linux

**Symptom**: Server crashes when a client disconnects.

**Root cause**: Writing to a closed socket sends SIGPIPE on Linux.

**Fix**: `signal(SIGPIPE, SIG_IGN)` in server main.

### Bug 8: send() Without EINTR Handling

**Symptom**: Client deadlock -- server stops responding.

**Root cause**: `send()` can return -1 with `errno=EINTR` on signal interruption. Without handling, the server breaks out of the send loop prematurely.

**Fix**: `send_all` helper that retries on EINTR.

---

## 16. Performance Characteristics

### Insert: ~46-48k distance computations per vector

Per-vector cost breakdown for M=16, ef_c=200:

| Phase | Distance Computations |
|---|---|
| Greedy descent (upper levels) | ~50-100 |
| search_layer at level 0 (ef=400) | ~12,800 |
| select_neighbors_heuristic (forward) | ~1,024 |
| Bidirectional linking (up to 32 neighbors) | ~33,792 |

At ~100-200ns per AVX2 L2 distance: ~5-10s of pure distance computation per 100k inserts, plus heap operations and memory access overhead.

### Search Complexity

- Greedy descent: O(max_level * M) distance computations
- Beam search at level 0: O(ef * M * log(ef)) with heap operations
- Total: ~1ms per query with ef_s=256, dim=128

### Per-Node Memory: 1,929 bytes (M=16, dim=128)

| Component | Size |
|---|---|
| Edges | 1,408 bytes |
| Vector | 512 bytes |
| ID | 8 bytes |
| Level | 1 byte |

### Trade-offs

| Parameter | Higher Value | Lower Value |
|---|---|---|
| M | Better recall, more memory, slower insert | Faster insert, worse recall |
| ef_construction | Better graph quality, slower insert | Worse graph quality, faster insert |
| ef_search | Better recall, slower search | Worse recall, faster search |

---

## 17. Current Status and Known Issues

### Working
- Full HNSW lifecycle: create, insert, search (1-NN and k-NN), soft delete, update
- mmap persistence across restarts
- Runtime ef_search tuning via CMD 11
- TCP protocol with proper TCP_NODELAY
- AVX2-optimized distance computation
- Generation-counter visited buffer (O(1) reset)
- Min-heap beam search
- 65 unit tests passing (all IVF; no HNSW-specific unit tests)

### Known Issues

1. **Recall degrades at scale**: 94% at 10k, 75% at 50k, ~62% at 100k. The graph quality at level 0 may be insufficient for larger datasets. Increasing ef_search or ef_construction may help, or there may be a bug in the graph construction.

2. **No HNSW-specific unit tests**: All 65 tests exercise IVF only. HNSW correctness is only validated through the recall test (which requires a running server).

3. **Soft deletion doesn't remove graph edges**: Deleted nodes are skipped during traversal but their edges remain, potentially wasting graph capacity and slightly degrading search quality.

4. **No edge graph cleanup/garbage collection**: Over time, with many insertions and deletions, the graph may accumulate dead edges.

5. **Single-threaded insert**: The insert path is fully sequential. No parallel insert support.

6. **No HNSW mixed workload benchmarks**: The mixed workload (70/20/10 search/insert/delete) and heavy-deletion benchmarks only test IVF.

7. **Insert speed**: ~140-186s for 100k vectors with current parameters. The dominant cost is the ~33 calls to `select_neighbors_heuristic` per insert (O(M^2) each).
