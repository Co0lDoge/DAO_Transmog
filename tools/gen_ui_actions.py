import struct
import os


def build_actions():
    constants = []
    cp_map = {}

    def cp(s):
        if s not in cp_map:
            cp_map[s] = len(constants)
            constants.append(s)
        return cp_map[s]

    # Pre-populate core strings
    for s in [
        "_root",
        "createEmptyMovieClip",
        "attachMovie",
        "ContainerEntryText",
        "lbl",
        "TextField",
        "text",
        "_x",
        "_y",
        "_width",
        "_visible",
        "_alpha",
        "lineStyle",
        "beginFill",
        "moveTo",
        "lineTo",
        "endFill",
        "onRelease",
        "onRollOver",
        "onRollOut",
        "mouseChildren",
        "mouseEnabled",
        "selectable",
        "useHandCursor",
        "DAOTransmog_InitUI",
        "DAOTransmog_mc",
        "DAOTransmog_Browser",
        "Transmog [T]",
        "Backpack [T]",
        "",
    ]:
        cp(s)

    def push_cp(s):
        idx = cp(s)
        assert idx < 256, f"Constant pool overflow: {idx}"
        return b"\x96" + struct.pack("<H", 2) + b"\x08" + struct.pack("<B", idx)

    def push_i(val):
        return b"\x96" + struct.pack("<H", 5) + b"\x07" + struct.pack("<i", int(val))

    def push_d(val):
        # SWF stores doubles with the two 32-bit words swapped (high word first)
        raw = struct.pack("<d", float(val))
        return b"\x96" + struct.pack("<H", 9) + b"\x06" + raw[4:8] + raw[0:4]

    def define_func(body):
        # ActionDefineFunction (0x9B):
        # Header length is 5 bytes: Name ("\x00") + NumParams (uint16 = 0) + CodeSize (uint16 = len(body))
        # Followed immediately by `body`.
        return b"\x9b\x05\x00\x00\x00\x00" + struct.pack("<H", len(body)) + body

    def push_root():
        return push_cp("_root") + b"\x1c"

    def push_path(parts):
        code = push_root()
        for p in parts:
            code += push_cp(p) + b"\x4e"  # GetMember
        return code

    def set_member_d(obj_code, prop, val):
        return obj_code + push_cp(prop) + push_d(val) + b"\x4f"

    def set_member_i(obj_code, prop, val):
        return obj_code + push_cp(prop) + push_i(val) + b"\x4f"

    def set_member_s(obj_code, prop, val):
        return obj_code + push_cp(prop) + push_cp(val) + b"\x4f"

    def draw_box(obj_code, w, h, bg_col, bg_alpha, border_col, border_w=1.5):
        code = b""
        # lineStyle(border_w, border_col, 100)
        code += push_d(100.0) + push_i(border_col) + push_d(border_w) + push_i(3)
        code += obj_code + push_cp("lineStyle") + b"\x52\x17"
        # beginFill(bg_col, bg_alpha)
        code += push_d(bg_alpha) + push_i(bg_col) + push_i(2)
        code += obj_code + push_cp("beginFill") + b"\x52\x17"
        # moveTo(0, 0)
        code += push_d(0.0) + push_d(0.0) + push_i(2)
        code += obj_code + push_cp("moveTo") + b"\x52\x17"
        for px, py in [(w, 0.0), (w, h), (0.0, h), (0.0, 0.0)]:
            code += push_d(py) + push_d(px) + push_i(2)
            code += obj_code + push_cp("lineTo") + b"\x52\x17"
        # endFill()
        code += push_i(0) + obj_code + push_cp("endFill") + b"\x52\x17"
        return code

    def make_widget(parent_parts, name, depth, x, y, w, h, label, cmd=None, arg="",
                    bg_col=0x2A1C10, bg_alpha=94.0, border_col=0xC6A15B):
        parent_code = push_path(parent_parts)
        self_parts = list(parent_parts) + [name]
        self_code = push_path(self_parts)
        lbl_code = push_path(self_parts + ["lbl"])
        tf_code = push_path(self_parts + ["lbl", "TextField"])

        code = b""
        # 1. parent.createEmptyMovieClip(name, depth)
        code += push_i(depth) + push_cp(name) + push_i(2)
        code += parent_code + push_cp("createEmptyMovieClip") + b"\x52\x17"

        # 2. Set _x, _y
        code += set_member_d(self_code, "_x", x)
        code += set_member_d(self_code, "_y", y)

        # 3. Draw background & border
        code += draw_box(self_code, w, h, bg_col, bg_alpha, border_col)

        # 4. Attach "ContainerEntryText" as "lbl" at depth 1
        code += push_i(1) + push_cp("lbl") + push_cp("ContainerEntryText") + push_i(3)
        code += self_code + push_cp("attachMovie") + b"\x52\x17"

        # 5. Position lbl and clamp TextField._width so hit area stays within button
        code += set_member_d(lbl_code, "_x", 4.0)
        code += set_member_d(lbl_code, "_y", 5.0)
        code += set_member_d(tf_code, "_width", max(20.0, w - 8.0))
        code += set_member_s(tf_code, "text", label)
        # Prevent child text from capturing mouse clicks away from button
        code += set_member_i(lbl_code, "mouseChildren", 0)
        code += set_member_i(lbl_code, "mouseEnabled", 0)
        code += set_member_i(tf_code, "selectable", 0)
        code += set_member_i(tf_code, "mouseEnabled", 0)

        # 6. Click & RollOver handlers if interactive
        if cmd:
            code += set_member_i(self_code, "useHandCursor", 1)

            # In Scaleform GFx / ActionScript 2, fscommand(cmd, arg) is ActionGetURL2
            # with URL "FSCommand:<cmd>" and target <arg>.
            # Stack for ActionGetURL2 (0x9A):
            # 1. url: "FSCommand:" + cmd
            # 2. target: arg
            fs_url = f"FSCommand:{cmd}"
            fn_rel = push_cp(fs_url) + push_cp(arg) + b"\x9a\x01\x00\x00\x3e"
            code += self_code + push_cp("onRelease") + define_func(fn_rel) + b"\x4f"

            fn_over = set_member_d(self_code, "_alpha", 75.0) + b"\x3e"
            code += self_code + push_cp("onRollOver") + define_func(fn_over) + b"\x4f"

            fn_out = set_member_d(self_code, "_alpha", 100.0) + b"\x3e"
            code += self_code + push_cp("onRollOut") + define_func(fn_out) + b"\x4f"

        return code

    # Build the body of _root.DAOTransmog_InitUI()
    init_body = b""

    # A. Top-right toggle button: _root.DAOTransmog_mc at depth 400 (left of sorting switch)
    init_body += make_widget(
        [], "DAOTransmog_mc", 400,
        660.0, 101.0, 96.0, 21.0,
        "Transmog [T]", "DAOTransmog_Toggle", "",
        bg_col=0x2A1C10, bg_alpha=95.0, border_col=0xC6A15B
    )

    # B. Wardrobe Browser container: _root.DAOTransmog_Browser at depth 401
    browser_parts = ["DAOTransmog_Browser"]
    browser_code = push_path(browser_parts)
    init_body += push_i(401) + push_cp("DAOTransmog_Browser") + push_i(2)
    init_body += push_root() + push_cp("createEmptyMovieClip") + b"\x52\x17"
    init_body += set_member_d(browser_code, "_x", 534.0)
    init_body += set_member_d(browser_code, "_y", 168.0)
    init_body += set_member_i(browser_code, "_visible", 0)
    init_body += draw_box(browser_code, 366.0, 448.0, 0x1A110B, 96.0, 0xC6A15B, 1.5)

    # C. 6 Slot Selector Tabs (y = 6, h = 22)
    slot_defs = [
        ("slot0", 10, 6.0,   56.0, "Chest",   "0"),
        ("slot1", 11, 65.0,  56.0, "Gloves",  "1"),
        ("slot2", 12, 124.0, 56.0, "Boots",   "2"),
        ("slot3", 13, 183.0, 56.0, "Main",    "3"),
        ("slot4", 14, 242.0, 60.0, "Offhand", "4"),
        ("slot5", 15, 305.0, 55.0, "Helm",    "5"),
    ]
    for sname, sdepth, sx, sw, slbl, sarg in slot_defs:
        init_body += make_widget(
            browser_parts, sname, sdepth,
            sx, 6.0, sw, 22.0,
            slbl, "DAOTransmog_SelectSlot", sarg,
            bg_col=0x332214, bg_alpha=95.0, border_col=0xC6A15B
        )

    # D. Header & 3 Outfit Preset Buttons (y = 32, h = 20)
    init_body += make_widget(
        browser_parts, "hdr", 16,
        6.0, 32.0, 240.0, 20.0,
        "Slot: Chest Armor", None, "",
        bg_col=0x140D08, bg_alpha=90.0, border_col=0x5A4628
    )
    for pidx, px in enumerate([250.0, 288.0, 326.0]):
        init_body += make_widget(
            browser_parts, f"pre{pidx}", 17 + pidx,
            px, 32.0, 34.0, 20.0,
            f"P{pidx + 1}", "DAOTransmog_Preset", str(pidx),
            bg_col=0x2A1C10, bg_alpha=95.0, border_col=0xC6A15B
        )

    # E. 10 Appearance Catalog Rows (row0 .. row9, y = 56 + i * 27, h = 25)
    for r in range(10):
        init_body += make_widget(
            browser_parts, f"row{r}", 20 + r,
            6.0, 56.0 + r * 27.0, 354.0, 25.0,
            "", "DAOTransmog_ClickRow", str(r),
            bg_col=0x2B1D12, bg_alpha=92.0, border_col=0x7A6238
        )

    # F. Pagination Row (y = 330, h = 22)
    init_body += make_widget(
        browser_parts, "pgPrev", 30,
        6.0, 330.0, 74.0, 22.0,
        "< Prev", "DAOTransmog_Page", "-1"
    )
    init_body += make_widget(
        browser_parts, "pgInfo", 31,
        84.0, 330.0, 198.0, 22.0,
        "Page 1 / 1", None, "",
        bg_col=0x140D08, bg_alpha=90.0, border_col=0x5A4628
    )
    init_body += make_widget(
        browser_parts, "pgNext", 32,
        286.0, 330.0, 74.0, 22.0,
        "Next >", "DAOTransmog_Page", "1"
    )

    # G. Material & Sync Row (y = 356, h = 22)
    init_body += make_widget(
        browser_parts, "matPrev", 33,
        6.0, 356.0, 28.0, 22.0,
        "<", "DAOTransmog_Mat", "-1"
    )
    init_body += make_widget(
        browser_parts, "matInfo", 34,
        38.0, 356.0, 210.0, 22.0,
        "Mat: Default", None, "",
        bg_col=0x140D08, bg_alpha=90.0, border_col=0x5A4628
    )
    init_body += make_widget(
        browser_parts, "matNext", 35,
        252.0, 356.0, 28.0, 22.0,
        ">", "DAOTransmog_Mat", "1"
    )
    init_body += make_widget(
        browser_parts, "syncBtn", 36,
        284.0, 356.0, 76.0, 22.0,
        "Sync All", "DAOTransmog_SyncAll", ""
    )

    # H. Heraldry & Zoom Row (y = 382, h = 22)
    init_body += make_widget(
        browser_parts, "herPrev", 37,
        6.0, 382.0, 28.0, 22.0,
        "<", "DAOTransmog_Her", "-1"
    )
    init_body += make_widget(
        browser_parts, "herInfo", 38,
        38.0, 382.0, 210.0, 22.0,
        "Heraldry: Default", "DAOTransmog_Her", "1",
        bg_col=0x140D08, bg_alpha=90.0, border_col=0x5A4628
    )
    init_body += make_widget(
        browser_parts, "herNext", 39,
        252.0, 382.0, 28.0, 22.0,
        ">", "DAOTransmog_Her", "1"
    )
    init_body += make_widget(
        browser_parts, "zoomBtn", 40,
        284.0, 382.0, 76.0, 22.0,
        "Zoom", "DAOTransmog_ZoomToggle", ""
    )

    # I. Action Bar (y = 412, h = 26)
    init_body += make_widget(
        browser_parts, "applyBtn", 41,
        6.0, 412.0, 114.0, 26.0,
        "Apply Outfit", "DAOTransmog_Apply", "",
        bg_col=0x3D2814, bg_alpha=98.0, border_col=0xE6C269
    )
    init_body += make_widget(
        browser_parts, "resetSlotBtn", 42,
        126.0, 412.0, 114.0, 26.0,
        "Reset Slot", "DAOTransmog_ResetSlot", ""
    )
    init_body += make_widget(
        browser_parts, "resetAllBtn", 43,
        246.0, 412.0, 114.0, 26.0,
        "Discard", "DAOTransmog_Discard", ""
    )

    init_body += b"\x3e"  # Return

    # Define _root.DAOTransmog_InitUI = function() { ... } and call it immediately
    main_code = b""
    main_code += push_root() + push_cp("DAOTransmog_InitUI")
    main_code += define_func(init_body)
    main_code += b"\x4f"  # SetMember

    # Call _root.DAOTransmog_InitUI()
    main_code += push_i(0) + push_root() + push_cp("DAOTransmog_InitUI") + b"\x52\x17"
    main_code += b"\x00"  # End action

    # Prepend ActionConstantPool (0x88)
    cpool_data = struct.pack("<H", len(constants))
    for s in constants:
        cpool_data += s.encode("utf-8") + b"\x00"
    cpool_action = b"\x88" + struct.pack("<H", len(cpool_data)) + cpool_data

    return cpool_action + main_code


def main():
    actions = build_actions()
    header_path = r"d:\Project\DAO_Transmog\src\TransmogUiActions.h"
    os.makedirs(os.path.dirname(header_path), exist_ok=True)
    with open(header_path, "w", encoding="utf-8") as f:
        f.write("#pragma once\n\n")
        f.write("#include <cstddef>\n")
        f.write("#include <vector>\n\n")
        f.write("namespace transmog::gui\n{\n\n")
        f.write("inline const std::vector<std::byte> kTransmogUiActions = {\n")
        for i in range(0, len(actions), 16):
            chunk = actions[i:i+16]
            hex_str = ", ".join(f"std::byte{{0x{b:02X}}}" for b in chunk)
            f.write(f"    {hex_str},\n")
        f.write("};\n\n")
        f.write("} // namespace transmog::gui\n")
    print(f"Generated {header_path} ({len(actions)} action bytes, {len(actions)//1024} KB)")


if __name__ == "__main__":
    main()
