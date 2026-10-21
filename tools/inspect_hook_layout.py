#!/usr/bin/env python3
"""Read-only check of the supplied GTA SA executable's hook layout."""
import argparse
import struct
from pathlib import Path

REQUEST = 0x4087E0
LOAD = 0x40E3A0
REQUEST_BYTES = bytes.fromhex("53 8B 5C 24 0C 55 8B 6C 24 0C 56 57")
LOAD_BYTES = bytes.fromhex("E9 FB 8C 15 01")

def inspect(path):
    data = Path(path).read_bytes()
    if data[:2] != b"MZ":
        raise ValueError("Not an MZ executable")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe+4] != b"PE\0\0":
        raise ValueError("Invalid PE signature")
    count = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    if struct.unpack_from("<H", data, opt)[0] != 0x10B:
        raise ValueError("Expected 32-bit PE")
    base = struct.unpack_from("<I", data, opt + 28)[0]
    size_of_image = struct.unpack_from("<I", data, opt + 56)[0]
    if base != 0x400000:
        raise ValueError(f"Unexpected image base {base:#x}")
    sections = []
    for i in range(count):
        pos = opt + opt_size + i*40
        name = data[pos:pos+8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, pos+8)
        flags = struct.unpack_from("<I", data, pos+36)[0]
        sections.append((name,vaddr,max(vsize,rawsize),rawptr,rawsize,flags))
    def location(address, size=1):
        rva = address - base
        for name, va, vs, raw, rs, flags in sections:
            if va <= rva and rva + size <= va + vs and rva + size <= va + rs:
                offset = raw+rva-va
                return name, flags, data[offset:offset+size]
        raise ValueError(f"Unmapped raw bytes at {address:#x}")
    _, flags, req = location(REQUEST,len(REQUEST_BYTES))
    req_ok = req == REQUEST_BYTES and bool(flags & 0x20000000)
    name, flags, load = location(LOAD,len(LOAD_BYTES))
    load_ok = load == LOAD_BYTES and bool(flags & 0x20000000)
    destination = LOAD + 5 + struct.unpack_from("<i",load,1)[0] if load[0] == 0xE9 else None
    dest_ok = False
    if destination is not None and base <= destination < base + size_of_image:
        dest_name, dest_flags, _ = location(destination)
        dest_ok = bool(dest_flags & 0x20000000)
    else:
        dest_name = "-"
    print(f"Request: {REQUEST:#010x}, signature={'MATCH' if req_ok else 'MISMATCH'}, bytes={req.hex(' ')}")
    print(f"Load:    {LOAD:#010x}, signature={'MATCH' if load_ok else 'MISMATCH'}, bytes={load.hex(' ')}")
    print(f"Load E9 destination: {destination:#010x} ({dest_name}), executable section={dest_ok}" if destination is not None else "Load entry is not E9")
    print("Disk signatures verified; runtime page protections still require in-game validation." if req_ok and load_ok and dest_ok else "UNSUPPORTED: do not force these hooks.")
    return 0 if req_ok and load_ok and dest_ok else 1

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    args = parser.parse_args()
    raise SystemExit(inspect(args.exe))
