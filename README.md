
# RedBoxDb

![CI](https://github.com/bibidhSubedi0/RedBoxDb/actions/workflows/ci.yml/badge.svg)
![PyPI](https://img.shields.io/pypi/v/redboxdb)
![License](https://img.shields.io/badge/license-MIT-blue.svg)

A high-performance, vector database engine written in **C++ 17+** with a **Python** client SDK. Designed for local RAG applications, rapid prototyping, and embedding management.

RedBoxDb operates on a client-server architecture where the Python library manages a bundled C++ backend process, communicating via a custom binary TCP protocol to minimize serialization overhead.

## Performance Metrics

Benchmarks run on a standard Windows workstation (50,000 vectors, Dim: 64):

* **Ingestion Throughput:** ~26,000 vectors/sec
* **Search Latency:** ~4ms (p50)
* **Protocol:** Raw TCP (No HTTP/JSON overhead)
* **Storage:** Append-only log persistence

## Installation

The Python package includes the compiled C++ server engine (Windows x64).

```bash
pip install redboxdb
```



*Note: Currently supports **Windows x64** only. Linux support requires compiling the C++ engine from source.*

## Quick Start

```python
from redboxdb import RedBoxClient
import numpy as np

# 1. Initialize (Auto-starts server if not running)
#    Creates a persistent database named 'wiki_vectors'
db = RedBoxClient(db_name="wiki_vectors", dim=128)

# 2. Insert Data (ID, Vector)
#    Vectors are validated for dimension consistency
db.insert(100, [0.1, 0.5, 0.9, ...]) 
db.insert(101, [0.9, 0.5, 0.1, ...])

# 3. Search (L2 Euclidean Distance)
#    Returns the ID of the nearest neighbor
result_id = db.search([0.1, 0.5, 0.8, ...])
print(f"Nearest Match ID: {result_id}") 

# 4. Cleanup
db.close()

```

## Architecture

1. **Engine (`/src`)**: Written in C++17. Handles memory management, vector storage, and linear scanning. Listens on TCP port 8080.
2. **Protocol**: Custom binary format.
* Request: `[CMD (1b)] [ID (4b)] [Payload]`
* Response: `[ACK (1b)]` or `[Data]`


3. **Storage**: Data is persisted to `.db` files in the execution directory using a robust append-only format.
4. **Client (`/redbox-sdk`)**: Python wrapper that handles process lifecycle (subprocess spawning), binary packing (`struct.pack`), and socket communication.

## API Reference

| Method | Description | Complexity |
| --- | --- | --- |
| `insert(id: int, vec: list)` | Adds a vector to the in-memory index and disk log. | O(1) |
| `search(vec: list) -> int` | Finds the nearest neighbor ID using L2 distance. | O(N) |
| `update(id: int, vec: list)` | Overwrites an existing vector in place. | O(1) |
| `delete(id: int)` | Soft-deletes a vector (excluded from search results). | O(1) |
| `RedBoxClient(db_name, dim)` | Connects to DB. Starts server process if port 8080 is free. | - |

## Building from Source

If you want to modify the C++ engine or build for non-Windows platforms:

**Requirements:**

* CMake 3.15+
* C++17 or above Compiler (MSVC, GCC, or Clang)

```bash
# 1. Clone the repository
git clone [https://github.com/bibidhSubedi0/RedBoxDb.git](https://github.com/bibidhSubedi0/RedBoxDb.git)
cd RedBoxDb

# 2. Build via CMake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# 3. Run the Server
./build/src/Release/RedBoxServer.exe

```

## Running Verification

The repository includes a comprehensive verification suite handling CRUD operations, concurrency, and persistence checks.

```bash
# Requires server to be running or installed via pip
python Client/validation.py

```

## License

MIT License. See `LICENSE` for details.

```

```
