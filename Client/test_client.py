"""
Unit tests for RedBoxClient's wire-protocol helpers, using a fake socket
so they run without a live RedBoxDb server.
"""
import unittest

from client import RedBoxClient


class FakeSocket:
    """Minimal stand-in for socket.socket: recv() returns pre-scripted chunks."""

    def __init__(self, chunks):
        self._chunks = list(chunks)

    def recv(self, n):
        if not self._chunks:
            return b""
        chunk = self._chunks.pop(0)
        return chunk[:n]


def make_client_with_socket(sock):
    client = RedBoxClient.__new__(RedBoxClient)
    client.sock = sock
    return client


class TestRecvExact(unittest.TestCase):
    def test_assembles_response_across_multiple_chunks(self):
        client = make_client_with_socket(FakeSocket([b"ab", b"cd", b"ef"]))

        result = client._recv_exact(6)

        self.assertEqual(result, b"abcdef")
        self.assertIsInstance(result, bytes)

    def test_single_chunk_response(self):
        client = make_client_with_socket(FakeSocket([b"hello"]))

        self.assertEqual(client._recv_exact(5), b"hello")

    def test_raises_connection_error_when_server_disconnects_mid_response(self):
        client = make_client_with_socket(FakeSocket([b"ab", b""]))

        with self.assertRaises(ConnectionError):
            client._recv_exact(6)


if __name__ == "__main__":
    unittest.main()
