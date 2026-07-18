import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import time
import uuid
import argparse
import numpy as np
from contextlib import contextmanager
from Client.client import RedBoxClient

DIM          = 128
QUERIES      = 200
NUM_CLUSTERS = 1000
KMEANS_THRESHOLD = 10_000

def make_corpus(db_size, seed) -> np.ndarray:
    return np.random.default_rng(seed).random((db_size, DIM)).astype(np.float32)

def make_queries(seed) -> np.ndarray:
    return np.random.default_rng(seed + 999).random((QUERIES, DIM)).astype(np.float32)

def brute_force_topk(corpus: np.ndarray, queries: np.ndarray, k: int) -> np.ndarray:
    results = np.empty((len(queries), k), dtype=np.int32)
    for i, q in enumerate(queries):
        dists = np.sum((corpus - q) ** 2, axis=1)
        results[i] = np.argsort(dists)[:k]
    return results

@contextmanager
def temp_db(host, port, capacity):
    client = RedBoxClient(
        host=host, port=port,
        db_name=f"recall_{uuid.uuid4().hex[:10]}",
        dim=DIM, capacity=capacity
    )
    try:
        yield client
    finally:
        try:
            client.drop()
        except Exception:
            pass
        client.close()

@contextmanager
def temp_hnsw_db(host, port, capacity, hnsw_M=16, hnsw_ef_construction=200):
    client = RedBoxClient.create_hnsw(
        host=host, port=port,
        db_name=f"recall_hnsw_{uuid.uuid4().hex[:10]}",
        dim=DIM, capacity=capacity,
        hnsw_M=hnsw_M, hnsw_ef_construction=hnsw_ef_construction
    )
    try:
        yield client
    finally:
        try:
            client.drop()
        except Exception:
            pass
        client.close()

def sanity_check(host, port):
    print("  Running sanity check...")
    vec = np.ones(DIM, dtype=np.float32)
    with temp_db(host, port, capacity=100) as client:
        client.insert(1, vec)
        result = client.search(vec)
        if result != 1:
            print(f"  [FAIL] Inserted ID=1, search returned ID={result}")
            print("         Stale .db files on disk — delete all *.db and *.db.del files and restart server.")
            return False
        print(f"  [PASS] Inserted ID=1, search returned ID={result}")
        return True

def sanity_check_hnsw(host, port):
    print("  Running HNSW sanity check...")
    vec = np.ones(DIM, dtype=np.float32)
    with temp_hnsw_db(host, port, capacity=100) as client:
        client.insert(1, vec)
        result = client.search(vec)
        if result != 1:
            print(f"  [FAIL] HNSW: Inserted ID=1, search returned ID={result}")
            return False
        print(f"  [PASS] HNSW: Inserted ID=1, search returned ID={result}")
        return True

def run_recall(host, port, db_size, k, num_probes, seed=42):
    corpus  = make_corpus(db_size, seed)
    queries = make_queries(seed)
    true_topk = brute_force_topk(corpus, queries, k)
    capacity  = db_size + 1000
    kmeans_fired = db_size >= KMEANS_THRESHOLD

    with temp_db(host, port, capacity=capacity) as client:
        t0 = time.perf_counter()
        for i, vec in enumerate(corpus):
            client.insert(i + 1, vec)
        insert_time = time.perf_counter() - t0

        client.set_probes(num_probes)

        t0 = time.perf_counter()
        hits = 0
        for i, q in enumerate(queries):
            ivf_ids  = set(client.search_n(q, k))
            true_ids = set(int(x) + 1 for x in true_topk[i])
            hits += len(ivf_ids & true_ids)
        search_time = time.perf_counter() - t0

    recall = hits / (QUERIES * k)
    return recall, insert_time, search_time, capacity, kmeans_fired

def run_recall_hnsw(host, port, db_size, k, hnsw_M=16, hnsw_ef_construction=200, hnsw_ef_search=128, seed=42):
    corpus  = make_corpus(db_size, seed)
    queries = make_queries(seed)
    true_topk = brute_force_topk(corpus, queries, k)
    capacity  = db_size + 1000

    with temp_hnsw_db(host, port, capacity=capacity, hnsw_M=hnsw_M, hnsw_ef_construction=hnsw_ef_construction) as client:
        client.set_hnsw_ef(hnsw_ef_search)

        t0 = time.perf_counter()
        for i, vec in enumerate(corpus):
            client.insert(i + 1, vec)
        insert_time = time.perf_counter() - t0

        t0 = time.perf_counter()
        hits = 0
        for i, q in enumerate(queries):
            hnsw_ids  = set(client.search_n(q, k))
            true_ids = set(int(x) + 1 for x in true_topk[i])
            hits += len(hnsw_ids & true_ids)
        search_time = time.perf_counter() - t0

    recall = hits / (QUERIES * k)
    return recall, insert_time, search_time, capacity

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--k", type=int, default=100, help="Search K (top-K nearest)")
    parser.add_argument("--probes", type=int, default=10, help="Number of IVF clusters to probe")
    parser.add_argument("--index", choices=["ivf", "hnsw", "both"], default="both", help="Which index to test")
    parser.add_argument("--hnsw-M", type=int, default=16, help="HNSW M parameter")
    parser.add_argument("--hnsw-ef-c", type=int, default=200, help="HNSW ef_construction")
    parser.add_argument("--hnsw-ef-s", type=int, default=128, help="HNSW ef_search")
    args = parser.parse_args()

    DB_SIZES = [9_999, 50_000, 100_000]
    k = args.k

    if args.index in ("ivf", "both"):
        print("=" * 72)
        print("  RedBoxDb RECALL TEST  (IVF vs Brute Force L2)")
        print(f"  dim={DIM}  queries={QUERIES}  clusters={NUM_CLUSTERS}  probes={args.probes}  search_k={k}")
        print("=" * 72)

        if not sanity_check(args.host, args.port):
            return

        print()
        header = (f"  {'DB Size':>10}  {'Capacity':>8}  {'Clusters':>8}  {'K-Means++':>9}"
                  f"  {'Insert':>8}  {'Search':>8}  {'Recall@'+str(k):>10}")
        sep = f"  {'-'*10}  {'-'*8}  {'-'*8}  {'-'*9}  {'-'*8}  {'-'*8}  {'-'*10}"
        print(header)
        print(sep)

        for db_size in DB_SIZES:
            recall, insert_time, search_time, capacity, kmeans_fired = run_recall(args.host, args.port, db_size, k, args.probes)
            flag = "  <-- BAD" if recall < 0.8 else ""
            print(f"  {db_size:>10,}  {capacity:>8,}  {NUM_CLUSTERS:>8}  {'Yes' if kmeans_fired else 'No':>9}"
                  f"  {insert_time:>7.2f}s  {search_time:>7.2f}s  {recall:>9.1%}{flag}")

        print()

    if args.index in ("hnsw", "both"):
        if args.index == "both":
            print()

        print("=" * 72)
        print("  RedBoxDb RECALL TEST  (HNSW vs Brute Force L2)")
        print(f"  dim={DIM}  queries={QUERIES}  M={args.hnsw_M}  ef_c={args.hnsw_ef_c}  ef_s={args.hnsw_ef_s}  search_k={k}")
        print("=" * 72)

        if not sanity_check_hnsw(args.host, args.port):
            return

        print()
        header = (f"  {'DB Size':>10}  {'Capacity':>8}"
                  f"  {'Insert':>8}  {'Search':>8}  {'Recall@'+str(k):>10}")
        sep = f"  {'-'*10}  {'-'*8}  {'-'*8}  {'-'*8}  {'-'*10}"
        print(header)
        print(sep)

        for db_size in DB_SIZES:
            recall, insert_time, search_time, capacity = run_recall_hnsw(
                args.host, args.port, db_size, k,
                args.hnsw_M, args.hnsw_ef_c, args.hnsw_ef_s)
            flag = "  <-- BAD" if recall < 0.8 else ""
            print(f"  {db_size:>10,}  {capacity:>8,}"
                  f"  {insert_time:>7.2f}s  {search_time:>7.2f}s  {recall:>9.1%}{flag}")

        print()

if __name__ == "__main__":
    main()
