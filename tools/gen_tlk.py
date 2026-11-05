"""Compiles translation CSVs into DAOLib-compatible binary TLK files.
GFF V4.0 PC, type 'TLK ' V0.2, string list field 19001, id 19002, text 19003.
"""

import csv
import os
import struct
import sys
from pathlib import Path

STRING_LIST, STRING_ID, STRING_TEXT = 19001, 19002, 19003
NONE = 0xFFFFFFFF


def hash_id(strref: int) -> int:
    """The game's hash of a string id (CTlkTable's find, 0x004E47F0)."""
    a = ((strref << 15) + (~strref & NONE)) & NONE
    c = (a >> 12) ^ a
    a = (c * 5) & NONE
    c = (a >> 4) ^ a
    c = (c * 0x809) & NONE
    return (c >> 16) ^ c


def build_tlk_bytes(strings: list[tuple[int, str]]) -> bytes:
    templates = [
        (b"TLK ", [(STRING_LIST, 1, 0x8000 | 0x4000, 0)], 4),
        (b"STRN", [(STRING_ID, 4, 0, 0), (STRING_TEXT, 14, 0, 4)], 8),
    ]
    fields_at = 28 + 16 * len(templates)
    decls = tmpl = b""
    for label, fields, size in templates:
        tmpl += struct.pack("<4sIII", label, len(fields), fields_at + len(decls), size)
        for flabel, t, flags, off in fields:
            decls += struct.pack("<III", flabel, (flags << 16) | t, off)
    data_offset = fields_at + len(decls)
    head = b"GFF V4.0PC  TLK V0.2" + struct.pack("<II", len(templates), data_offset)

    texts = dict(strings)
    if len(texts) != len(strings) or NONE in texts:
        raise ValueError("Duplicate ID or invalid 0xFFFFFFFF ID")
    if not texts:
        return head + tmpl + decls + struct.pack("<I", NONE)

    count = len(texts) * 4 // 3
    slots = [NONE] * count
    for strref in sorted(texts):
        at = hash_id(strref) % count
        while slots[at] != NONE:
            at = (at + 1) % count
        slots[at] = strref

    body = bytearray(struct.pack("<II", 4, count))
    listed = len(body)
    body += bytes(8 * count)
    first = {}
    for i, strref in enumerate(slots):
        offset = NONE
        text = texts.get(strref, "")
        if strref != NONE and text:
            if text not in first:
                body += b"\xff" * (-len(body) % 4)
                first[text] = len(body)
                raw = (text + "\0").encode("utf-16-le")
                body += struct.pack("<I", len(raw) // 2) + raw
            offset = first[text]
        struct.pack_into("<II", body, listed + 8 * i, strref, offset)

    return head + tmpl + decls + bytes(body)


def read_csv(path: Path) -> list[tuple[int, str]]:
    out = []
    with open(path, encoding="utf-8-sig", newline="") as f:
        for row in csv.reader(f):
            if not row or not row[0].strip():
                continue
            id_str = row[0].strip()
            if not id_str.lstrip("-").isdigit():
                continue
            out.append((int(id_str), row[1] if len(row) > 1 else ""))
    return out


def compile_file(csv_path: Path, tlk_path: Path) -> None:
    strings = read_csv(csv_path)
    data = build_tlk_bytes(strings)
    tlk_path.parent.mkdir(parents=True, exist_ok=True)
    with open(tlk_path, "wb") as f:
        f.write(data)
    print(f"Compiled {len(strings)} strings from {csv_path.name} -> {tlk_path.name}")


def main() -> None:
    repo_root = Path(__file__).resolve().parent.parent
    assets_dir = repo_root / "assets" / "tlk"
    
    if len(sys.argv) >= 3 and sys.argv[1] == "--write":
        csv_p = Path(sys.argv[2])
        tlk_p = Path(sys.argv[3])
        compile_file(csv_p, tlk_p)
        return

    # Default batch mode: compile all strings_<locale>.csv into DAOTransmog_<locale>.tlk
    if assets_dir.exists():
        for csv_file in assets_dir.glob("strings_*.csv"):
            locale = csv_file.stem.replace("strings_", "")
            tlk_file = assets_dir / f"DAOTransmog_{locale}.tlk"
            compile_file(csv_file, tlk_file)


if __name__ == "__main__":
    main()
