#!/usr/bin/env python3
"""Recover the PC and Xbox OGRE RenderSystem primary vtables.

This script deliberately limits itself to facts that can be reproduced from
the binaries: PE mappings, MSVC RTTI Complete Object Locators, executable
vtable targets, and literal-string xrefs for the frame anchors.

Usage:
    python3 tools/pc_re/align_render_vtables.py \
        --d3d9 PC/RenderSystem_Direct3D9.dll --gl PC/RenderSystem_GL.dll \
        --ogre-main PC/OgreMain.dll --guest-image IMAGE
  PC     the Torchlight PC install (reference binaries)
  IMAGE  guest image from tools/xex/xex_dump.py
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

from pe_utils import PE

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "guest_re"))
from guest_image import BASE, GuestImage  # noqa: E402
import rtti_vtable  # noqa: E402


@dataclass
class Table:
    binary: str
    class_name: str
    image_base: int
    type_name: int
    type_descriptor: int
    complete_object_locator: int | None
    col_pointer: int | None
    vtable: int
    entries: list[int]
    next_word: int


def u32le(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u32be(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def map_pe(pe: PE) -> bytearray:
    """Map a disk PE into its RVA layout without applying relocations."""
    image = bytearray(pe.size_of_image)
    header_end = min((section["raw_ptr"] for section in pe.sections), default=0)
    image[:header_end] = pe.data[:header_end]
    for section in pe.sections:
        source = section["raw_ptr"]
        size = min(section["raw_size"], len(pe.data) - source)
        destination = section["virtual_address"]
        image[destination : destination + size] = pe.data[source : source + size]
    return image


def executable_ranges(pe: PE) -> list[tuple[int, int]]:
    return [
        (
            pe.image_base + section["virtual_address"],
            pe.image_base
            + section["virtual_address"]
            + max(section["virtual_size"], section["raw_size"]),
        )
        for section in pe.sections
        if section["characteristics"] & 0x20000000
    ]


def in_ranges(address: int, ranges: list[tuple[int, int]]) -> bool:
    return any(start <= address < end for start, end in ranges)


def all_aligned_refs(
    image: bytes | bytearray, image_base: int, target: int, endian: str
) -> list[int]:
    needle = struct.pack(endian + "I", target)
    return [
        image_base + offset
        for offset in range(0, len(image) - 3, 4)
        if image[offset : offset + 4] == needle
    ]


def recover_pc_table(path: Path, rtti_name: bytes) -> tuple[Table, bytearray, PE]:
    pe = PE(str(path))
    image = map_pe(pe)
    ranges = executable_ranges(pe)
    name_offset = image.find(rtti_name + b"\x00")
    if name_offset < 8:
        raise ValueError(f"{path}: RTTI name {rtti_name!r} was not found")
    type_name = pe.image_base + name_offset
    type_descriptor = type_name - 8

    cols: list[int] = []
    for reference in all_aligned_refs(image, pe.image_base, type_descriptor, "<"):
        col_offset = reference - pe.image_base - 12
        if col_offset < 0:
            continue
        signature, object_offset, constructor_offset = struct.unpack_from(
            "<III", image, col_offset
        )
        hierarchy = u32le(image, col_offset + 16)
        if (
            signature == 0
            and object_offset == 0
            and constructor_offset == 0
            and pe.image_base <= hierarchy < pe.image_base + len(image)
        ):
            cols.append(pe.image_base + col_offset)
    cols = sorted(set(cols))
    if len(cols) != 1:
        raise ValueError(f"{path}: expected one primary COL, found {cols}")

    candidates: list[tuple[int, int]] = []
    for col_pointer in all_aligned_refs(image, pe.image_base, cols[0], "<"):
        entry_offset = col_pointer - pe.image_base + 4
        if entry_offset + 4 <= len(image):
            first = u32le(image, entry_offset)
            if in_ranges(first, ranges):
                candidates.append((col_pointer, col_pointer + 4))
    if len(candidates) != 1:
        raise ValueError(f"{path}: expected one primary vtable, found {candidates}")

    col_pointer, vtable = candidates[0]
    entries: list[int] = []
    cursor = vtable - pe.image_base
    while cursor + 4 <= len(image):
        target = u32le(image, cursor)
        if not in_ranges(target, ranges):
            break
        entries.append(target)
        cursor += 4
    return (
        Table(
            binary=path.name,
            class_name=rtti_name.decode("ascii"),
            image_base=pe.image_base,
            type_name=type_name,
            type_descriptor=type_descriptor,
            complete_object_locator=cols[0],
            col_pointer=col_pointer,
            vtable=vtable,
            entries=entries,
            next_word=u32le(image, cursor),
        ),
        image,
        pe,
    )


def recover_exported_pc_table(path: Path, export_name: str) -> Table:
    pe = PE(str(path))
    image = map_pe(pe)
    ranges = executable_ranges(pe)
    matches = [entry for entry in pe.get_exports() if entry["name"] == export_name]
    if len(matches) != 1:
        raise ValueError(f"{path}: expected one {export_name!r} export")
    vtable = pe.image_base + matches[0]["rva"]
    entries: list[int] = []
    cursor = vtable - pe.image_base
    while cursor + 4 <= len(image):
        target = u32le(image, cursor)
        if not in_ranges(target, ranges):
            break
        entries.append(target)
        cursor += 4
    return Table(
        binary=path.name,
        class_name=".?AVRenderSystem@Ogre@@",
        image_base=pe.image_base,
        type_name=0,
        type_descriptor=0,
        complete_object_locator=None,
        col_pointer=None,
        vtable=vtable,
        entries=entries,
        next_word=u32le(image, cursor),
    )


def recover_xbox_table(img: GuestImage, rtti_name: bytes, binary: str) -> Table:
    code = rtti_vtable.text_range(img)
    name_offset = img.data.find(rtti_name + b"\x00")
    if name_offset < 8:
        raise ValueError(f"Xbox RTTI name {rtti_name!r} was not found")
    type_name = BASE + name_offset
    type_descriptor = type_name - 8
    cols = [col for col, offset in rtti_vtable.locators(img, type_descriptor) if offset == 0]
    if len(cols) != 1:
        raise ValueError(f"Xbox {rtti_name!r}: expected one primary COL, found {cols}")
    candidates = [
        ref + 4
        for ref in rtti_vtable.word_refs(img, cols[0])
        if rtti_vtable.vtable_entries(img, ref + 4, code)
    ]
    if len(candidates) != 1:
        raise ValueError(f"Xbox {rtti_name!r}: expected one vtable, found {candidates}")
    vtable = candidates[0]
    entries = rtti_vtable.vtable_entries(img, vtable, code)
    return Table(
        binary=binary,
        class_name=rtti_name.decode("ascii"),
        image_base=BASE,
        type_name=type_name,
        type_descriptor=type_descriptor,
        complete_object_locator=cols[0],
        col_pointer=vtable - 4,
        vtable=vtable,
        entries=entries,
        next_word=u32be(img.data, vtable - BASE + 4 * len(entries)),
    )


def anchor_slots_pc(
    table: Table, image: bytes | bytearray, label: bytes
) -> list[dict[str, int]]:
    offset = image.find(label + b"\x00")
    if offset < 0:
        return []
    string_va = table.image_base + offset
    needle = struct.pack("<I", string_va)
    xrefs = []
    cursor = 0
    while True:
        cursor = image.find(needle, cursor)
        if cursor < 0:
            break
        xrefs.append(table.image_base + cursor)
        cursor += 1
    starts = sorted(set(table.entries))
    results = []
    for xref in xrefs:
        preceding = [start for start in starts if start <= xref]
        if not preceding:
            continue
        owner = preceding[-1]
        slot = table.entries.index(owner)
        results.append({"string": string_va, "xref": xref, "owner": owner, "slot": slot})
    return results


def printable(table: Table) -> dict:
    result = asdict(table)
    result["count"] = len(table.entries)
    result["last_slot_address"] = table.vtable + 4 * (len(table.entries) - 1)
    del result["entries"]
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--d3d9", type=Path, required=True)
    parser.add_argument("--gl", type=Path, required=True)
    parser.add_argument("--ogre-main", type=Path, required=True)
    parser.add_argument("--guest-image", type=Path, required=True)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    d3d, d3d_image, _ = recover_pc_table(
        args.d3d9, b".?AVD3D9RenderSystem@Ogre@@"
    )
    gl, gl_image, _ = recover_pc_table(args.gl, b".?AVGLRenderSystem@Ogre@@")
    pc_base = recover_exported_pc_table(args.ogre_main, "??_7RenderSystem@Ogre@@6B@")
    xbox_image = GuestImage(str(args.guest_image))
    xbox_d3d = recover_xbox_table(
        xbox_image, b".?AVD3D9RenderSystem@Ogre@@", args.guest_image.name
    )
    xbox_base = recover_xbox_table(
        xbox_image, b".?AVRenderSystem@Ogre@@", args.guest_image.name
    )

    anchors = {
        "pc_d3d9_begin": anchor_slots_pc(
            d3d, d3d_image, b"D3D9RenderSystem::_beginFrame"
        ),
        "pc_d3d9_end": anchor_slots_pc(
            d3d, d3d_image, b"D3D9RenderSystem::_endFrame"
        ),
        "pc_gl_begin": anchor_slots_pc(gl, gl_image, b"GLRenderSystem::_beginFrame"),
    }
    payload = {
        "tables": [
            printable(pc_base),
            printable(d3d),
            printable(gl),
            printable(xbox_base),
            printable(xbox_d3d),
        ],
        "anchors": anchors,
    }
    if args.json:
        print(json.dumps(payload, indent=2))
        return

    for table in [pc_base, d3d, gl, xbox_base, xbox_d3d]:
        print(f"{table.binary}: {table.class_name}")
        print(f"  type name:  0x{table.type_name:08X}")
        if table.complete_object_locator is not None:
            print(f"  COL:        0x{table.complete_object_locator:08X}")
            print(f"  COL word:   0x{table.col_pointer:08X}")
        print(f"  vtable:     0x{table.vtable:08X}")
        print(f"  entries:    {len(table.entries)}")
        print(f"  next word:  0x{table.next_word:08X}")
    for label, records in anchors.items():
        for record in records:
            print(
                f"{label}: string 0x{record['string']:08X}, "
                f"xref 0x{record['xref']:08X}, slot {record['slot']}, "
                f"owner 0x{record['owner']:08X}"
            )


if __name__ == "__main__":
    main()
