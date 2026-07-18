import socket
import struct
import numpy as np
from typing import Union, List, Optional


class RedBoxClient:
    """
    High-performance Python client for RedBoxDb (C++).
    Handles raw binary protocol communication over TCP.
    """

    # Protocol command constants
    CMD_INSERT      = 1
    CMD_SEARCH      = 2
    CMD_DELETE      = 3
    CMD_SELECT_DB   = 4
    CMD_UPDATE      = 5
    CMD_INSERT_AUTO = 6
    CMD_SEARCH_N    = 7
    CMD_DROP_DB     = 8
    CMD_SET_PROBES  = 9
    CMD_CREATE_HNSW_DB = 10
    CMD_SET_HNSW_EF = 11

    def __init__(self, host: str = '127.0.0.1', port: int = 8080, db_name: str = 'default', dim: int = 128, capacity: int=100_000):
        self.host    = host
        self.port    = port
        self.dim     = dim
        self.db_name = db_name
        self.capacity = capacity
        self.sock: Optional[socket.socket] = None

        self._connect()
        self._handshake(db_name, dim, capacity)


    def _connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        try:
            self.sock.connect((self.host, self.port))
        except ConnectionRefusedError:
            raise ConnectionError(f"Could not connect to RedBoxDb at {self.host}:{self.port}. Is the server running?")

    def _handshake(self, name: str, dim: int, capacity: int):
        name_bytes = name.encode('utf-8')
        header  = struct.pack('<BI', self.CMD_SELECT_DB, len(name_bytes))
        payload = name_bytes + struct.pack('<II', dim, capacity)
        self.sock.sendall(header + payload)
        ack = self.sock.recv(1)
        if not ack:
            raise RuntimeError("Server rejected handshake or disconnected.")

    def _recv_exact(self, n: int) -> bytes:
        buf = b''
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("Server disconnected mid-response")
            buf += chunk
        return buf

    def _validate_vector(self, vector: Union[np.ndarray, List[float]]) -> bytes:
        if isinstance(vector, list):
            vector = np.array(vector, dtype=np.float32)
        if vector.shape[0] != self.dim:
            raise ValueError(f"Vector dim {vector.shape[0]} != DB dim {self.dim}")
        return vector.astype(np.float32).tobytes()

    # ------------------------------------------------------------------

    def insert(self, vec_id: int, vector: Union[np.ndarray, List[float]]):
        """Insert a vector with a manually specified ID."""
        data   = self._validate_vector(vector)
        header = struct.pack('<BI', self.CMD_INSERT, vec_id)
        self.sock.sendall(header + data)
        self.sock.recv(1)  # ack

    def insert_auto(self, vector: Union[np.ndarray, List[float]]) -> int:
        """Insert a vector with a server-assigned auto-incrementing ID. Returns the ID."""
        data   = self._validate_vector(vector)
        header = struct.pack('<BI', self.CMD_INSERT_AUTO, 0)
        self.sock.sendall(header + data)
        return struct.unpack('<Q', self._recv_exact(8))[0]

    def search(self, vector: Union[np.ndarray, List[float]]) -> int:
        """Return the ID of the nearest vector (L2). Returns -1 if DB is empty."""
        data   = self._validate_vector(vector)
        header = struct.pack('<BI', self.CMD_SEARCH, 0)
        self.sock.sendall(header + data)
        return struct.unpack('<i', self._recv_exact(4))[0]

    def search_n(self, vector: Union[np.ndarray, List[float]], n: int) -> List[int]:
        """Return the IDs of the N nearest vectors, closest first. Returns [] for n<=0."""
        if n <= 0:
            return []
        data   = self._validate_vector(vector)
        header = struct.pack('<BI', self.CMD_SEARCH_N, n)
        self.sock.sendall(header + data)
        count = struct.unpack('<I', self._recv_exact(4))[0]
        if count == 0:
            return []
        return list(struct.unpack(f'<{count}i', self._recv_exact(count * 4)))

    def delete(self, vec_id: int) -> bool:
        """Soft-delete a vector by ID. Returns True if found and deleted."""
        header = struct.pack('<BI', self.CMD_DELETE, vec_id)
        self.sock.sendall(header)
        return self.sock.recv(1) == b'1'

    def update(self, vec_id: int, vector: Union[np.ndarray, List[float]]) -> bool:
        """Overwrite an existing vector in-place. Returns False if ID not found/deleted."""
        data   = self._validate_vector(vector)
        header = struct.pack('<BI', self.CMD_UPDATE, vec_id)
        self.sock.sendall(header + data)
        return self.sock.recv(1) == b'1'

    def drop(self) -> bool:
        self.sock.sendall(struct.pack('<BI', self.CMD_DROP_DB, 0))
        return self.sock.recv(1) == b'1'

    def set_probes(self, probes: int) -> bool:
        """Set the number of IVF clusters to probe during search (1-255)."""
        header = struct.pack('<BI', self.CMD_SET_PROBES, probes)
        self.sock.sendall(header)
        return self.sock.recv(1) == b'1'

    def set_hnsw_ef(self, ef: int) -> bool:
        """Set the HNSW ef_search parameter."""
        header = struct.pack('<BI', self.CMD_SET_HNSW_EF, ef)
        self.sock.sendall(header)
        return self.sock.recv(1) == b'1'

    @classmethod
    def create_hnsw(cls, host: str = '127.0.0.1', port: int = 8080,
                    db_name: str = 'default', dim: int = 128,
                    capacity: int = 100_000,
                    hnsw_M: int = 16, hnsw_ef_construction: int = 200):
        """Create a new client connected to an HNSW database."""
        client = cls.__new__(cls)
        client.host = host
        client.port = port
        client.dim = dim
        client.db_name = db_name
        client.capacity = capacity
        client.sock = None

        client._connect()
        client._handshake_hnsw(db_name, dim, capacity, hnsw_M, hnsw_ef_construction)
        return client

    def _handshake_hnsw(self, name: str, dim: int, capacity: int, hnsw_M: int, hnsw_ef_construction: int):
        name_bytes = name.encode('utf-8')
        header  = struct.pack('<BI', self.CMD_CREATE_HNSW_DB, len(name_bytes))
        payload = name_bytes + struct.pack('<I', dim) + struct.pack('<I', capacity)
        payload += struct.pack('<B', hnsw_M) + struct.pack('<H', hnsw_ef_construction)
        self.sock.sendall(header + payload)
        ack = self.sock.recv(1)
        if not ack:
            raise RuntimeError("Server rejected HNSW handshake or disconnected.")

    # ------------------------------------------------------------------

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()


# --- QUICK USAGE EXAMPLE ---
if __name__ == "__main__":
    print("Connecting to RedBoxDb...")
    with RedBoxClient(db_name="test_auto", dim=3) as db:

        print("\n-- Manual Insert --")
        db.insert(999, [1.0, 0.0, 0.0])
        print("Inserted ID 999 manually")

        print("\n-- Auto Insert --")
        id1 = db.insert_auto([0.0, 1.0, 0.0])
        id2 = db.insert_auto([0.0, 0.0, 1.0])
        id3 = db.insert_auto([1.0, 1.0, 0.0])
        print(f"Auto-inserted 3 vectors, got IDs: {id1}, {id2}, {id3}")

        print("\n-- Search --")
        result = db.search([0.0, 0.9, 0.1])
        print(f"Search near [0, 0.9, 0.1] -> ID {result}  (expect {id1})")

        print("\n-- Search N --")
        db.insert_auto([1.0, 0.0, 0.0])
        db.insert_auto([2.0, 0.0, 0.0])
        db.insert_auto([3.0, 0.0, 0.0])
        db.insert_auto([100.0, 0.0, 0.0])

        results = db.search_n([0.0, 0.0, 0.0], 3)
        print(f"Top 3 nearest: {results}")

        results = db.search_n([0.0, 0.0, 0.0], 10)
        print(f"Ask for 10, got: {len(results)} results")

        print("\n-- Drop --")
        ok = db.drop()
        print(f"DB dropped: {ok}")