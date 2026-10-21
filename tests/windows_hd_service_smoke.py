#!/usr/bin/env python3
r"""Windows-only end-to-end named-pipe smoke test for ASC_HDService64.exe.

Usage: py -3 tests\windows_hd_service_smoke.py dist\ASC_HDService64.exe
No third-party Python packages are required.
"""
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import time

PIPE = r"\\.\pipe\AdaptiveStreaming.HD.v2"
MAGIC, VERSION = 0x54534148, 2
FRAME = struct.Struct("<IHHQI")
ASSET = struct.Struct("<IIIIQ")


def make_dds(width=4, height=4) -> bytes:
    h = bytearray(128)
    h[0:4] = b"DDS "
    def w(at, value):
        struct.pack_into("<I", h, at, value)
    w(4, 124)
    w(8, 0x1007)
    w(12, height)
    w(16, width)
    w(28, 1)
    w(76, 32)
    w(80, 4)
    w(84, 0x31545844)
    w(108, 0x1000)
    return bytes(h) + b"\x00" * (((width + 3) // 4) * ((height + 3) // 4) * 8)


def read_exact(f, size):
    data = bytearray()
    while len(data) < size:
        chunk = f.read(size - len(data))
        if not chunk:
            raise RuntimeError(f"Pipe closed after {len(data)} of {size} bytes")
        data.extend(chunk)
    return bytes(data)


def exchange(command, payload=b""):
    # The single-instance service recreates its pipe between requests.
    # Retry connection establishment only; never mask a truncated reply.
    deadline = time.monotonic() + 5
    while True:
        try:
            connection = open(PIPE, "r+b", buffering=0)
            break
        except OSError as error:
            if (error.errno not in (2, 13) and
                    getattr(error, "winerror", None) != 231):
                raise
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.01)
    with connection as f:
        f.write(FRAME.pack(MAGIC, VERSION, command, 17, len(payload)))
        if payload:
            f.write(payload)
        magic, version, response, request, size = FRAME.unpack(
            read_exact(f, FRAME.size))
        assert (magic, version, request) == (MAGIC, VERSION, 17)
        assert size < 70 * 1024 * 1024
        return response, read_exact(f, size)


def main():
    if os.name != "nt":
        raise SystemExit("Windows-only smoke test")
    if len(sys.argv) != 2:
        raise SystemExit("Pass the path to ASC_HDService64.exe")
    service = pathlib.Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="asc-hd-test-") as folder:
        (pathlib.Path(folder) / "smoketest.dds").write_bytes(make_dds())
        large = make_dds(512, 512)
        (pathlib.Path(folder) / "large.dds").write_bytes(large)
        p = subprocess.Popen([str(service), folder])
        try:
            for _ in range(100):
                if p.poll() is not None:
                    raise RuntimeError(f"Service exited early: {p.returncode}")
                try:
                    cmd, payload = exchange(1)
                    assert cmd == 5 and payload == b""
                    break
                except OSError:
                    time.sleep(0.1)
            else:
                raise RuntimeError("HD service pipe did not become ready")
            assert exchange(2, struct.pack("<I", 0)) == (2, b"large\nsmoketest\n")
            assert exchange(2, struct.pack("<I", 1)) == (2, b"smoketest\n")
            assert exchange(2, struct.pack("<I", 2)) == (2, b"")
            cmd, payload = exchange(3, b"smoketest")
            assert cmd == 6
            assert ASSET.unpack(payload[:ASSET.size]) == (
                4, 4, 1, 0x31545844, 8)
            assert payload[ASSET.size:] == b"\x00" * 8
            cmd, payload = exchange(3, b"large")
            assert cmd == 6
            assert ASSET.unpack(payload[:ASSET.size]) == (
                512, 512, 1, 0x31545844, len(large) - 128)
            assert payload[ASSET.size:] == large[128:]
            # Replacement files must not be served with stale descriptors.
            (pathlib.Path(folder) / "large.dds").write_bytes(make_dds())
            assert exchange(3, b"large") == (4, b"")
            assert exchange(3, b"missing") == (4, b"")
            print("PASS: HD service ping/pagination/small+large DDS/stale+missing assets")
        finally:
            p.terminate()
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()


if __name__ == "__main__":
    main()
