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
        "lblSub",
        "TextField",
        "text",
        "_x",
        "_y",
        "_width",
        "_height",
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
        "hl",
        "div",
        "icon_frame",
        "icon_mc",
        "loadMovie",
        "unloadMovie",
        "textColor",
        "html",
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

    def draw_box(obj_code, w, h, bg_col, bg_alpha, border_col=None, border_w=1.0, r=2.0):
        code = b""
        if border_col is not None and border_w > 0:
            # lineStyle(border_w, border_col, 100)
            code += push_d(100.0) + push_i(border_col) + push_d(border_w) + push_i(3)
            code += obj_code + push_cp("lineStyle") + b"\x52\x17"
        else:
            # lineStyle(undefined)
            code += push_i(0) + obj_code + push_cp("lineStyle") + b"\x52\x17"

        if bg_alpha > 0:
            # beginFill(bg_col, bg_alpha)
            code += push_d(bg_alpha) + push_i(bg_col) + push_i(2)
            code += obj_code + push_cp("beginFill") + b"\x52\x17"

        if r > 0:
            # 8-point chamfer polygon:
            # (r, 0), (w-r, 0), (w, r), (w, h-r), (w-r, h), (r, h), (0, h-r), (0, r), (r, 0)
            code += push_d(0.0) + push_d(r) + push_i(2)
            code += obj_code + push_cp("moveTo") + b"\x52\x17"
            pts = [(w - r, 0.0), (w, r), (w, h - r), (w - r, h), (r, h), (0.0, h - r), (0.0, r), (r, 0.0)]
            for px, py in pts:
                code += push_d(py) + push_d(px) + push_i(2)
                code += obj_code + push_cp("lineTo") + b"\x52\x17"
        else:
            code += push_d(0.0) + push_d(0.0) + push_i(2)
            code += obj_code + push_cp("moveTo") + b"\x52\x17"
            for px, py in [(w, 0.0), (w, h), (0.0, h), (0.0, 0.0)]:
                code += push_d(py) + push_d(px) + push_i(2)
                code += obj_code + push_cp("lineTo") + b"\x52\x17"

        if bg_alpha > 0:
            code += push_i(0) + obj_code + push_cp("endFill") + b"\x52\x17"
        return code

    def make_widget(parent_parts, name, depth, x, y, w, h, label, cmd=None, arg="",
                    bg_col=0x150E09, bg_alpha=85.0, border_col=0x6E512F, border_w=1.0, r=2.0):
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

        # 3. Draw background & chamfer border
        code += draw_box(self_code, w, h, bg_col, bg_alpha, border_col, border_w, r)

        # 4. Attach "ContainerEntryText" as "lbl" at depth 1
        code += push_i(1) + push_cp("lbl") + push_cp("ContainerEntryText") + push_i(3)
        code += self_code + push_cp("attachMovie") + b"\x52\x17"

        # 5. Position lbl and clamp TextField._width so hit area stays within button
        code += set_member_d(lbl_code, "_x", 4.0)
        code += set_member_d(lbl_code, "_y", 4.0)
        code += set_member_d(tf_code, "_width", max(20.0, w - 8.0))
        code += set_member_s(tf_code, "text", label)
        code += set_member_i(lbl_code, "mouseChildren", 0)
        code += set_member_i(lbl_code, "mouseEnabled", 0)
        code += set_member_i(tf_code, "selectable", 0)
        code += set_member_i(tf_code, "mouseEnabled", 0)

        # 6. Click & RollOver handlers if interactive
        if cmd:
            code += set_member_i(self_code, "useHandCursor", 1)

            fs_url = f"FSCommand:{cmd}"
            fn_rel = push_cp(fs_url) + push_cp(arg) + b"\x9a\x01\x00\x00\x3e"
            code += self_code + push_cp("onRelease") + define_func(fn_rel) + b"\x4f"

            fn_over = set_member_d(self_code, "_alpha", 75.0) + b"\x3e"
            code += self_code + push_cp("onRollOver") + define_func(fn_over) + b"\x4f"

            fn_out = set_member_d(self_code, "_alpha", 100.0) + b"\x3e"
            code += self_code + push_cp("onRollOut") + define_func(fn_out) + b"\x4f"

        return code

    def make_item_row(parent_parts, name, depth, x, y, w, h, cmd, arg):
        parent_code = push_path(parent_parts)
        self_parts = list(parent_parts) + [name]
        self_code = push_path(self_parts)
        hl_code = push_path(self_parts + ["hl"])
        div_code = push_path(self_parts + ["div"])
        icon_frame_code = push_path(self_parts + ["icon_frame"])
        icon_mc_code = push_path(self_parts + ["icon_frame", "icon_mc"])
        lbl_code = push_path(self_parts + ["lbl"])
        lbl_sub_code = push_path(self_parts + ["lblSub"])
        tf_code = push_path(self_parts + ["lbl", "TextField"])
        tf_sub_code = push_path(self_parts + ["lblSub", "TextField"])

        code = b""
        # 1. createEmptyMovieClip(name, depth)
        code += push_i(depth) + push_cp(name) + push_i(2)
        code += parent_code + push_cp("createEmptyMovieClip") + b"\x52\x17"

        # 2. Set _x, _y
        code += set_member_d(self_code, "_x", x)
        code += set_member_d(self_code, "_y", y)

        # 3. Transparent hit area base so clicking anywhere in the row works
        code += draw_box(self_code, w, h, bg_col=0x000000, bg_alpha=1.0, border_col=None, border_w=0, r=0)

        # 4. Highlight strip: hl child movieclip at depth 1 (warm golden parchment glow)
        code += push_i(1) + push_cp("hl") + push_i(2)
        code += self_code + push_cp("createEmptyMovieClip") + b"\x52\x17"
        code += draw_box(hl_code, w, h - 2.0, bg_col=0xC6A15B, bg_alpha=30.0, border_col=0x9B7A3E, border_w=1.0, r=2.0)
        code += set_member_i(hl_code, "_visible", 0)

        # 5. Divider line at bottom: div child movieclip at depth 2
        code += push_i(2) + push_cp("div") + push_i(2)
        code += self_code + push_cp("createEmptyMovieClip") + b"\x52\x17"
        code += push_d(50.0) + push_i(0x3D2B1B) + push_d(1.0) + push_i(3)
        code += div_code + push_cp("lineStyle") + b"\x52\x17"
        code += push_d(h - 1.0) + push_d(4.0) + push_i(2)
        code += div_code + push_cp("moveTo") + b"\x52\x17"
        code += push_d(h - 1.0) + push_d(w - 4.0) + push_i(2)
        code += div_code + push_cp("lineTo") + b"\x52\x17"

        # 6. Icon frame at depth 3: x = 4, y = 2, w = 29, h = 29
        code += push_i(3) + push_cp("icon_frame") + push_i(2)
        code += self_code + push_cp("createEmptyMovieClip") + b"\x52\x17"
        code += set_member_d(icon_frame_code, "_x", 4.0)
        code += set_member_d(icon_frame_code, "_y", 2.0)
        code += draw_box(icon_frame_code, 29.0, 29.0, bg_col=0x120C07, bg_alpha=85.0, border_col=0x6E512F, border_w=1.0, r=1.0)

        # 7. Icon image inside icon_frame at depth 1: icon_mc
        code += push_i(1) + push_cp("icon_mc") + push_i(2)
        code += icon_frame_code + push_cp("createEmptyMovieClip") + b"\x52\x17"
        code += set_member_d(icon_mc_code, "_x", 1.0)
        code += set_member_d(icon_mc_code, "_y", 1.0)

        # 8. Name text field: lbl at depth 4, x = 38.0, y = 1.0 (authentic dark vanilla inventory text: 0x261906)
        code += push_i(4) + push_cp("lbl") + push_cp("ContainerEntryText") + push_i(3)
        code += self_code + push_cp("attachMovie") + b"\x52\x17"
        code += set_member_d(lbl_code, "_x", 38.0)
        code += set_member_d(lbl_code, "_y", 1.0)
        code += set_member_d(tf_code, "_width", w - 44.0)
        code += set_member_s(tf_code, "text", "")
        code += set_member_i(lbl_code, "mouseChildren", 0)
        code += set_member_i(lbl_code, "mouseEnabled", 0)
        code += set_member_i(tf_code, "selectable", 0)
        code += set_member_i(tf_code, "mouseEnabled", 0)
        code += set_member_i(tf_code, "html", 0)
        code += set_member_i(tf_code, "textColor", 0x261906)

        # 9. Subtitle text field: lblSub at depth 5, x = 38.0, y = 16.0 (muted dark tan/brown: 0x5C4430)
        code += push_i(5) + push_cp("lblSub") + push_cp("ContainerEntryText") + push_i(3)
        code += self_code + push_cp("attachMovie") + b"\x52\x17"
        code += set_member_d(lbl_sub_code, "_x", 38.0)
        code += set_member_d(lbl_sub_code, "_y", 16.0)
        code += set_member_d(tf_sub_code, "_width", w - 44.0)
        code += set_member_s(tf_sub_code, "text", "")
        code += set_member_i(lbl_sub_code, "mouseChildren", 0)
        code += set_member_i(lbl_sub_code, "mouseEnabled", 0)
        code += set_member_i(tf_sub_code, "selectable", 0)
        code += set_member_i(tf_sub_code, "mouseEnabled", 0)
        code += set_member_i(tf_sub_code, "html", 0)
        code += set_member_i(tf_sub_code, "textColor", 0x5C4430)

        # 10. Interactive events: Click & Hover
        code += set_member_i(self_code, "useHandCursor", 1)
        fs_url = f"FSCommand:{cmd}"
        fn_rel = push_cp(fs_url) + push_cp(arg) + b"\x9a\x01\x00\x00\x3e"
        code += self_code + push_cp("onRelease") + define_func(fn_rel) + b"\x4f"

        fn_over = set_member_i(hl_code, "_visible", 1) + b"\x3e"
        code += self_code + push_cp("onRollOver") + define_func(fn_over) + b"\x4f"

        # onRollOut: if not selected, hide hl
        fn_out = set_member_i(hl_code, "_visible", 0) + b"\x3e"
        code += self_code + push_cp("onRollOut") + define_func(fn_out) + b"\x4f"

        return code

    # Build the body of _root.DAOTransmog_InitUI()
    init_body = b""

    # A. Top-right toggle button: _root.DAOTransmog_mc at depth 400
    # Styled exactly like the native "Sort by type" button, in its original position (660, 101)
    init_body += make_widget(
        [], "DAOTransmog_mc", 400,
        660.0, 101.0, 96.0, 21.0,
        "Transmog [T]", "DAOTransmog_Toggle", "",
        bg_col=0x150E09, bg_alpha=85.0, border_col=0x6E512F, border_w=1.0, r=2.0
    )

    # B. Wardrobe Browser container: _root.DAOTransmog_Browser at depth 401
    # Transparent background (bg_alpha = 0.0) so the book parchment and embossed backpack artwork show through!
    browser_parts = ["DAOTransmog_Browser"]
    browser_code = push_path(browser_parts)
    init_body += push_i(401) + push_cp("DAOTransmog_Browser") + push_i(2)
    init_body += push_root() + push_cp("createEmptyMovieClip") + b"\x52\x17"
    init_body += set_member_d(browser_code, "_x", 534.0)
    init_body += set_member_d(browser_code, "_y", 168.0)
    init_body += set_member_i(browser_code, "_visible", 0)
    init_body += draw_box(browser_code, 366.0, 448.0, 0x1A110B, 0.0, 0x3D2B1B, 0.5, r=0)

    # C. 6 Slot Selector Tabs (y = 6, h = 22)
    # Refined antique tabs with fine 1px bronze borders and chamfered corners
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
            bg_col=0x150E08, bg_alpha=85.0, border_col=0x5C4328, border_w=1.0, r=2.0
        )

    # D. Header & 3 Outfit Preset Badges (y = 32, h = 20)
    init_body += make_widget(
        browser_parts, "hdr", 16,
        6.0, 32.0, 240.0, 20.0,
        "Slot: Chest Armor", None, "",
        bg_col=0x120C07, bg_alpha=80.0, border_col=0x5C4328, border_w=1.0, r=2.0
    )
    # Presets formatted as elegant Roman numerals: [ I ], [ II ], [ III ]
    for pidx, px in enumerate([250.0, 288.0, 326.0]):
        preset_label = ["I", "II", "III"][pidx]
        init_body += make_widget(
            browser_parts, f"pre{pidx}", 17 + pidx,
            px, 32.0, 34.0, 20.0,
            preset_label, "DAOTransmog_Preset", str(pidx),
            bg_col=0x150E08, bg_alpha=85.0, border_col=0x5C4328, border_w=1.0, r=2.0
        )

    # E. 8 Appearance Catalog Rows (row0 .. row7, y = 56 + i * 34, h = 33)
    # Designed to look like native inventory item rows with icon slots and dividers
    for r in range(8):
        init_body += make_item_row(
            browser_parts, f"row{r}", 20 + r,
            6.0, 56.0 + r * 34.0, 354.0, 33.0,
            "DAOTransmog_ClickRow", str(r)
        )

    # F. Pagination Row (y = 332, h = 22)
    init_body += make_widget(
        browser_parts, "pgPrev", 30,
        6.0, 332.0, 74.0, 22.0,
        "< Prev", "DAOTransmog_Page", "-1",
        bg_col=0x150E08, bg_alpha=85.0, border_col=0x5C4328, border_w=1.0, r=2.0
    )
    init_body += make_widget(
        browser_parts, "pgInfo", 31,
        84.0, 332.0, 198.0, 22.0,
        "Page 1 / 1", None, "",
        bg_col=0x120C07, bg_alpha=80.0, border_col=0x5C4328, border_w=1.0, r=2.0
    )
    init_body += make_widget(
        browser_parts, "pgNext", 32,
        286.0, 332.0, 74.0, 22.0,
        "Next >", "DAOTransmog_Page", "1",
        bg_col=0x150E08, bg_alpha=85.0, border_col=0x5C4328, border_w=1.0, r=2.0
    )

    # G. Material & Sync Row (y = 358, h = 22)
    init_body += make_widget(
        browser_parts, "matPrev", 33,
        6.0, 358.0, 28.0, 22.0,
        "<", "DAOTransmog_Mat", "-1",
        bg_col=0x150E08, bg_alpha=85.0, border_col=0x5C4328, border_w=1.0, r=2.0
    )
    init_body += make_widget(
        browser_parts, "matInfo", 34,
        38.0, 358.0, 210.0, 22.0,
        "Mat: Default", None, "",
        bg_col=0x120C07, bg_alpha=80.0, border_col=0x5C4328, border_w=1.0, r=2.0
    )
    init_body += make_widget(
        browser_parts, "matNext", 35,
        252.0, 358.0, 28.0, 22.0,
        ">", "DAOTransmog_Mat", "1",
        bg_col=0x150E08, bg_alpha=85.0, border_col=0x5C4328, border_w=1.0, r=2.0
    )
    init_body += make_widget(
        browser_parts, "syncBtn", 36,
        284.0, 358.0, 76.0, 22.0,
        "Sync All", "DAOTransmog_SyncAll", "",
        bg_col=0x150E08, bg_alpha=85.0, border_col=0x5C4328, border_w=1.0, r=2.0
    )

    # H. Heraldry / Helmet Visibility & Zoom Row (y = 384, h = 22)
    init_body += make_widget(
        browser_parts, "herPrev", 37,
        6.0, 384.0, 28.0, 22.0,
        "<", "DAOTransmog_Her", "-1",
        bg_col=0x150E08, bg_alpha=85.0, border_col=0x5C4328, border_w=1.0, r=2.0
    )
    init_body += make_widget(
        browser_parts, "herInfo", 38,
        38.0, 384.0, 210.0, 22.0,
        "Heraldry: Default", "DAOTransmog_Her", "1",
        bg_col=0x120C07, bg_alpha=80.0, border_col=0x5C4328, border_w=1.0, r=2.0
    )
    init_body += make_widget(
        browser_parts, "herNext", 39,
        252.0, 384.0, 28.0, 22.0,
        ">", "DAOTransmog_Her", "1",
        bg_col=0x150E08, bg_alpha=85.0, border_col=0x5C4328, border_w=1.0, r=2.0
    )
    init_body += make_widget(
        browser_parts, "zoomBtn", 40,
        284.0, 384.0, 76.0, 22.0,
        "Zoom", "DAOTransmog_ZoomToggle", "",
        bg_col=0x150E08, bg_alpha=85.0, border_col=0x5C4328, border_w=1.0, r=2.0
    )

    # I. Action Bar (y = 412, h = 26)
    # Refined button frames with rich borders
    init_body += make_widget(
        browser_parts, "applyBtn", 41,
        6.0, 412.0, 114.0, 26.0,
        "Apply Outfit", "DAOTransmog_Apply", "",
        bg_col=0x24160C, bg_alpha=90.0, border_col=0xB8934C, border_w=1.2, r=2.0
    )
    init_body += make_widget(
        browser_parts, "resetSlotBtn", 42,
        126.0, 412.0, 114.0, 26.0,
        "Reset Slot", "DAOTransmog_ResetSlot", "",
        bg_col=0x150E08, bg_alpha=85.0, border_col=0x5C4328, border_w=1.0, r=2.0
    )
    init_body += make_widget(
        browser_parts, "resetAllBtn", 43,
        246.0, 412.0, 114.0, 26.0,
        "Discard", "DAOTransmog_Discard", "",
        bg_col=0x150E08, bg_alpha=85.0, border_col=0x5C4328, border_w=1.0, r=2.0
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
