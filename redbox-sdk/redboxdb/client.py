import socket
import struct
import numpy as np
import subprocess
import os
import time
import atexit
from typing import Union, List, Optional

class RedBoxClient:
    """
    High-performance Python client for RedBoxDb.
    
    Features:
    - Auto-starts RedBoxServer.exe if not running.
    - Manages raw TCP binary protocol.
    - Supports numpy arrays and standard lists.
    """

    def __init__(self, 
                 db_name: str = 'default', 
                 dim: int = 128, 
                 host: str = '127.0.0.1', 
                 port: int = 8080):
        """
        Initialize connection to RedBoxDb.
        
        Args:
            db_name (str): Name of the database (created automatically if new).
            dim (int): Vector dimension (must match DB if it already exists).
            host (str): Server IP (default localhost).
            port (int): Server Port (default 8080).
        """
        self.host = host
        self.port = port
        self.dim = dim
        self.sock: Optional[socket.socket] = None
        self.server_process: Optional[subprocess.Popen] = None

        # 1. Ensure Server is Running
        if not self._is_server_running():
            print(f"[RedBox] Server not active at {host}:{port}. Attempting to start...")
            self._start_server()

        # 2. Connect
        self._connect()

        # 3. Handshake (Select/Create DB)
        self._handshake(db_name, dim)

    def _is_server_running(self) -> bool:
        """Checks if the TCP port is open."""
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(1.0)
            return s.connect_ex((self.host, self.port)) == 0

    def _start_server(self):
        """Finds the bundled executable and launches it."""
        # Locate RedBoxServer.exe relative to this script
        base_dir = os.path.dirname(os.path.abspath(__file__))
        exe_path = os.path.join(base_dir, "RedBoxServer.exe")

        if not os.path.exists(exe_path):
            # Fallback: maybe we are running from source/root, not installed package
            exe_path = "RedBoxServer.exe" 
            if not os.path.exists(exe_path):
                raise FileNotFoundError(
                    f"CRITICAL: RedBoxServer.exe not found at {exe_path}. "
                    "Please download the server or reinstall the package."
                )

        # Launch process without creating a visible console window (Windows only)
        creation_flags = 0
        if os.name == 'nt':
            creation_flags = 0x08000000  # CREATE_NO_WINDOW

        try:
            self.server_process = subprocess.Popen(
                [exe_path],
                creationflags=creation_flags,
                stdout=subprocess.DEVNULL,  # Suppress logs for cleaner output
                stderr=subprocess.DEVNULL
            )
            
            # Ensure cleanup on exit
            atexit.register(self._kill_server)
            
            # Wait for boot
            time.sleep(1.0)
            
        except Exception as e:
            raise RuntimeError(f"Failed to start RedBoxServer: {e}")

    def _kill_server(self):
        """Terminates the server subprocess."""
        if self.server_process:
            self.server_process.kill()
            self.server_process = None

    def _connect(self):
        """Establishes the physical TCP socket."""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        
        try:
            self.sock.connect((self.host, self.port))
        except ConnectionRefusedError:
            raise ConnectionError(
                f"Could not connect to {self.host}:{self.port}. "
                "Server failed to start or is blocked by firewall."
            )

    def _handshake(self, name: str, dim: int):
        """Sends Protocol CMD 4 to open/create a DB."""
        name_bytes = name.encode('utf-8')
        # Packet: [CMD=4 (1 byte)] [NameLen (4 bytes)] [NameBytes...] [Dim (4 bytes)]
        header = struct.pack('<BI', 4, len(name_bytes))
        payload = name_bytes + struct.pack('<I', dim)
        
        self.sock.sendall(header + payload)
        
        # Wait for 1-byte ACK
        ack = self.sock.recv(1)
        if not ack:
            raise ConnectionError("Server disconnected during handshake.")

    def _validate(self, vector: Union[np.ndarray, List[float]]) -> bytes:
        """Ensures vector is the correct dimension and type."""
        if isinstance(vector, list):
            vector = np.array(vector, dtype=np.float32)
        
        if vector.dtype != np.float32:
            vector = vector.astype(np.float32)

        if vector.shape[0] != self.dim:
            raise ValueError(f"Vector dimension mismatch! Expected {self.dim}, got {vector.shape[0]}")
            
        return vector.tobytes()

    # ==========================
    # PUBLIC API
    # ==========================

    def insert(self, vec_id: int, vector: Union[np.ndarray, List[float]]):
        """
        Add a vector to the database.
        Protocol: [CMD=1] [ID] [VectorData...]
        """
        body = self._validate(vector)
        header = struct.pack('<BI', 1, vec_id)
        
        self.sock.sendall(header + body)
        self.sock.recv(1) # Wait for ACK

    def search(self, vector: Union[np.ndarray, List[float]]) -> int:
        """
        Find the nearest neighbor (L2 Distance).
        Protocol: [CMD=2] [Ignored] [VectorData...]
        Returns: Integer ID of best match.
        """
        body = self._validate(vector)
        header = struct.pack('<BI', 2, 0)
        
        self.sock.sendall(header + body)
        
        # Response is a 4-byte integer ID
        resp = self.sock.recv(4)
        if len(resp) < 4:
            raise ConnectionError("Incomplete response from search.")
            
        return struct.unpack('<i', resp)[0]

    def delete(self, vec_id: int) -> bool:
        """
        Soft-delete a vector by ID.
        Protocol: [CMD=3] [ID]
        Returns: True if deleted, False if not found.
        """
        header = struct.pack('<BI', 3, vec_id)
        self.sock.sendall(header)
        resp = self.sock.recv(1)
        return resp == b'1'

    def update(self, vec_id: int, vector: Union[np.ndarray, List[float]]) -> bool:
        """
        Strict update (overwrite).
        Protocol: [CMD=5] [ID] [VectorData...]
        Returns: True if updated, False if ID doesn't exist.
        """
        body = self._validate(vector)
        header = struct.pack('<BI', 5, vec_id)
        
        self.sock.sendall(header + body)
        resp = self.sock.recv(1)
        return resp == b'1'

    def close(self):
        """Cleanly close the connection."""
        if self.sock:
            try:
                self.sock.shutdown(socket.SHUT_RDWR)
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

# Example Usage
if __name__ == "__main__":
    try:
        print("Initializing RedBox Client...")
        with RedBoxClient("demo_db", dim=3) as db:
            print("Connected!")
            db.insert(1, [1.0, 0.0, 0.0])
            print(f"Search Result: {db.search([0.9, 0.1, 0.0])}")
    except Exception as e:
        print(f"Error: {e}")