"""
Regression tests for unchecked recv(1) acknowledgment reads (issue #93).

Uses a fake socket to simulate the server closing the connection between
sending a request and sending its single-byte ack.
"""
import unittest

from client import RedBoxClient


class FakeSocket:
    """recv() returns pre-scripted chunks; sendall() is a no-op."""

    def __init__(self, chunks):
        self._chunks = list(chunks)

    def recv(self, n):
        if not self._chunks:
            return b""
        return self._chunks.pop(0)[:n]

    def sendall(self, data):
        pass


def make_client_with_socket(sock):
    client = RedBoxClient.__new__(RedBoxClient)
    client.sock = sock
    client.dim = 3
    return client


class TestRecvAckCheck(unittest.TestCase):
    def test_recv_ack_returns_the_ack_byte(self):
        client = make_client_with_socket(FakeSocket([b"1"]))

        self.assertEqual(client._recv_ack(), b"1")

    def test_recv_ack_raises_on_disconnect(self):
        client = make_client_with_socket(FakeSocket([b""]))

        with self.assertRaises(ConnectionError):
            client._recv_ack()

    def test_delete_raises_instead_of_silently_returning_false_on_disconnect(self):
        client = make_client_with_socket(FakeSocket([b""]))

        with self.assertRaises(ConnectionError):
            client.delete(1)

    def test_set_probes_raises_instead_of_silently_returning_false_on_disconnect(self):
        client = make_client_with_socket(FakeSocket([b""]))

        with self.assertRaises(ConnectionError):
            client.set_probes(4)

    def test_insert_raises_instead_of_silently_swallowing_disconnect(self):
        client = make_client_with_socket(FakeSocket([b""]))

        with self.assertRaises(ConnectionError):
            client.insert(1, [0.0, 0.0, 0.0])

    def test_delete_still_returns_true_false_normally(self):
        client = make_client_with_socket(FakeSocket([b"1", b"0"]))

        self.assertTrue(client.delete(1))
        self.assertFalse(client.delete(2))


if __name__ == "__main__":
    unittest.main()
