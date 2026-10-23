#!/usr/bin/env python3
"""
Canon CHMP 1.4.0 scan client — reverse-engineered from Canon PRINT app packet
captures (see README.md for the full protocol writeup).

Usage:
    python chmp_scan.py --host <printer-ip> --dpi 300 --out scan.jpg
"""
import argparse
import socket
import struct
import sys
import time


EP = "/canon/ij/command2/port3"

XML_MODE_SHIFT_BLANK = b'<?xml version="1.0" encoding="utf-8" ?><cmd xmlns:ivec="http://www.canon.com/ns/cmd/2008/07/common/" xmlns:vcn="http://www.canon.com/ns/cmd/2008/07/canon/"><ivec:contents><ivec:operation>VendorCmd</ivec:operation><ivec:param_set servicetype="scan"><ivec:jobID> </ivec:jobID><vcn:ijoperation>ModeShift</vcn:ijoperation><vcn:ijmode>1</vcn:ijmode></ivec:param_set></ivec:contents></cmd>'
XML_START_JOB = b'<?xml version="1.0" encoding="utf-8" ?><cmd xmlns:ivec="http://www.canon.com/ns/cmd/2008/07/common/"><ivec:contents><ivec:operation>StartJob</ivec:operation><ivec:param_set servicetype="scan"><ivec:jobID>00000001</ivec:jobID><ivec:bidi>1</ivec:bidi></ivec:param_set></ivec:contents></cmd>'
XML_MODE_SHIFT = b'<?xml version="1.0" encoding="utf-8" ?><cmd xmlns:ivec="http://www.canon.com/ns/cmd/2008/07/common/" xmlns:vcn="http://www.canon.com/ns/cmd/2008/07/canon/"><ivec:contents><ivec:operation>VendorCmd</ivec:operation><ivec:param_set servicetype="scan"><ivec:jobID>00000001</ivec:jobID><vcn:ijoperation>ModeShift</vcn:ijoperation><vcn:ijmode>1</vcn:ijmode></ivec:param_set></ivec:contents></cmd>'
XML_END_JOB = b'<?xml version="1.0" encoding="utf-8" ?><cmd xmlns:ivec="http://www.canon.com/ns/cmd/2008/07/common/"><ivec:contents><ivec:operation>EndJob</ivec:operation><ivec:param_set servicetype="scan"><ivec:jobID>00000001</ivec:jobID></ivec:param_set></ivec:contents></cmd>'


def log(msg):
    print(msg, flush=True)


def recv_until(sock, marker):
    data = bytearray()
    while marker not in data:
        chunk = sock.recv(1)
        if not chunk:
            raise EOFError("socket closed while reading headers")
        data.extend(chunk)
    return bytes(data)


def parse_headers(header_bytes):
    text = header_bytes.decode("iso-8859-1", "replace")
    lines = text.split("\r\n")
    status = lines[0]
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()
    return status, headers


def read_chunked(sock):
    body = bytearray()
    while True:
        line = bytearray()
        while not line.endswith(b"\r\n"):
            b = sock.recv(1)
            if not b:
                raise EOFError("socket closed while reading chunk size")
            line.extend(b)
        size_text = line.strip().split(b";", 1)[0]
        if not size_text:
            continue
        size = int(size_text, 16)
        if size == 0:
            recv_until(sock, b"\r\n")
            break
        chunk = bytearray()
        while len(chunk) < size:
            part = sock.recv(size - len(chunk))
            if not part:
                raise EOFError("socket closed while reading chunk body")
            chunk.extend(part)
        body.extend(chunk)
        # TCP may return fewer bytes than requested. Read the full CRLF
        # delimiter so larger scans cannot desynchronize the next chunk.
        trailer = recv_until(sock, b"\r\n")
        if trailer != b"\r\n":
            raise ValueError(f"bad chunk trailer: {trailer!r}")
    return bytes(body)


def read_response(sock):
    head = recv_until(sock, b"\r\n\r\n")
    status, headers = parse_headers(head)
    lower_status = status.lower()
    if "200" not in lower_status:
        body = b""
        if "content-length" in headers:
            n = int(headers.get("content-length", "0"))
            while len(body) < n:
                body += sock.recv(n - len(body))
        raise RuntimeError(f"HTTP failed: {status}; body={body[:300]!r}")

    if "chunked" in headers.get("transfer-encoding", "").lower():
        body = read_chunked(sock)
    else:
        n = int(headers.get("content-length", "0"))
        body = bytearray()
        while len(body) < n:
            part = sock.recv(n - len(body))
            if not part:
                raise EOFError("socket closed while reading body")
            body.extend(part)
        body = bytes(body)
    return status, headers, body


class Chmp:
    def __init__(self, host, timeout):
        self.host = host
        self.sock = socket.create_connection((host, 80), timeout=timeout)
        self.sock.settimeout(timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def post(self, body):
        req = (
            f"POST {EP} HTTP/1.1\r\n"
            f"Host: {self.host}\r\n"
            "X-CHMP-Version: 1.4.0\r\n"
            "X-CHMP-Timeout: 20\r\n"
            "Content-Type: application/octet-stream\r\n"
            f"Content-Length: {len(body)}\r\n"
            "Connection: Keep-Alive\r\n"
            "\r\n"
        ).encode("ascii") + body
        self.sock.sendall(req)
        return read_response(self.sock)

    def get(self):
        req = (
            f"GET {EP} HTTP/1.1\r\n"
            f"Host: {self.host}\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Connection: Keep-Alive\r\n"
            "X-CHMP-Version: 1.4.0\r\n"
            "\r\n"
        ).encode("ascii")
        self.sock.sendall(req)
        return read_response(self.sock)

    def exchange(self, body, label):
        self.post(body)
        _status, _headers, resp = self.get()
        log(f"{label}: {len(resp)} bytes {resp[:24].hex() if resp else ''}")
        return resp


def checksum_params(params):
    if not params:
        return params
    params = bytearray(params)
    params[-1] = (-sum(params[:-1])) & 0xFF
    return bytes(params)


def build_command(code, params=b""):
    params = checksum_params(params)
    return struct.pack(">HH10xH", code, 0, len(params)) + params


def cmd_start_session():
    return bytes.fromhex("db200001000000000000000000000000")


def cmd_abort_session():
    return bytes.fromhex("ef200000000000000000000000000000")


def cmd_capability_query():
    return bytes.fromhex("f3200000000000000000000000000010")


def cmd_get_dimensions():
    return bytes.fromhex("dc200000000000000000000000000008")


def cmd_scan_start_3():
    return bytes.fromhex("d9200001000000000000000000000000")


def cmd_status_3():
    return bytes.fromhex("da200000000000000000000000000008")


def cmd_read_image(block_size_64k=0x20):
    buf = bytearray(16)
    buf[0:2] = b"\xd4\x20"
    buf[12:14] = struct.pack(">H", block_size_64k)
    return bytes(buf)


def a4_params(dpi, grayscale=False):
    width = int(210.0 / 25.4 * dpi)
    height = int(297.0 / 25.4 * dpi)
    width = (width + 31) & ~31

    data = bytearray(0x38)
    data[0x00] = 0x01
    data[0x01] = 0x01
    data[0x02] = 0x01
    data[0x08:0x0A] = struct.pack(">H", dpi | 0x8000)
    data[0x0A:0x0C] = struct.pack(">H", dpi | 0x8000)
    data[0x0C:0x10] = struct.pack(">I", 0)
    data[0x10:0x14] = struct.pack(">I", 0)
    data[0x14:0x18] = struct.pack(">I", width)
    data[0x18:0x1C] = struct.pack(">I", height)
    if grayscale:
        data[0x1C] = 0x04
        data[0x1D] = 8
    else:
        data[0x1C] = 0x08
        data[0x1D] = 24
    data[0x1F] = 0x01
    data[0x20] = 0xFF
    data[0x21] = 0x82
    data[0x23] = 0x02
    data[0x24] = 0x01
    data[0x30] = 0x01
    return build_command(0xD820, bytes(data))


def status_code(resp):
    if len(resp) < 2:
        return None
    return struct.unpack(">H", resp[:2])[0]


def require_ok(resp, label):
    code = status_code(resp)
    if code != 0x0606:
        raise RuntimeError(f"{label} failed: code={code!r}, resp={resp.hex()}")


def parse_image_block(resp):
    require_ok(resp, "ReadImage")
    if len(resp) < 16:
        raise RuntimeError(f"short image response: {resp.hex()}")
    end = (resp[8] & 0x20) != 0
    data_len = struct.unpack(">I", resp[12:16])[0]
    data = resp[16:16 + data_len] if data_len and len(resp) >= 16 + data_len else b""
    return end, data_len, data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, help="printer IP address or hostname")
    parser.add_argument("--dpi", type=int, default=150)
    parser.add_argument("--gray", action="store_true")
    parser.add_argument("--out", default="scan.jpg")
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    conn = Chmp(args.host, args.timeout)
    image = bytearray()
    try:
        conn.exchange(b"\x00", "handshake 1")
        conn.exchange(b"\x00", "handshake 2")
        resp = conn.exchange(XML_MODE_SHIFT_BLANK, "ModeShift blank")
        if b"OK" not in resp:
            raise RuntimeError(f"ModeShift blank rejected: {resp[:500]!r}")
        conn.exchange(cmd_capability_query(), "Capability pre")
        resp = conn.exchange(XML_START_JOB, "StartJob")
        if b"OK" not in resp:
            raise RuntimeError(f"StartJob rejected: {resp[:500]!r}")
        resp = conn.exchange(XML_MODE_SHIFT, "ModeShift job")
        if b"OK" not in resp:
            raise RuntimeError(f"ModeShift job rejected: {resp[:500]!r}")
        resp = conn.exchange(cmd_start_session(), "StartSession")
        require_ok(resp, "StartSession")
        conn.exchange(cmd_capability_query(), "Capability session")
        resp = conn.exchange(a4_params(args.dpi, args.gray), "ScanParam3")
        require_ok(resp, "ScanParam3")
        resp = conn.exchange(cmd_scan_start_3(), "ScanStart3")
        require_ok(resp, "ScanStart3")

        deadline = time.time() + 180
        while True:
            time.sleep(0.5)
            resp = conn.exchange(cmd_status_3(), "Status3")
            require_ok(resp, "Status3")
            b8 = resp[8] if len(resp) > 8 else None
            log(f"  status byte[8]={b8!r}")
            if b8 == 0x03:
                break
            if b8 not in (0x00, 0x02):
                raise RuntimeError(f"unexpected scan status byte: {b8!r}, resp={resp.hex()}")
            if time.time() > deadline:
                raise TimeoutError("timed out waiting for scan data ready")

        conn.exchange(cmd_get_dimensions(), "GetDimensions")

        for i in range(200):
            resp = conn.exchange(cmd_read_image(0x20), f"ReadImage {i:03d}")
            end, declared_len, data = parse_image_block(resp)
            image.extend(data)
            log(f"  chunk declared={declared_len} used={len(data)} total={len(image)} end={end}")
            if end:
                break
        else:
            raise RuntimeError("ReadImage loop limit reached without end-of-scan")

        if not image.startswith(b"\xff\xd8"):
            log(f"warning: collected data does not start with JPEG SOI: {image[:16].hex()}")
        with open(args.out, "wb") as f:
            f.write(image)
        log(f"saved {len(image)} bytes to {args.out}")
        return 0
    finally:
        try:
            conn.exchange(cmd_abort_session(), "AbortSession")
            conn.exchange(XML_END_JOB, "EndJob")
            conn.exchange(b"\x00", "cleanup handshake")
        except Exception as exc:
            log(f"cleanup warning: {exc}")
        conn.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr, flush=True)
        raise
