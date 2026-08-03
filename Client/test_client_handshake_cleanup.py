"""
Regression tests for socket cleanup on failed handshake (issue #92).

Uses a real local TCP listener that accepts the connection and then
closes it immediately without sending an ack, reproducing "the server
rejected the handshake" against real sockets rather than a mock.
"""
import socket
import threading
import unittest
import unittest.mock

from client import RedBoxClient


class _RejectingServer:
    """Accepts a TCP connection, then closes it without sending anything."""

    def __init__(self):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.bind(("127.0.0.1", 0))
        self._sock.listen(1)
        self.port = self._sock.getsockname()[1]
        self._stop = False
        self._thread = threading.Thread(target=self._accept_loop, daemon=True)
        self._thread.start()

    def _accept_loop(self):
        self._sock.settimeout(0.1)
        while not self._stop:
            try:
                conn, _ = self._sock.accept()
                conn.close()
            except socket.timeout:
                continue
            except OSError:
                break

    def close(self):
        self._stop = True
        self._thread.join(timeout=2)
        self._sock.close()


class TestSocketCleanupOnFailedHandshake(unittest.TestCase):
    def setUp(self):
        self.server = _RejectingServer()
        self.opened_sockets = []
        original_connect = RedBoxClient._connect

        def _tracking_connect(client_self):
            original_connect(client_self)
            self.opened_sockets.append(client_self.sock)

        self._patch = unittest.mock.patch.object(RedBoxClient, "_connect", _tracking_connect)
        self._patch.start()

    def tearDown(self):
        self._patch.stop()
        self.server.close()

    def test_socket_is_closed_when_handshake_fails(self):
        # An immediate server-side close can surface as a graceful b'' read
        # (RuntimeError, per _handshake's own check) or as a TCP RST
        # (ConnectionResetError) depending on OS/timing -- either way, the
        # socket must not leak.
        with self.assertRaises((RuntimeError, ConnectionError, OSError)):
            RedBoxClient(host="127.0.0.1", port=self.server.port, db_name="x", dim=3)

        self.assertEqual(len(self.opened_sockets), 1)
        # fileno() is -1 on a closed socket -- the documented, portable way
        # to check, since close() is idempotent and doesn't always raise.
        self.assertEqual(self.opened_sockets[0].fileno(), -1)

    def test_create_hnsw_closes_socket_when_handshake_fails(self):
        # An immediate server-side close can surface as a graceful b'' read
        # (RuntimeError, per _handshake's own check) or as a TCP RST
        # (ConnectionResetError) depending on OS/timing -- either way, the
        # socket must not leak.
        with self.assertRaises((RuntimeError, ConnectionError, OSError)):
            RedBoxClient.create_hnsw(host="127.0.0.1", port=self.server.port, db_name="x", dim=3)

        self.assertEqual(len(self.opened_sockets), 1)
        self.assertEqual(self.opened_sockets[0].fileno(), -1)


if __name__ == "__main__":
    unittest.main()
