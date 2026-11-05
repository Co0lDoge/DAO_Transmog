import struct
import os

def create_rect(xmin, xmax, ymin, ymax):
    # twips (1 px = 20 twips)
    xmin_t = int(xmin * 20)
    xmax_t = int(xmax * 20)
    ymin_t = int(ymin * 20)
    ymax_t = int(ymax * 20)
    nbits = max(xmin_t.bit_length(), xmax_t.bit_length(), ymin_t.bit_length(), ymax_t.bit_length()) + 1
    if nbits < 15:
        nbits = 15

    val = nbits << (nbits * 4)
    val |= (xmin_t & ((1 << nbits) - 1)) << (nbits * 3)
    val |= (xmax_t & ((1 << nbits) - 1)) << (nbits * 2)
    val |= (ymin_t & ((1 << nbits) - 1)) << (nbits * 1)
    val |= (ymax_t & ((1 << nbits) - 1))

    total_bits = 5 + nbits * 4
    total_bytes = (total_bits + 7) // 8
    shift = total_bytes * 8 - total_bits
    val <<= shift
    return val.to_bytes(total_bytes, byteorder="big")

def make_constant_pool(strings):
    # ActionConstantPool (0x88)
    data = struct.pack("<H", len(strings))
    for s in strings:
        data += s.encode("utf-8") + b"\x00"
    return b"\x88" + struct.pack("<H", len(data)) + data

def push_cpool(idx):
    # ActionPush (0x96) type 8 (cpool uint8)
    return b"\x96" + struct.pack("<H", 2) + b"\x08" + struct.pack("<B", idx)

def push_int(val):
    # ActionPush type 7 (uint32)
    return b"\x96" + struct.pack("<H", 5) + b"\x07" + struct.pack("<I", val)

def push_double(val):
    # ActionPush type 6 (float64)
    return b"\x96" + struct.pack("<H", 9) + b"\x06" + struct.pack("<d", float(val))

def push_str(s):
    # ActionPush type 0 (null-terminated string)
    encoded = s.encode("utf-8") + b"\x00"
    return b"\x96" + struct.pack("<H", 1 + len(encoded)) + b"\x00" + encoded

def build_swf(out_path):
    # RECT: 0 to 1024x768 (standard DAO Scaleform stage)
    rect = create_rect(0, 1024, 0, 768)
    frame_rate = struct.pack("<H", 30 << 8) # 30 fps
    frame_count = struct.pack("<H", 1)

    # Tag 9: SetBackgroundColor (RGB = 0,0,0)
    tag_bg = struct.pack("<H", (9 << 6) | 3) + b"\x00\x00\x00"

    constants = [
        "_root",                                                                            # 0
        "DAOTransmog_mc",                                                                   # 1
        "_x",                                                                               # 2
        "_y",                                                                               # 3
        "lineStyle",                                                                        # 4
        "beginFill",                                                                        # 5
        "moveTo",                                                                           # 6
        "lineTo",                                                                           # 7
        "endFill",                                                                          # 8
        "createTextField",                                                                  # 9
        "txt",                                                                              # 10
        "selectable",                                                                       # 11
        "html",                                                                             # 12
        "htmlText",                                                                         # 13
        '<p align="center"><font face="$TitleFont" size="12" color="#E6D0A8"><b>[Transmog]</b></font></p>', # 14
        '<p align="center"><font face="$TitleFont" size="12" color="#FFD700"><b>[Backpack]</b></font></p>', # 15
        "onRelease",                                                                        # 16
        "onRollOver",                                                                       # 17
        "onRollOut",                                                                        # 18
        "_alpha",                                                                           # 19
        "fscommand",                                                                        # 20
        "DAOTransmog_Toggle",                                                               # 21
        "",                                                                                 # 22
        "SetButtonState",                                                                   # 23
    ]

    actions = make_constant_pool(constants)

    # Helper function to push `_root.DAOTransmog_mc`:
    # push "_root" -> ActionGetVariable -> push "DAOTransmog_mc" -> ActionGetMember
    def push_target_mc():
        return push_cpool(0) + b"\x1c" + push_cpool(1) + b"\x4e"

    # 1. _root.DAOTransmog_mc._x = 672.0;
    # Stack for SetMember: obj, member, value
    actions += push_target_mc()
    actions += push_cpool(2)      # "_x"
    actions += push_double(672.0)
    actions += b"\x4f"            # ActionSetMember

    # 2. _root.DAOTransmog_mc._y = 24.0;
    actions += push_target_mc()
    actions += push_cpool(3)      # "_y"
    actions += push_double(24.0)
    actions += b"\x4f"            # ActionSetMember

    # 3. Draw Gold Border: lineStyle(1.5, 0xC6A15B, 100);
    # Stack for CallMethod: args (alpha, rgb, width), numArgs (3), obj, methodName
    actions += push_double(100.0) # alpha
    actions += push_int(0xC6A15B)  # color: gold
    actions += push_double(1.5)    # width
    actions += push_int(3)         # numArgs
    actions += push_target_mc()
    actions += push_cpool(4)       # "lineStyle"
    actions += b"\x52"             # ActionCallMethod
    actions += b"\x17"             # ActionPop

    # 4. Fill: beginFill(0x2A1C10, 95);
    actions += push_double(95.0)   # alpha
    actions += push_int(0x2A1C10)  # color: dark leather
    actions += push_int(2)         # numArgs
    actions += push_target_mc()
    actions += push_cpool(5)       # "beginFill"
    actions += b"\x52"
    actions += b"\x17"

    # 5. Box: moveTo(0, 0)
    actions += push_double(0.0)    # y
    actions += push_double(0.0)    # x
    actions += push_int(2)
    actions += push_target_mc()
    actions += push_cpool(6)       # "moveTo"
    actions += b"\x52"
    actions += b"\x17"

    # lineTo(84, 0), lineTo(84, 22), lineTo(0, 22), lineTo(0, 0)
    for (x, y) in [(84.0, 0.0), (84.0, 22.0), (0.0, 22.0), (0.0, 0.0)]:
        actions += push_double(y)
        actions += push_double(x)
        actions += push_int(2)
        actions += push_target_mc()
        actions += push_cpool(7)   # "lineTo"
        actions += b"\x52"
        actions += b"\x17"

    # endFill()
    actions += push_int(0)         # numArgs
    actions += push_target_mc()
    actions += push_cpool(8)       # "endFill"
    actions += b"\x52"
    actions += b"\x17"

    # 6. createTextField("txt", 1, 0, 1, 84, 20)
    actions += push_double(20.0)   # height
    actions += push_double(84.0)   # width
    actions += push_double(1.0)    # y
    actions += push_double(0.0)    # x
    actions += push_int(1)         # depth
    actions += push_cpool(10)      # "txt"
    actions += push_int(6)         # numArgs
    actions += push_target_mc()
    actions += push_cpool(9)       # "createTextField"
    actions += b"\x52"
    actions += b"\x17"

    # 7. _root.DAOTransmog_mc.txt.selectable = false;
    actions += push_target_mc()
    actions += push_cpool(10)      # "txt"
    actions += b"\x4e"             # ActionGetMember -> _root.DAOTransmog_mc.txt
    actions += push_cpool(11)      # "selectable"
    actions += push_int(0)         # false
    actions += b"\x4f"             # ActionSetMember

    # 8. _root.DAOTransmog_mc.txt.html = true;
    actions += push_target_mc()
    actions += push_cpool(10)      # "txt"
    actions += b"\x4e"             # ActionGetMember
    actions += push_cpool(12)      # "html"
    actions += push_int(1)         # true
    actions += b"\x4f"             # ActionSetMember

    # 9. _root.DAOTransmog_mc.txt.htmlText = '[Transmog]'
    actions += push_target_mc()
    actions += push_cpool(10)      # "txt"
    actions += b"\x4e"             # ActionGetMember
    actions += push_cpool(13)      # "htmlText"
    actions += push_cpool(14)      # '[Transmog]'
    actions += b"\x4f"             # ActionSetMember

    # 10. onRelease = function() { fscommand("DAOTransmog_Toggle", ""); }
    fn_release_body = push_cpool(22)  # ""
    fn_release_body += push_cpool(21) # "DAOTransmog_Toggle"
    fn_release_body += push_int(2)    # numArgs
    fn_release_body += push_cpool(20) # "fscommand"
    fn_release_body += b"\x3d"        # ActionCallFunction
    fn_release_body += b"\x17"        # ActionPop
    fn_release_body += b"\x3e"        # ActionReturn

    fn_release_tag = b"\x00" + struct.pack("<H", 0) + struct.pack("<H", len(fn_release_body)) + fn_release_body
    fn_release_header = b"\x9b" + struct.pack("<H", len(fn_release_tag)) + fn_release_tag

    actions += push_target_mc()
    actions += push_cpool(16)         # "onRelease"
    actions += fn_release_header      # pushes function
    actions += b"\x4f"                # ActionSetMember

    # 11. onRollOver = function() { this._alpha = 75; }
    fn_rollover_body = push_target_mc()
    fn_rollover_body += push_cpool(19) # "_alpha"
    fn_rollover_body += push_double(75.0)
    fn_rollover_body += b"\x4f"
    fn_rollover_body += b"\x3e"

    fn_rollover_tag = b"\x00" + struct.pack("<H", 0) + struct.pack("<H", len(fn_rollover_body)) + fn_rollover_body
    fn_rollover_header = b"\x9b" + struct.pack("<H", len(fn_rollover_tag)) + fn_rollover_tag

    actions += push_target_mc()
    actions += push_cpool(17)         # "onRollOver"
    actions += fn_rollover_header
    actions += b"\x4f"

    # 12. onRollOut = function() { this._alpha = 100; }
    fn_rollout_body = push_target_mc()
    fn_rollout_body += push_cpool(19) # "_alpha"
    fn_rollout_body += push_double(100.0)
    fn_rollout_body += b"\x4f"
    fn_rollout_body += b"\x3e"

    fn_rollout_tag = b"\x00" + struct.pack("<H", 0) + struct.pack("<H", len(fn_rollout_body)) + fn_rollout_body
    fn_rollout_header = b"\x9b" + struct.pack("<H", len(fn_rollout_tag)) + fn_rollout_tag

    actions += push_target_mc()
    actions += push_cpool(18)         # "onRollOut"
    actions += fn_rollout_header
    actions += b"\x4f"

    # 13. SetButtonState = function(active) { ... }
    # Param 0: active
    fn_state_body = push_target_mc()
    fn_state_body += push_cpool(10)   # "txt"
    fn_state_body += b"\x4e"          # txt
    fn_state_body += push_cpool(13)   # "htmlText"
    # Read param active (register 0 or variable "active")
    fn_state_body += push_str("active")
    fn_state_body += b"\x1c"          # ActionGetVariable (active)
    # ActionIf (0x9D)
    # If active is true, branch to set Backpack text
    branch_active = push_cpool(15)    # '[Backpack]'
    branch_inactive = push_cpool(14)  # '[Transmog]'
    # ActionIf skips branch_inactive if active is true
    # Stack before ActionIf: condition (active). If true, jumps offset.
    offset = len(branch_inactive) + 5 # 5 bytes for ActionJump (0x99)
    fn_state_body += b"\x9d" + struct.pack("<h", offset) # ActionIf
    fn_state_body += branch_inactive
    fn_state_body += b"\x99" + struct.pack("<h", len(branch_active)) # ActionJump
    fn_state_body += branch_active
    fn_state_body += b"\x4f"          # ActionSetMember (txt.htmlText = ...)
    fn_state_body += b"\x3e"          # ActionReturn

    fn_state_tag = b"\x00" + struct.pack("<H", 1) + b"active\x00" + struct.pack("<H", len(fn_state_body)) + fn_state_body
    fn_state_header = b"\x9b" + struct.pack("<H", len(fn_state_tag)) + fn_state_tag

    actions += push_target_mc()
    actions += push_cpool(23)         # "SetButtonState"
    actions += fn_state_header
    actions += b"\x4f"

    actions += b"\x00"                # ActionEnd

    # Tag 12: DoAction
    tag_do_action = b"\x3f\x03" + struct.pack("<I", len(actions)) + actions

    # Tag 1: ShowFrame
    tag_show_frame = b"\x40\x00"

    # Tag 0: End
    tag_end = b"\x00\x00"

    body = tag_bg + tag_do_action + tag_show_frame + tag_end
    file_len = 8 + len(rect) + 2 + 2 + len(body)

    header = b"FWS" + struct.pack("<B", 8) + struct.pack("<I", file_len) + rect + frame_rate + frame_count

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(header + body)

    print(f"Generated {out_path} ({file_len} bytes)")

if __name__ == "__main__":
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else r"d:\Project\DAO_Transmog\assets\daotransmog_ui.swf"
    build_swf(out)
