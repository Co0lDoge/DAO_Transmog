import struct
import zlib
import os

erf_path = r"D:\Entertainment\BlackFlag\Dragon Age Origins\packages\core\data\guiexport.erf"
patch_path = r"D:\Entertainment\BlackFlag\Dragon Age Origins\packages\core\patch\patch003.erf"

def inspect_erf(path):
    if not os.path.exists(path):
        return
    with open(path, "rb") as f:
        magic = f.read(4)
        if magic != b"ERF ":
            return
        ver = f.read(4)
        entry_count, year, day, desc_str_id = struct.unpack("<IIII", f.read(16))
        f.seek(32)
        offset_strings, str_count, offset_keys, offset_resources = struct.unpack("<IIII", f.read(16))
        
        # Read keys
        f.seek(offset_keys)
        keys = []
        for i in range(entry_count):
            name_bytes = f.read(64).split(b"\x00")[0]
            res_id, res_type, res_flags = struct.unpack("<IHH", f.read(8))
            keys.append((name_bytes.decode("ascii", errors="ignore"), res_id, res_type))
            
        # Read resource list
        f.seek(offset_resources)
        res_info = []
        for i in range(entry_count):
            offset, length = struct.unpack("<II", f.read(8))
            res_info.append((offset, length))
            
        for (name, res_id, res_type), (offset, length) in zip(keys, res_info):
            if "inventory" in name.lower():
                print(f"Found in {os.path.basename(path)}: {name} at offset {offset}, len {length}")
                f.seek(offset)
                data = f.read(length)
                with open(os.path.join(r"d:\Project\DAO_Transmog", f"{name}.gfx"), "wb") as out:
                    out.write(data)
                print(f"Extracted to {name}.gfx ({len(data)} bytes)")

inspect_erf(patch_path)
inspect_erf(erf_path)
