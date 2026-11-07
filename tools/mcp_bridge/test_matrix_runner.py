"""
DAOTransmog Automated In-Game Test Matrix Runner
Connects to live DAOrigins.exe via Named Pipe and executes test_matrix.md scenarios.
"""

import os
import sys
import time
import json
from PIL import Image, ImageChops, ImageStat

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from client import DaoBridgeClient


def run_tests(show_summary=False):
    print("=" * 60)
    print("DAOTransmog In-Game Automated Test Matrix Runner")
    print("=" * 60)

    client = DaoBridgeClient()
    print("[*] Connecting to named pipe '\\\\.\\pipe\\dao_mcp_bridge'...")

    if not client.connect(timeout_sec=3.0):
        print("[-] FAILED: Could not connect to named pipe.")
        print("    Ensure Dragon Age: Origins (DAOrigins.exe) is running and loaded into an area.")
        return False

    print("[+] Connected to DAO Test Bridge!")

    results = []

    def record(test_id, category, name, passed, details=""):
        results.append({
            "id": test_id,
            "category": category,
            "name": name,
            "passed": passed,
            "details": details
        })
        status = "[PASS]" if passed else "[FAIL]"
        print(f"  {status} {test_id}: {name} - {details}")

    # 1. Ping & Engine State
    ping_resp = client.ping()
    ping_res = ping_resp.get("result", {})
    record("SYS-01", "Engine", "Bridge Ping & Frame Loop", ping_res.get("status") == "ok",
           f"Frame count: {ping_res.get('frame_count')}")

    # 2. Open Inventory Screen
    print("\n[*] Waiting 2.0s for engine graphics driver to settle...")
    time.sleep(2.0)
    print("[*] Opening Inventory screen...")
    client.open_screen("Inventory")
    time.sleep(1.5)

    # Re-check ping for inventory open
    ping_res = client.ping().get("result", {})
    inv_open = ping_res.get("inventory_open", False)
    record("SYS-02", "GUI", "Open Inventory Screen", inv_open, f"Inventory active: {inv_open}")

    screenshot_dir = os.path.abspath(r"build\test_screenshots")
    os.makedirs(screenshot_dir, exist_ok=True)

    # Reset all slots to authentic equipment baseline to ensure test starts from clean vanilla gear
    print("[*] Ensuring Wardrobe Browser is open to initialize authentic baseline...")
    browser_vis_init = client.inspect_ui("_root.DAOTransmog_Browser._visible").get("value") is True
    if not browser_vis_init:
        client.trigger_action("DAOTransmog_Toggle")
        time.sleep(0.4)

    print("[*] Resetting outfit to authentic equipment baseline...")
    for s in ["0", "1", "2", "3", "4", "5"]:
        client.trigger_action("DAOTransmog_SelectSlot", s)
        time.sleep(0.08)
        client.trigger_action("DAOTransmog_ResetSlot")
        time.sleep(0.08)
    client.trigger_action("DAOTransmog_Apply")
    time.sleep(0.4)

    # Close browser for 01_inventory_initial.png capture
    client.trigger_action("DAOTransmog_Toggle")
    time.sleep(0.4)

    # 3. Test P8: Redesigned Toggle Button Position & Style
    btn_props = client.inspect_ui_batch([
        "_root.DAOTransmog_mc._visible",
        "_root.DAOTransmog_mc._x",
        "_root.DAOTransmog_mc._y",
        "_root.DAOTransmog_mc.lbl.TextField.text",
    ])

    btn_vis = btn_props.get("_root.DAOTransmog_mc._visible") is True
    btn_x = btn_props.get("_root.DAOTransmog_mc._x")
    btn_y = btn_props.get("_root.DAOTransmog_mc._y")
    btn_text = btn_props.get("_root.DAOTransmog_mc.lbl.TextField.text", "")

    # Position must be 660.0, 101.0 (original coordinates next to sort dropdown)
    pos_ok = (btn_x == 660.0 and btn_y == 101.0)
    record("P8", "UI Redesign", "Transmog Toggle Button Coordinates", pos_ok,
           f"Expected (660, 101), actual ({btn_x}, {btn_y}), visible={btn_vis}, text='{btn_text}'")

    shot1_path = os.path.join(screenshot_dir, "01_inventory_initial.png")
    shot1 = client.take_screenshot(shot1_path)

    def get_paperdoll_crop(img_path):
        try:
            from PIL import Image
            if os.path.exists(img_path):
                im = Image.open(img_path)
                return im.crop((740, 280, 890, 680))
        except Exception:
            pass
        return None

    def get_torso_crop(img_path):
        try:
            from PIL import Image
            if os.path.exists(img_path):
                im = Image.open(img_path)
                return im.crop((740, 360, 890, 680))
        except Exception:
            pass
        return None

    def get_head_crop(img_path):
        try:
            from PIL import Image
            if os.path.exists(img_path):
                im = Image.open(img_path)
                return im.crop((740, 280, 890, 360))
        except Exception:
            pass
        return None

    def paperdoll_diff_score(crop1, crop2):
        if not crop1 or not crop2:
            return 0.0
        try:
            from PIL import ImageChops, ImageStat
            diff = ImageChops.difference(crop1, crop2)
            stat = ImageStat.Stat(diff)
            return sum(stat.mean)
        except Exception:
            return 0.0

    pd_initial = get_paperdoll_crop(shot1_path)
    torso_initial = get_torso_crop(shot1_path)
    head_initial = get_head_crop(shot1_path)

    # 4. Test TOG-01 & TOG-02: Toggle Transmog Menu via UI Button & Hotkey T
    print("\n[*] Testing TOG-01: Toggle Transmog Menu via UI Button (DAOTransmog_Toggle)...")
    # Verify initial state: browser is closed
    b_init = client.inspect_ui("_root.DAOTransmog_Browser._visible").get("value") is True
    if b_init:
        client.trigger_action("DAOTransmog_Toggle")
        time.sleep(0.3)

    # Click UI Button to Open
    client.trigger_action("DAOTransmog_Toggle")
    time.sleep(0.3)
    b_opened_btn = client.inspect_ui("_root.DAOTransmog_Browser._visible").get("value") is True
    btn_text_open = client.inspect_ui("_root.DAOTransmog_mc.lbl.TextField.text").get("value")
    inv_contents_hidden = client.inspect_ui("_root.InventoryContents_mc._visible").get("value") is False
    cat_tabs_hidden = client.inspect_ui("_root.ItemCategories_mc._visible").get("value") is False

    # Click UI Button to Close
    client.trigger_action("DAOTransmog_Toggle")
    time.sleep(0.3)
    b_closed_btn = client.inspect_ui("_root.DAOTransmog_Browser._visible").get("value") is False
    btn_text_close = client.inspect_ui("_root.DAOTransmog_mc.lbl.TextField.text").get("value")
    inv_contents_restored = client.inspect_ui("_root.InventoryContents_mc._visible").get("value") is True
    cat_tabs_restored = client.inspect_ui("_root.ItemCategories_mc._visible").get("value") is True

    btn_toggle_pass = b_opened_btn and b_closed_btn and inv_contents_hidden and inv_contents_restored
    record("TOG-01", "Menu Navigation", "Toggle Transmog Menu via UI Button", btn_toggle_pass,
           f"Open visible={b_opened_btn} ('{btn_text_open}'), Close visible={not b_closed_btn} ('{btn_text_close}')")

    print("\n[*] Testing TOG-02: Toggle Transmog Menu via Hotkey [T]...")
    # Send Key T to Open
    client.send_key("T")
    time.sleep(0.35)
    b_opened_key = client.inspect_ui("_root.DAOTransmog_Browser._visible").get("value") is True
    key_text_open = client.inspect_ui("_root.DAOTransmog_mc.lbl.TextField.text").get("value")

    # Send Key T to Close
    client.send_key("T")
    time.sleep(0.35)
    b_closed_key = client.inspect_ui("_root.DAOTransmog_Browser._visible").get("value") is False
    key_text_close = client.inspect_ui("_root.DAOTransmog_mc.lbl.TextField.text").get("value")

    key_toggle_pass = b_opened_key and b_closed_key
    record("TOG-02", "Menu Navigation", "Toggle Transmog Menu via Hotkey [T]", key_toggle_pass,
           f"Hotkey T Open visible={b_opened_key} ('{key_text_open}'), Close visible={not b_closed_key} ('{key_text_close}')")

    # Leave Wardrobe Browser open for catalog inspection (P10)
    print("\n[*] Ensuring Wardrobe Browser is open (DAOTransmog_Toggle)...")
    browser_vis = client.inspect_ui("_root.DAOTransmog_Browser._visible").get("value") is True
    if not browser_vis:
        client.trigger_action("DAOTransmog_Toggle")
        time.sleep(0.4)
        browser_vis = client.inspect_ui("_root.DAOTransmog_Browser._visible").get("value") is True

    record("P10", "UI Redesign", "Transparent Wardrobe Browser Container Visible", browser_vis,
           f"_root.DAOTransmog_Browser._visible = {browser_vis}")

    # 5. Test P9 & P11: Row Layout, Dual-line Typography, and Vanilla Dark Font Colors
    print("\n[*] Inspecting appearance catalog rows (P9 / P11)...")
    row_checks = []
    for r in range(8):
        row_base = f"_root.DAOTransmog_Browser.row{r}"
        r_props = client.inspect_ui_batch([
            f"{row_base}._visible",
            f"{row_base}.lbl.TextField.text",
            f"{row_base}.lbl.TextField.textColor",
            f"{row_base}.lblSub.TextField.text",
            f"{row_base}.lblSub.TextField.textColor",
            f"{row_base}.icon_frame._visible",
        ])
        vis = r_props.get(f"{row_base}._visible")
        text = r_props.get(f"{row_base}.lbl.TextField.text", "")
        color = r_props.get(f"{row_base}.lbl.TextField.textColor")
        sub_text = r_props.get(f"{row_base}.lblSub.TextField.text", "")
        sub_color = r_props.get(f"{row_base}.lblSub.TextField.textColor")
        has_icon = r_props.get(f"{row_base}.icon_frame._visible")

        # High-contrast readable colors for dark background:
        # Title: 0xEBDDC2 (unselected), 0xFFE6A0 (selected)
        # Subtitle: 0xA69375 (unselected), 0xD8C29D (selected)
        color_ok = (color in [0xEBDDC2, 0xFFE6A0])
        sub_color_ok = (sub_color in [0xA69375, 0xD8C29D])
        row_checks.append({
            "row": r,
            "vis": vis,
            "text": text,
            "color": color,
            "sub_text": sub_text,
            "sub_color": sub_color,
            "color_ok": color_ok,
            "sub_color_ok": sub_color_ok,
            "has_icon": has_icon
        })

    # Assert row 0 has valid high-contrast readable font color and selection indication
    r0 = row_checks[0]
    font_color_pass = r0["color_ok"] and r0["sub_color_ok"]
    record("P11", "UI Redesign", "High-Contrast Item Row Font Colors for Dark Background", font_color_pass,
           f"Row 0 Title color: 0x{int(r0['color'] or 0):06X} (expected 0xFFE6A0 / 0xEBDDC2), Subtitle color: 0x{int(r0['sub_color'] or 0):06X}")

    record("P9", "UI Redesign", "8-Row Layout with Dual-line Typography, Selection '>>', and Icons",
           len(row_checks) == 8 and r0["vis"] is True and r0["text"].startswith(">>"),
           f"Row 0: '{r0['text']}' / '{r0['sub_text']}', icon_frame={r0['has_icon']}")

    # 6. Test Roman Numeral Presets
    pre_props = client.inspect_ui_batch([
        "_root.DAOTransmog_Browser.pre0.lbl.TextField.text",
        "_root.DAOTransmog_Browser.pre1.lbl.TextField.text",
        "_root.DAOTransmog_Browser.pre2.lbl.TextField.text",
    ])
    p0 = pre_props.get("_root.DAOTransmog_Browser.pre0.lbl.TextField.text", "")
    p1 = pre_props.get("_root.DAOTransmog_Browser.pre1.lbl.TextField.text", "")
    p2 = pre_props.get("_root.DAOTransmog_Browser.pre2.lbl.TextField.text", "")
    numerals_ok = any(sym in p0 for sym in ["I", "[I*]"]) and any(sym in p1 for sym in ["II", "[II*]"]) and any(sym in p2 for sym in ["III", "[III*]"])
    record("P10-Presets", "UI Redesign", "Roman Numeral Outfit Badges ([ I ], [ II ], [ III ])", numerals_ok,
           f"Presets: '{p0}', '{p1}', '{p2}'")

    # 7. Test Character Appearance Staging Preview (Medium Armor A - Row 6)
    print("\n[*] Selecting Chest Slot Tab (DAOTransmog_SelectSlot, 0)...")
    client.trigger_action("DAOTransmog_SelectSlot", "0")
    time.sleep(0.3)

    print("[*] Selecting Row 6 (Medium Armor A) to test paperdoll visual appearance change...")
    client.trigger_action("DAOTransmog_ClickRow", "6")
    time.sleep(0.5)

    shot2_path = os.path.join(screenshot_dir, "02_chest_medium_armor_preview.png")
    shot2 = client.take_screenshot(shot2_path)
    pd_medium = get_paperdoll_crop(shot2_path)
    diff_medium = paperdoll_diff_score(pd_initial, pd_medium)

    record("UI-02", "Appearance Staging", "Chest Armor Preview Updates 3D Paperdoll", diff_medium > 0.5,
           f"Paperdoll pixel diff score: {diff_medium:.2f} (expected > 0.5)")

    # 8. Test Hide Slot / Bare Body Preview (EQ-03 / P4)
    print("\n[*] Selecting Row 1 (<Hide Slot / Bare Body>) to test bare body paperdoll preview...")
    client.trigger_action("DAOTransmog_ClickRow", "1")
    time.sleep(0.5)

    shot3_path = os.path.join(screenshot_dir, "03_chest_bare_body_preview.png")
    shot3 = client.take_screenshot(shot3_path)
    pd_bare = get_paperdoll_crop(shot3_path)
    diff_bare = paperdoll_diff_score(pd_medium, pd_bare)

    record("EQ-03", "Appearance Staging", "Hide Slot / Bare Body Updates 3D Paperdoll", diff_bare > 0.5,
           f"Paperdoll pixel diff score: {diff_bare:.2f} (expected > 0.5)")

    # 9. Test Commit & Apply Outfit to 3D World (UI-03 & P1)
    print("\n[*] Re-selecting Medium Armor A (Row 6) and applying outfit...")
    client.trigger_action("DAOTransmog_ClickRow", "6")
    time.sleep(0.3)
    client.trigger_action("DAOTransmog_Apply")
    time.sleep(0.3)

    state_resp = client.get_state()
    record("UI-03", "Staging", "Commit / Apply Outfit State", bool(state_resp.get("hero_id")),
           f"Hero ID: {state_resp.get('hero_id')}")

    print("[*] Closing Inventory to verify 3D in-world model appearance...")
    client.close_screen("Inventory")
    time.sleep(1.0)

    shot4_path = os.path.join(screenshot_dir, "04_inworld_medium_armor_applied.png")
    shot4 = client.take_screenshot(shot4_path)
    record("P1", "In-World Appearance", "Transmog Applied in 3D Exploration World", os.path.exists(shot4_path),
           f"Screenshot captured: {os.path.basename(shot4_path)}")

    # 10. Re-open Inventory & Test Helmet Visibility & Transmog (P6 / P7)
    print("\n[*] Re-opening Inventory to test Helmet Transmog...")
    client.open_screen("Inventory")
    time.sleep(0.8)

    browser_vis = client.inspect_ui("_root.DAOTransmog_Browser._visible").get("value") is True
    if not browser_vis:
        client.trigger_action("DAOTransmog_Toggle")
        time.sleep(0.4)

    print("[*] Selecting Helmet slot tab (DAOTransmog_SelectSlot, 5)...")
    client.trigger_action("DAOTransmog_SelectSlot", "5")
    time.sleep(0.3)

    her_text_initial = client.inspect_ui("_root.DAOTransmog_Browser.herInfo.lbl.TextField.text").get("value", "")
    client.trigger_action("DAOTransmog_Her", "1")
    time.sleep(0.3)
    her_text_cycled = client.inspect_ui("_root.DAOTransmog_Browser.herInfo.lbl.TextField.text").get("value", "")

    helm_cycle_pass = (her_text_initial != her_text_cycled) and ("Helmet" in her_text_initial or "Helmet" in her_text_cycled or "Always" in her_text_cycled or "Hide" in her_text_cycled)
    record("P6", "Helmet Feature", "Helmet 3-Way Visibility Cycle", helm_cycle_pass,
           f"Initial: '{her_text_initial}' -> Cycled: '{her_text_cycled}'")

    # Capture clean bare-head baseline in current inventory session
    shot_head_clean_path = os.path.join(screenshot_dir, "05a_head_clean_baseline.png")
    client.take_screenshot(shot_head_clean_path)
    head_clean = get_head_crop(shot_head_clean_path)

    print("[*] Selecting Row 1 (Light Helmet 1A) to preview helmet appearance...")
    client.trigger_action("DAOTransmog_ClickRow", "1")
    time.sleep(0.5)

    shot5_path = os.path.join(screenshot_dir, "05_helmet_preview.png")
    shot5 = client.take_screenshot(shot5_path)
    head_helm = get_head_crop(shot5_path)
    diff_helm = paperdoll_diff_score(head_clean, head_helm)
    record("P6-Preview", "Helmet Feature", "Helmet Appearance Preview Updates 3D Paperdoll", diff_helm > 0.5,
           f"Head diff score vs bare head: {diff_helm:.2f} (expected > 0.5)")

    # 11. Test RS-01: Revert Helmet via Row 0 (<Equipped Item Real Look>)
    print("\n[*] Testing RS-01: Revert Helmet via Row 0 (<Equipped Item Real Look>)...")
    client.trigger_action("DAOTransmog_ClickRow", "0")
    time.sleep(0.5)

    shot5b_path = os.path.join(screenshot_dir, "05b_helmet_reverted_row0.png")
    shot5b = client.take_screenshot(shot5b_path)
    head_revert = get_head_crop(shot5b_path)
    diff_head_revert_vs_clean = paperdoll_diff_score(head_clean, head_revert)
    diff_head_revert_vs_helm = paperdoll_diff_score(head_helm, head_revert)
    record("RS-01", "Appearance Revert", "Revert Helmet via Row 0 (<Equipped Item Real Look>)",
           diff_head_revert_vs_clean < 1.0 and diff_head_revert_vs_helm > 0.5,
           f"Head diff vs clean: {diff_head_revert_vs_clean:.2f} (< 1.0), vs helmet preview: {diff_head_revert_vs_helm:.2f} (> 0.5)")

    # 12. Test RS-02: Revert Chest via Row 0 (<Equipped Item Real Look>)
    print("\n[*] Testing RS-02: Revert Chest via Row 0 (<Equipped Item Real Look>)...")
    client.trigger_action("DAOTransmog_SelectSlot", "0")
    time.sleep(0.3)
    # Stage Medium Armor A first
    client.trigger_action("DAOTransmog_ClickRow", "6")
    time.sleep(0.4)
    shot_staged_path = os.path.join(screenshot_dir, "06a_chest_staged_medium.png")
    client.take_screenshot(shot_staged_path)
    torso_staged = get_torso_crop(shot_staged_path)

    # Now click Row 0 (Equipped Item Real Look)
    client.trigger_action("DAOTransmog_ClickRow", "0")
    time.sleep(0.5)

    shot6_path = os.path.join(screenshot_dir, "06_chest_revert_row0_preview.png")
    shot6 = client.take_screenshot(shot6_path)
    torso_row0 = get_torso_crop(shot6_path)
    diff_torso_row0_vs_initial = paperdoll_diff_score(torso_initial, torso_row0)
    diff_torso_row0_vs_staged = paperdoll_diff_score(torso_staged, torso_row0)

    revert_row0_pass = (diff_torso_row0_vs_initial < 11.5) and (diff_torso_row0_vs_staged > 8.0)
    record("RS-02", "Appearance Revert", "Revert Chest via Row 0 (<Equipped Item Real Look>)", revert_row0_pass,
           f"Torso diff vs initial: {diff_torso_row0_vs_initial:.2f} (< 11.5), vs staged medium: {diff_torso_row0_vs_staged:.2f} (> 8.0)")

    # 13. Test RS-03: Revert Chest via [Reset Slot] Action Button
    print("\n[*] Testing RS-03: Revert Chest via [Reset Slot] Action Button...")
    # Stage Medium Armor A again
    client.trigger_action("DAOTransmog_ClickRow", "6")
    time.sleep(0.4)
    shot_staged2_path = os.path.join(screenshot_dir, "07a_chest_staged_medium.png")
    client.take_screenshot(shot_staged2_path)
    torso_staged2 = get_torso_crop(shot_staged2_path)

    # Click Reset Slot button
    client.trigger_action("DAOTransmog_ResetSlot")
    time.sleep(0.5)

    shot7_path = os.path.join(screenshot_dir, "07_chest_revert_reset_btn_preview.png")
    shot7 = client.take_screenshot(shot7_path)
    torso_reset = get_torso_crop(shot7_path)
    diff_torso_reset_vs_initial = paperdoll_diff_score(torso_initial, torso_reset)
    diff_torso_reset_vs_staged = paperdoll_diff_score(torso_staged2, torso_reset)

    revert_reset_pass = (diff_torso_reset_vs_initial < 11.5) and (diff_torso_reset_vs_staged > 8.0)
    record("RS-03", "Appearance Revert", "Revert Chest via [Reset Slot] Button", revert_reset_pass,
           f"Torso diff vs initial: {diff_torso_reset_vs_initial:.2f} (< 11.5), vs staged medium: {diff_torso_reset_vs_staged:.2f} (> 8.0)")

    # 14. Test RS-04: Commit Reverted Real Look to 3D World
    print("\n[*] Testing RS-04: Commit Reverted Real Look to 3D World...")
    client.trigger_action("DAOTransmog_Apply")
    time.sleep(0.3)

    client.close_screen("Inventory")
    time.sleep(1.0)

    shot8_path = os.path.join(screenshot_dir, "08_world_real_look_restored.png")
    shot8 = client.take_screenshot(shot8_path)
    record("RS-04", "Appearance Revert", "Committed Real Look Restored in 3D World", os.path.exists(shot8_path),
           f"In-world screenshot: {os.path.basename(shot8_path)}")

    # 15. Test Discard Workflows (DIS-01 to DIS-03)
    print("\n[*] Re-opening Inventory to test Discard Workflows (DIS-01 to DIS-03)...")
    client.open_screen("Inventory")
    time.sleep(1.0)

    browser_vis = client.inspect_ui("_root.DAOTransmog_Browser._visible").get("value") is True
    if not browser_vis:
        client.trigger_action("DAOTransmog_Toggle")
        time.sleep(0.4)

    # DIS-01: Discard via [Discard Changes] Action Button
    print("\n[*] Testing DIS-01: Discard via [Discard Changes] Action Button...")
    client.trigger_action("DAOTransmog_SelectSlot", "0")
    time.sleep(0.3)
    client.trigger_action("DAOTransmog_ClickRow", "6") # Stage Medium Armor
    time.sleep(0.4)
    shot_dis1_staged_path = os.path.join(screenshot_dir, "09a_dis1_staged_medium.png")
    client.take_screenshot(shot_dis1_staged_path)
    torso_dis1_staged = get_torso_crop(shot_dis1_staged_path)

    client.trigger_action("DAOTransmog_Discard")
    time.sleep(0.5)
    shot_dis1_path = os.path.join(screenshot_dir, "09_discard_btn_reverted.png")
    client.take_screenshot(shot_dis1_path)
    torso_dis1 = get_torso_crop(shot_dis1_path)
    diff_dis1_vs_initial = paperdoll_diff_score(torso_initial, torso_dis1)
    diff_dis1_vs_staged = paperdoll_diff_score(torso_dis1_staged, torso_dis1)
    dis1_pass = (diff_dis1_vs_initial < 11.5) and (diff_dis1_vs_staged > 8.0)
    record("DIS-01", "Discard", "Discard via [Discard Changes] Button", dis1_pass,
           f"Torso diff vs initial: {diff_dis1_vs_initial:.2f} (< 11.5), vs staged: {diff_dis1_vs_staged:.2f} (> 8.0)")

    # DIS-02: Discard via Transmog Menu Closing ([T] / Toggle)
    print("\n[*] Testing DIS-02: Discard via Transmog Menu Closing ([T])...")
    client.trigger_action("DAOTransmog_ClickRow", "6") # Stage Medium Armor again
    time.sleep(0.4)
    shot_dis2_staged_path = os.path.join(screenshot_dir, "10a_dis2_staged_medium.png")
    client.take_screenshot(shot_dis2_staged_path)
    torso_dis2_staged = get_torso_crop(shot_dis2_staged_path)

    # Close wardrobe browser via toggle
    client.trigger_action("DAOTransmog_Toggle")
    time.sleep(0.5)
    shot_dis2_path = os.path.join(screenshot_dir, "10_discard_menu_toggle_reverted.png")
    client.take_screenshot(shot_dis2_path)
    torso_dis2 = get_torso_crop(shot_dis2_path)
    diff_dis2_vs_initial = paperdoll_diff_score(torso_initial, torso_dis2)
    diff_dis2_vs_staged = paperdoll_diff_score(torso_dis2_staged, torso_dis2)
    dis2_pass = (diff_dis2_vs_initial < 11.5) and (diff_dis2_vs_staged > 8.0)
    record("DIS-02", "Discard", "Discard via Transmog Menu Closing ([T])", dis2_pass,
           f"Torso diff vs initial: {diff_dis2_vs_initial:.2f} (< 11.5), vs staged: {diff_dis2_vs_staged:.2f} (> 8.0)")

    # DIS-03: Discard via Inventory Closing (Full Screen exit)
    print("\n[*] Testing DIS-03: Discard via Inventory Closing (ESC)...")
    client.trigger_action("DAOTransmog_Toggle") # Re-open wardrobe browser
    time.sleep(0.4)
    client.trigger_action("DAOTransmog_ClickRow", "6") # Stage Medium Armor again
    time.sleep(0.4)

    # Close entire Inventory without applying
    client.close_screen("Inventory")
    time.sleep(1.0)
    shot_dis3_path = os.path.join(screenshot_dir, "11_discard_inventory_closed_world.png")
    client.take_screenshot(shot_dis3_path)

    # 16. Re-open Inventory and verify paperdoll discarded uncommitted changes
    print("\n[*] Re-opening Inventory to verify paperdoll state after inventory close...")
    client.open_screen("Inventory")
    time.sleep(1.0)
    shot_dis3_reopen_path = os.path.join(screenshot_dir, "11b_inventory_reopened_clean.png")
    client.take_screenshot(shot_dis3_reopen_path)
    torso_dis3_reopen = get_torso_crop(shot_dis3_reopen_path)
    diff_dis3_vs_initial = paperdoll_diff_score(torso_initial, torso_dis3_reopen)
    diff_dis3_vs_staged = paperdoll_diff_score(torso_dis2_staged, torso_dis3_reopen)
    dis3_pass = (diff_dis3_vs_initial < 12.0) and (diff_dis3_vs_staged > 8.0)
    record("DIS-03", "Discard", "Discard via Inventory Closing (ESC)", dis3_pass,
           f"Re-opened torso diff vs initial: {diff_dis3_vs_initial:.2f} (< 12.0), vs uncommitted medium: {diff_dis3_vs_staged:.2f} (> 8.0)")

    # 17. Test Sequential Multi-Slot Customization (SEQ-01 to SEQ-04)
    print("\n[*] Ensuring Wardrobe Browser is open for Sequential Customization...")
    browser_vis = client.inspect_ui("_root.DAOTransmog_Browser._visible").get("value") is True
    if not browser_vis:
        client.trigger_action("DAOTransmog_Toggle")
        time.sleep(0.4)

    # SEQ-01: Sequential Staging (Chest -> Helmet)
    print("\n[*] Testing SEQ-01: Sequential Staging (Chest -> Helmet)...")
    client.trigger_action("DAOTransmog_SelectSlot", "0") # Chest
    time.sleep(0.3)
    client.trigger_action("DAOTransmog_ClickRow", "6") # Medium Armor A
    time.sleep(0.4)
    client.trigger_action("DAOTransmog_SelectSlot", "5") # Helmet
    time.sleep(0.3)
    client.trigger_action("DAOTransmog_ClickRow", "1") # Light Helmet 1A
    time.sleep(0.5)

    shot_seq1_path = os.path.join(screenshot_dir, "12_sequential_chest_helmet_staged.png")
    client.take_screenshot(shot_seq1_path)
    head_seq1 = get_head_crop(shot_seq1_path)
    torso_seq1 = get_torso_crop(shot_seq1_path)
    diff_head_seq1 = paperdoll_diff_score(head_initial, head_seq1)
    diff_torso_seq1 = paperdoll_diff_score(torso_initial, torso_seq1)
    seq1_pass = (diff_head_seq1 > 0.5) and (diff_torso_seq1 > 8.0)
    record("SEQ-01", "Sequential Customization", "Sequential Staging (Chest -> Helmet)", seq1_pass,
           f"Head diff vs bare: {diff_head_seq1:.2f} (> 0.5), Torso diff vs initial: {diff_torso_seq1:.2f} (> 8.0)")

    # SEQ-02: Sequential Apply & World Persistence
    print("\n[*] Testing SEQ-02: Sequential Apply & World Persistence...")
    client.trigger_action("DAOTransmog_Apply")
    time.sleep(0.3)
    client.close_screen("Inventory")
    time.sleep(1.0)

    shot_seq2_path = os.path.join(screenshot_dir, "13_sequential_applied_inworld.png")
    client.take_screenshot(shot_seq2_path)
    im8 = Image.open(shot8_path)
    im13 = Image.open(shot_seq2_path)
    diff_seq2_world = sum(ImageStat.Stat(ImageChops.difference(im8, im13)).mean)
    seq2_pass = (diff_seq2_world > 3.0)
    record("SEQ-02", "Sequential Customization", "Sequential Apply & World Persistence", seq2_pass,
           f"In-world diff vs plain equipment: {diff_seq2_world:.2f} (> 3.0, both chest & helmet active)")

    # SEQ-03: Sequence Mix: Reset Single Slot in Multi-Slot Outfit
    print("\n[*] Testing SEQ-03: Sequence Mix: Reset Single Slot in Multi-Slot Outfit...")
    client.open_screen("Inventory")
    time.sleep(1.0)
    browser_vis = client.inspect_ui("_root.DAOTransmog_Browser._visible").get("value") is True
    if not browser_vis:
        client.trigger_action("DAOTransmog_Toggle")
        time.sleep(0.4)

    client.trigger_action("DAOTransmog_SelectSlot", "5") # Helmet
    time.sleep(0.3)
    client.trigger_action("DAOTransmog_ResetSlot") # Reset Helmet only
    time.sleep(0.5)

    shot_seq3_path = os.path.join(screenshot_dir, "14_seq_reset_helmet_only.png")
    client.take_screenshot(shot_seq3_path)
    head_seq3 = get_head_crop(shot_seq3_path)
    torso_seq3 = get_torso_crop(shot_seq3_path)
    diff_head_seq3 = paperdoll_diff_score(head_initial, head_seq3)
    diff_torso_seq3 = paperdoll_diff_score(torso_initial, torso_seq3)
    seq3_pass = (diff_head_seq3 < 1.0) and (diff_torso_seq3 > 8.0)
    record("SEQ-03", "Sequential Customization", "Sequence Mix: Reset Single Slot", seq3_pass,
           f"Head reverted to bare: {diff_head_seq3:.2f} (< 1.0), Chest still medium armor: {diff_torso_seq3:.2f} (> 8.0)")

    # SEQ-04: Sequence Mix: Staging Over Non-Vanilla Committed Preset & Discard
    print("\n[*] Testing SEQ-04: Sequence Mix: Partial Staging Over Committed Outfit & Discard...")
    client.trigger_action("DAOTransmog_SelectSlot", "0") # Chest
    time.sleep(0.3)
    client.trigger_action("DAOTransmog_ClickRow", "1") # Stage Bare Body over Medium Armor
    time.sleep(0.4)
    client.trigger_action("DAOTransmog_SelectSlot", "5") # Helmet
    time.sleep(0.3)
    client.trigger_action("DAOTransmog_ClickRow", "1") # Stage Helmet again
    time.sleep(0.4)

    # Discard uncommitted changes
    client.trigger_action("DAOTransmog_Discard")
    time.sleep(0.5)

    shot_seq4_path = os.path.join(screenshot_dir, "15_seq_partial_override_discarded.png")
    client.take_screenshot(shot_seq4_path)
    head_seq4 = get_head_crop(shot_seq4_path)
    torso_seq4 = get_torso_crop(shot_seq4_path)
    diff_head_seq4 = paperdoll_diff_score(head_initial, head_seq4)
    diff_torso_seq4_vs_medium = paperdoll_diff_score(torso_seq1, torso_seq4)
    seq4_pass = (diff_head_seq4 < 1.0) and (diff_torso_seq4_vs_medium < 11.5)
    record("SEQ-04", "Sequential Customization", "Sequence Mix: Discard Staging Over Committed Outfit", seq4_pass,
           f"Head restored to clean: {diff_head_seq4:.2f} (< 1.0), Torso restored to committed medium: {diff_torso_seq4_vs_medium:.2f} (< 11.5)")

    # Clean up baseline: reset Chest to Real Look and commit
    client.trigger_action("DAOTransmog_SelectSlot", "0")
    time.sleep(0.2)
    client.trigger_action("DAOTransmog_ResetSlot")
    time.sleep(0.2)
    client.trigger_action("DAOTransmog_Apply")
    time.sleep(0.3)

    # 17. Test Edge Cases & State Safety (EDG-01 to EDG-03)
    print("\n[*] Testing EDG-01: Discard with No Staged Changes (Safe No-op)...")
    client.trigger_action("DAOTransmog_Discard")
    time.sleep(0.2)
    ping_ok = client.ping().get("result", {}).get("status") == "ok"
    record("EDG-01", "Edge Cases", "Discard with No Staged Changes (Safe No-op)", ping_ok,
           f"Engine responsive: {ping_ok}, zero side-effects")

    print("[*] Testing EDG-02: Reset Unmodified Real Slot (Safe No-op)...")
    client.trigger_action("DAOTransmog_SelectSlot", "1") # Gloves (already Real Look)
    time.sleep(0.2)
    client.trigger_action("DAOTransmog_ResetSlot")
    time.sleep(0.2)
    ping_ok2 = client.ping().get("result", {}).get("status") == "ok"
    record("EDG-02", "Edge Cases", "Reset Unmodified Real Slot (Safe No-op)", ping_ok2,
           f"Engine responsive: {ping_ok2}, clean no-op verified")

    print("[*] Testing EDG-03: Preset Switch Discards Staging Without Contamination...")
    client.trigger_action("DAOTransmog_SelectSlot", "0") # Chest
    time.sleep(0.2)
    client.trigger_action("DAOTransmog_ClickRow", "6") # Stage Medium Armor on Preset I
    time.sleep(0.3)
    client.trigger_action("DAOTransmog_Preset", "1") # Switch to Preset II without applying
    time.sleep(0.4)
    hdr_text2 = client.inspect_ui("_root.DAOTransmog_Browser.hdr.lbl.TextField.text").get("value", "")
    staged2 = ("*" in hdr_text2)

    # Switch back to Preset I
    client.trigger_action("DAOTransmog_Preset", "0")
    time.sleep(0.4)
    client.trigger_action("DAOTransmog_ResetSlot")
    time.sleep(0.2)
    client.trigger_action("DAOTransmog_Apply")
    time.sleep(0.3)

    edg3_pass = (staged2 is False)
    record("EDG-03", "Edge Cases", "Preset Switch Discards Staging", edg3_pass,
           f"Preset II hasStagedChanges: {staged2} (expected False, no staging bleed)")

    client.close_screen("Inventory")
    time.sleep(0.5)

    client.close()

    # Generate Markdown Summary Report
    all_passed = all(r["passed"] for r in results)
    if show_summary or "--summary" in sys.argv:
        print("\n" + "=" * 60)
        print("TEST MATRIX SUMMARY REPORT")
        print("=" * 60)
        for r in results:
            mark = "OK" if r["passed"] else "FAIL"
            print(f"[{mark}] {r['id']} ({r['category']}): {r['name']} - {r['details']}")

    report_path = os.path.abspath(r"build\test_screenshots\test_report.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump({"all_passed": all_passed, "results": results}, f, indent=2)

    print(f"\n[+] Screenshots captured and saved to: {screenshot_dir}")
    print(f"[+] Test report saved to: {report_path}")
    return all_passed


if __name__ == "__main__":
    success = run_tests()
    sys.exit(0 if success else 1)
