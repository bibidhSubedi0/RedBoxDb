import time
import numpy as np
import statistics
import struct
import sys
from client import RedBoxClient

# ==========================================
# CONFIGURATION
# ==========================================
DB_NAME = "bench_final_100k" # Fresh name ensures 100k capacity
DIM = 64
NUM_INSERTS = 50_000         # Adjust based on how long you want to wait
NUM_QUERIES = 2_000          # Higher = more accurate latency stats

def print_metric(label, value, unit=""):
    print(f"   • {label:<15}: {value} {unit}")

def run_benchmark():
    print("==================================================")
    print("   RedBoxDb HIGH-PERFORMANCE BENCHMARK")
    print(f"   Target: {NUM_INSERTS:,} Vectors | Dim: {DIM}")
    print("==================================================\n")

    # --- 1. DATA GENERATION ---
    print(f"[SETUP] Generating {NUM_INSERTS:,} synthetic vectors in RAM...", end="", flush=True)
    # Using float32 for direct binary compatibility
    data_vectors = np.random.rand(NUM_INSERTS, DIM).astype(np.float32)
    print(" Done.")

    client = RedBoxClient(db_name=DB_NAME, dim=DIM)

    # --- 2. INGESTION TEST ---
    print(f"\n[PHASE 1] Ingestion Throughput")
    print(f"   -> Streaming {NUM_INSERTS:,} vectors over TCP...")
    
    start_time = time.perf_counter()
    
    # We loop through standard insert to test full protocol overhead
    for i in range(NUM_INSERTS):
        client.insert(i, data_vectors[i])
        
        if (i+1) % 10000 == 0:
            print(f"      ... {i+1:,} inserted")
            
    end_time = time.perf_counter()
    duration = end_time - start_time
    tps = NUM_INSERTS / duration
    
    print(f"   ✅ INGESTION COMPLETE")
    print_metric("Total Time", f"{duration:.2f}", "s")
    print_metric("Throughput", f"{tps:,.0f}", "vectors/sec")

    # --- 3. SEARCH LATENCY TEST ---
    print(f"\n[PHASE 2] Search Latency ({NUM_QUERIES:,} probes)")
    latencies = []
    
    # Randomly sample existing vectors to ensure "Hit" logic is tested
    query_indices = np.random.choice(NUM_INSERTS, NUM_QUERIES, replace=False)
    
    # Pre-select vectors to avoid numpy overhead inside the timing loop
    query_vectors = data_vectors[query_indices]
    
    print("   -> Running probes...")
    for vec in query_vectors:
        t0 = time.perf_counter()
        client.search(vec)
        t1 = time.perf_counter()
        latencies.append((t1 - t0) * 1000.0) # Convert to ms

    latencies.sort()
    avg_lat = statistics.mean(latencies)
    p50 = latencies[int(len(latencies) * 0.50)]
    p95 = latencies[int(len(latencies) * 0.95)]
    p99 = latencies[int(len(latencies) * 0.99)]

    print(f"   ✅ LATENCY RESULTS")
    print_metric("Average", f"{avg_lat:.2f}", "ms")
    print_metric("Median (P50)", f"{p50:.2f}", "ms")
    print_metric("P95", f"{p95:.2f}", "ms")
    print_metric("P99", f"{p99:.2f}", "ms")

    # --- 4. CONTEXT SWITCHING TEST ---
    print(f"\n[PHASE 3] Multi-Tenant Context Switching")
    print("   -> Switching between 2 DBs 100 times...")
    
    start_time = time.perf_counter()
    for i in range(100):
        # We simulate a web server handling requests for different users
        db_target = "bench_A" if i % 2 == 0 else "bench_B"
        client._handshake(db_target, DIM)
    end_time = time.perf_counter()
    
    avg_switch = ((end_time - start_time) / 100) * 1000
    print_metric("Avg Switch", f"{avg_switch:.2f}", "ms")

    client.close()
    print("\n==================================================")
    print("   BENCHMARK SUITE COMPLETE")
    print("==================================================")

if __name__ == "__main__":
    try:
        run_benchmark()
    except Exception as e:
        print(f"\n❌ Benchmark Failed: {e}")