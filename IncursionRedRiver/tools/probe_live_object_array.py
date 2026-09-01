#!/usr/bin/env python3
"""Read-only live-process probe for the UE chunked GUObjectArray.

This is a diagnostic companion to GameAccess.cpp. It scans writable PE sections
for structurally valid TUObjectArray headers and verifies stable UObject indices.
"""

from __future__ import annotations

import argparse
import ctypes
import struct
import sys
from ctypes import wintypes


PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
IMAGE_SCN_MEM_WRITE = 0x80000000
IMAGE_SCN_MEM_READ = 0x40000000
OBJECTS_PER_CHUNK = 0x10000
KNOWN_INDICES = (0x1E, 0x443, 0x4B8, 0x76D, 0xFCE)

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.ReadProcessMemory.argtypes = (
    wintypes.HANDLE,
    wintypes.LPCVOID,
    wintypes.LPVOID,
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
)
kernel32.ReadProcessMemory.restype = wintypes.BOOL
kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)


class Reader:
    def __init__(self, pid: int):
        self.handle = kernel32.OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid
        )
        if not self.handle:
            raise OSError(ctypes.get_last_error(), "OpenProcess failed")

    def close(self) -> None:
        if self.handle:
            kernel32.CloseHandle(self.handle)
            self.handle = None

    def read(self, address: int, size: int) -> bytes | None:
        if not address or size <= 0:
            return None
        buffer = ctypes.create_string_buffer(size)
        transferred = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(
            self.handle,
            ctypes.c_void_p(address),
            buffer,
            size,
            ctypes.byref(transferred),
        ):
            return None
        if transferred.value != size:
            return None
        return buffer.raw

    def u64(self, address: int) -> int:
        data = self.read(address, 8)
        return struct.unpack("<Q", data)[0] if data else 0

    def i32(self, address: int) -> int:
        data = self.read(address, 4)
        return struct.unpack("<i", data)[0] if data else -1


def pe_sections(reader: Reader, base: int):
    header = reader.read(base, 0x1000)
    if not header or header[:2] != b"MZ":
        raise RuntimeError("target module has no valid DOS header")
    pe_offset = struct.unpack_from("<I", header, 0x3C)[0]
    if pe_offset + 0x108 > len(header):
        header = reader.read(base, pe_offset + 0x1000)
    if not header or header[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise RuntimeError("target module has no valid PE header")
    section_count = struct.unpack_from("<H", header, pe_offset + 6)[0]
    optional_size = struct.unpack_from("<H", header, pe_offset + 20)[0]
    section_offset = pe_offset + 24 + optional_size
    required = section_offset + section_count * 40
    if required > len(header):
        header = reader.read(base, required)
    if not header:
        raise RuntimeError("could not read PE section table")
    for index in range(section_count):
        offset = section_offset + index * 40
        name = header[offset : offset + 8].split(b"\0", 1)[0].decode(errors="replace")
        virtual_size, virtual_address = struct.unpack_from("<II", header, offset + 8)
        characteristics = struct.unpack_from("<I", header, offset + 36)[0]
        yield name, base + virtual_address, virtual_size, characteristics


def object_at(reader: Reader, chunks: int, count: int, stride: int, index: int) -> int:
    if index < 0 or index >= count:
        return 0
    chunk = reader.u64(chunks + (index // OBJECTS_PER_CHUNK) * 8)
    if not chunk:
        return 0
    return reader.u64(chunk + (index % OBJECTS_PER_CHUNK) * stride)


def score_candidate(
    reader: Reader, base: int, image_size: int, chunks: int, count: int, stride: int
) -> int:
    score = 0
    for index in KNOWN_INDICES:
        obj = object_at(reader, chunks, count, stride, index)
        if not obj:
            continue
        internal_index = reader.i32(obj + 0xC)
        vtable = reader.u64(obj)
        if internal_index == index and base <= vtable < base + image_size:
            score += 1
    return score


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("pid", type=int)
    parser.add_argument("base", type=lambda value: int(value, 0))
    parser.add_argument("image_size", type=lambda value: int(value, 0))
    args = parser.parse_args()

    reader = Reader(args.pid)
    try:
        candidates = []
        for name, address, size, flags in pe_sections(reader, args.base):
            if not (flags & IMAGE_SCN_MEM_READ and flags & IMAGE_SCN_MEM_WRITE):
                continue
            print(f"Scanning {name or '<unnamed>'} at 0x{address:X} (0x{size:X} bytes)")
            for block_offset in range(0, size, 4 * 1024 * 1024):
                block_size = min(4 * 1024 * 1024, size - block_offset)
                block = reader.read(address + block_offset, block_size)
                if not block:
                    continue
                for offset in range(0, max(0, len(block) - 0x20), 8):
                    chunks = struct.unpack_from("<Q", block, offset)[0]
                    capacity, count, max_chunks, num_chunks = struct.unpack_from(
                        "<iiii", block, offset + 0x10
                    )
                    if not (0x1000 <= count <= 2_000_000):
                        continue
                    if not (count <= capacity <= 4_000_000):
                        continue
                    if not (1 <= num_chunks <= max_chunks <= 512):
                        continue
                    if num_chunks < (count + OBJECTS_PER_CHUNK - 1) // OBJECTS_PER_CHUNK:
                        continue
                    for stride in (0x18, 0x20):
                        score = score_candidate(
                            reader, args.base, args.image_size, chunks, count, stride
                        )
                        if score:
                            candidates.append(
                                (
                                    score,
                                    address + block_offset + offset,
                                    chunks,
                                    count,
                                    capacity,
                                    num_chunks,
                                    stride,
                                )
                            )
        if not candidates:
            print("No structurally valid object array found.", file=sys.stderr)
            return 1
        candidates.sort(reverse=True)
        for score, root, chunks, count, capacity, num_chunks, stride in candidates[:10]:
            print(
                f"score={score}/{len(KNOWN_INDICES)} root=0x{root:X} "
                f"rva=0x{root - args.base:X} chunks=0x{chunks:X} "
                f"objects={count}/{capacity} chunks={num_chunks} stride=0x{stride:X}"
            )
        return 0 if candidates[0][0] >= 3 else 2
    finally:
        reader.close()


if __name__ == "__main__":
    raise SystemExit(main())
