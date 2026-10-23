import importlib.util
from pathlib import Path
import unittest


MODULE_PATH = Path(__file__).parents[1] / "chmp_scan.py"
SPEC = importlib.util.spec_from_file_location("chmp_scan", MODULE_PATH)
chmp_scan = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(chmp_scan)


class OneByteSocket:
    def __init__(self, data):
        self.data = data
        self.offset = 0

    def recv(self, requested):
        if self.offset >= len(self.data):
            return b""
        result = self.data[self.offset:self.offset + min(requested, 1)]
        self.offset += len(result)
        return result


class ChunkedReadTests(unittest.TestCase):
    def test_chunk_trailers_are_read_across_short_tcp_reads(self):
        wire = b"4\r\nDATA\r\n3\r\n123\r\n0\r\n\r\n"
        self.assertEqual(chmp_scan.read_chunked(OneByteSocket(wire)), b"DATA123")


if __name__ == "__main__":
    unittest.main()
