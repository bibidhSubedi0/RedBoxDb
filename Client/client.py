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
    

    def __init__(self, host: str = '127.0.0.1', port: int = 8080, db_name: str = 'default', dim: int = 128):
        """
        Connects to the RedBoxDb server and selects the database.

        Args:
            host (str): Server IP address.
            port (int): Server Port (default 8080).
            db_name (str): The specific database to open/create.
            dim (int): The dimension of vectors for this DB.
        """
        self.host = host
        self.port = port
        self.dim = dim
        self.sock: Optional[socket.socket] = None
        
        self._connect()
        self._handshake(db_name, dim)

    def _connect(self):
        """Establishes the physical TCP connection."""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        # Disable Nagle's algorithm to minimize latency for small packets
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        
        try:
            self.sock.connect((self.host, self.port))
        except ConnectionRefusedError:
            raise ConnectionError(f"Could not connect to RedBoxDb at {self.host}:{self.port}. Is the server running?")

    def _handshake(self, name: str, dim: int):
        """Internal: Sends [CMD=4] to select/create the database."""
        name_bytes = name.encode('utf-8')
        header = struct.pack('<BI', self.CMD_SELECT_DB, len(name_bytes))
        payload = name_bytes + struct.pack('<I', dim)
        
        self.sock.sendall(header + payload)
        ack = self.sock.recv(1)
        if not ack:
            raise RuntimeError("Server rejected handshake or disconnected.")

    def _recv_exact(self, n: int) -> bytes:
        """Read exactly n bytes from the socket, handling partial reads."""
        buf = b''
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("Server disconnected mid-response")
            buf += chunk
        return buf

    def _validate_vector(self, vector: Union[np.ndarray, List[float]]) -> bytes:
        """Helper to validate dimension and convert to raw bytes."""
        if isinstance(vector, list):
            vector = np.array(vector, dtype=np.float32)
            
        if vector.shape[0] != self.dim:
            raise ValueError(f"Vector dim {vector.shape[0]} != DB dim {self.dim}")
            
        return vector.astype(np.float32).tobytes()

    def insert(self, vec_id: int, vector: Union[np.ndarray, List[float]]):
        """
        Inserts a new vector with a manually specified ID.
        Protocol: [CMD=1] [ID] [Floats...]
        """
        data = self._validate_vector(vector)
        header = struct.pack('<BI', self.CMD_INSERT, vec_id)
        
        self.sock.sendall(header + data)
        self.sock.recv(1)  # Wait for Ack

    def insert_auto(self, vector: Union[np.ndarray, List[float]]) -> int:
        """
        Inserts a new vector with an auto-generated ID.
        The server assigns the next available ID and returns it.
        Protocol: [CMD=6] [0 (ignored)] [Floats...]
        Returns: The auto-assigned ID (uint64).
        """
        data = self._validate_vector(vector)
        header = struct.pack('<BI', self.CMD_INSERT_AUTO, 0)

        self.sock.sendall(header + data)

        # Server responds with the assigned ID — 8 bytes (uint64)
        response = self._recv_exact(8)
        return struct.unpack('<Q', response)[0]  # Q = unsigned 64-bit int

    def search_n(self, vector: Union[np.ndarray, List[float]], n: int) -> List[int]:
        """
        Finds the N closest vector IDs (L2 Distance), ordered closest first.
        Protocol: [CMD=7] [N] [Floats...]
        Returns: List of IDs, closest first. May be shorter than N if DB has fewer vectors.
        """
        data = self._validate_vector(vector)
        header = struct.pack('<BI', self.CMD_SEARCH_N, n)

        self.sock.sendall(header + data)

        # Read result count first
        count = struct.unpack('<I', self._recv_exact(4))[0]
        if count == 0:
            return []

        # Read exactly count IDs
        raw = self._recv_exact(count * 4)
        return list(struct.unpack(f'<{count}i', raw))

    def search(self, vector: Union[np.ndarray, List[float]]) -> int:
        """
        Finds the closest vector ID (L2 Distance).
        Protocol: [CMD=2] [Ignored] [Floats...]
        Returns: ID of the nearest neighbor.
        """
        data = self._validate_vector(vector)
        header = struct.pack('<BI', self.CMD_SEARCH, 0)
        
        self.sock.sendall(header + data)
        
        response = self._recv_exact(4)
        return struct.unpack('<i', response)[0]

    def delete(self, vec_id: int) -> bool:
        """
        Soft-deletes a vector by ID.
        Protocol: [CMD=3] [ID]
        Returns: True if deleted, False if ID not found.
        """
        header = struct.pack('<BI', self.CMD_DELETE, vec_id)
        self.sock.sendall(header)
        resp = self.sock.recv(1)
        return resp == b'1'

    def update(self, vec_id: int, vector: Union[np.ndarray, List[float]]) -> bool:
        """
        Strict Update: Overwrites an existing vector.
        Protocol: [CMD=5] [ID] [Floats...]
        Returns: True if updated, False if ID not found/deleted.
        """
        data = self._validate_vector(vector)
        header = struct.pack('<BI', self.CMD_UPDATE, vec_id)
        
        self.sock.sendall(header + data)
        resp = self.sock.recv(1)
        return resp == b'1'

    def close(self):
        """Closes the TCP connection."""
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

        # Manual insert (old way)
        print("\n-- Manual Insert --")
        db.insert(999, [1.0, 0.0, 0.0])
        print("Inserted ID 999 manually")

        # Auto insert (new way)
        print("\n-- Auto Insert --")
        id1 = db.insert_auto([0.0, 1.0, 0.0])
        id2 = db.insert_auto([0.0, 0.0, 1.0])
        id3 = db.insert_auto([1.0, 1.0, 0.0])
        print(f"Auto-inserted 3 vectors, got IDs: {id1}, {id2}, {id3}")

        # Search
        print("\n-- Search --")
        result = db.search([0.0, 0.9, 0.1])
        print(f"Search near [0, 0.9, 0.1] → ID {result}  (expect {id1})")

        # Search N
        print("\n-- Search N --")
        db.insert_auto([1.0, 0.0, 0.0])
        db.insert_auto([2.0, 0.0, 0.0])
        db.insert_auto([3.0, 0.0, 0.0])
        db.insert_auto([100.0, 0.0, 0.0])

        results = db.search_n([0.0, 0.0, 0.0], 3)
        print(f"Top 3 nearest: {results}")  

        results = db.search_n([0.0, 0.0, 0.0], 10)  
        print(f"Ask for 10, got: {len(results)} results")  