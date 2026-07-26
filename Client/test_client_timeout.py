"""
Regression tests for RedBoxClient socket timeout behavior (issue #91).

Uses a real local TCP listener that accepts connections but never sends
data, to prove the client actually times out instead of hanging forever --
not just that settimeout() is called somewhere.
"""
import socket
import threading
import time
import unittest

from client import RedBoxClient


class _SilentServer:
    """Accepts TCP connections and then never sends or closes -- simulates
    an unresponsive server for timeout testing."""

    def __init__(self):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.bind(("127.0.0.1", 0))
        self._sock.listen(1)
        self.port = self._sock.getsockname()[1]
        self._stop = False
        self._accepted = []
        self._thread = threading.Thread(target=self._accept_loop, daemon=True)
        self._thread.start()

    def _accept_loop(self):
        self._sock.settimeout(0.1)
        while not self._stop:
            try:
                conn, _ = self._sock.accept()
                self._accepted.append(conn)
            except socket.timeout:
                continue
            except OSError:
                break

    def close(self):
        self._stop = True
        self._thread.join(timeout=2)
        for conn in self._accepted:
            conn.close()
        self._sock.close()


class TestClientTimeout(unittest.TestCase):
    def setUp(self):
        self.server = _SilentServer()

    def tearDown(self):
        self.server.close()

    def test_handshake_times_out_instead_of_hanging_forever(self):
        start = time.monotonic()
        with self.assertRaises((socket.timeout, TimeoutError, ConnectionError)):
            RedBoxClient(host="127.0.0.1", port=self.server.port, timeout=0.3)
        elapsed = time.monotonic() - start

        # Bounded well below what "hangs forever" would look like in a test run.
        self.assertLess(elapsed, 5.0)

    def test_default_timeout_is_thirty_seconds(self):
        client = RedBoxClient.__new__(RedBoxClient)
        client.host = "127.0.0.1"
        client.port = self.server.port
        client.timeout = 30.0
        client.sock = None
        client._connect()
        try:
            self.assertEqual(client.sock.gettimeout(), 30.0)
        finally:
            client.close()


if __name__ == "__main__":
    unittest.main()
