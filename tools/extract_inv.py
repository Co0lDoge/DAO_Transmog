import struct
import os

patch_path = r"D:\Entertainment\BlackFlag\Dragon Age Origins\packages\core\patch\patch003.erf"
with open(patch_path, "rb") as f:
    header = f.read(16).decode("utf-16-le")
    entry_count, year, day, desc_id = struct.unpack("<IIII", f.read(16))
    f.seek(32)
    off_str, str_count, off_keys, off_res = struct.unpack("<IIII", f.read(16))
    print(f"ERF header: {header}, entries: {entry_count}, off_keys: {off_keys}, off_res: {off_res}")

    # In ERF V2.0, filenames are UTF-16
    f.seek(off_keys)
    keys = []
    for i in range(entry_count):
        # 64 bytes or 128 bytes name?
        name_bytes = f.read(64)
        name = name_bytes.decode("ascii", errors="ignore").split("\x00")[0]
        res_id, res_type, res_flags = struct.unpack("<IHH", f.read(8))
        keys.append((name, res_id, res_type))

    f.seek(off_res)
    res_info = []
    for i in range(entry_count):
        offset, length = struct.unpack("<II", f.read(8))
        res_info.append((offset, length))

    for (name, res_id, res_type), (offset, length) in zip(keys, res_info):
        if "inventory" in name.lower():
            print(f"Found: {name} at {offset}, len {length}")
            f.seek(offset)
            data = f.read(length)
            with open(os.path.join(r"d:\Project\DAO_Transmog", f"{name}.gfx"), "wb") as out:
                out.write(data)
            print(f"Wrote {name}.gfx ({len(data)} bytes)")
