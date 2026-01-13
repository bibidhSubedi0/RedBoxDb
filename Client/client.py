import socket
import struct
import numpy as np
from typing import Union, List, Optional

class RedBoxClient:
    """
    High-performance Python client for RedBoxDb (C++).
    Handles raw binary protocol communication over TCP.
    """

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
        # Packet: [CMD=4] [NameLen] [Name] [Dim]
        header = struct.pack('<BI', 4, len(name_bytes))
        payload = name_bytes + struct.pack('<I', dim)
        
        self.sock.sendall(header + payload)
        ack = self.sock.recv(1)
        if not ack:
            raise RuntimeError("Server rejected handshake or disconnected.")

    def _validate_vector(self, vector: Union[np.ndarray, List[float]]) -> bytes:
        """Helper to validate dimension and convert to raw bytes."""
        if isinstance(vector, list):
            vector = np.array(vector, dtype=np.float32)
            
        if vector.shape[0] != self.dim:
            raise ValueError(f"Vector dim {vector.shape[0]} != DB dim {self.dim}")
            
        return vector.astype(np.float32).tobytes()

    def insert(self, vec_id: int, vector: Union[np.ndarray, List[float]]):
        """
        Inserts a new vector.
        Protocol: [CMD=1] [ID] [Floats...]
        """
        data = self._validate_vector(vector)
        header = struct.pack('<BI', 1, vec_id)
        
        self.sock.sendall(header + data)
        self.sock.recv(1) # Wait for Ack

    def search(self, vector: Union[np.ndarray, List[float]]) -> int:
        """
        Finds the closest vector ID (L2 Distance).
        Protocol: [CMD=2] [Ignored] [Floats...]
        Returns: ID of the nearest neighbor.
        """
        data = self._validate_vector(vector)
        header = struct.pack('<BI', 2, 0)
        
        self.sock.sendall(header + data)
        
        # Receive 4-byte ID
        response = self.sock.recv(4)
        return struct.unpack('<i', response)[0]

    def delete(self, vec_id: int) -> bool:
        """
        Soft-deletes a vector by ID.
        Protocol: [CMD=3] [ID]
        Returns: True if deleted, False if ID not found.
        """
        header = struct.pack('<BI', 3, vec_id)
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
        header = struct.pack('<BI', 5, vec_id)
        
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
    # Example usage when running this file directly
    print("Connecting to RedBoxDb...")
    with RedBoxClient(db_name="test_client", dim=3) as db:
        print("-> Inserting ID 100: [1, 0, 0]")
        db.insert(100, [1.0, 0.0, 0.0])
        
        print("-> Searching for [0.9, 0.1, 0.1]")
        result = db.search([0.9, 0.1, 0.1])
        print(f"   Result ID: {result}")
        
        print("-> Updating ID 100 to [0, 1, 0]")
        success = db.update(100, [0.0, 1.0, 0.0])
        print(f"   Update Success: {success}")