"""
End-to-end verification script for Multi-Character party transmog and Dog creature slots.
"""

import os
import sys
import time
import json

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from client import DaoBridgeClient
from run_matrix_standalone import launch_game_if_needed, terminate_game

OUT_DIR = os.path.abspath(r"D:\Project\DAO_Transmog\build\test_screenshots\party_tests")
os.makedirs(OUT_DIR, exist_ok=True)


def main():
    print("=" * 70)
    print("Multi-Character & Dog Slot Verification (E2E Automated)")
    print("=" * 70)

    launch_game_if_needed()
    client = DaoBridgeClient()

    results = {}

    try:
        # 1. Connect to bridge
        start = time.time()
        connected = False
        while time.time() - start < 120.0:
            if client.connect(timeout_sec=1.0):
                connected = True
                break
            time.sleep(1.0)

        assert connected, "Failed to connect to MCP pipe bridge"
        print("[+] Connected to DAO MCP Bridge!")

        # Wait for pregame / main menu
        while time.time() - start < 120.0:
            p = client.ping().get("result", {})
            if p.get("is_pregame") or p.get("is_in_world"):
                break
            time.sleep(1.0)

        time.sleep(2.0)

        # 2. Enumerate saves
        saves = client.send_request("list_saves").get("result", {}).get("saves", [])
        party_idx = None
        dog_idx = None
        for s in saves:
            if s.get("kind_name") == "manual":
                if s.get("number") == 1:
                    party_idx = s.get("index")
                elif s.get("number") == 2:
                    dog_idx = s.get("index")

        print(f"[+] Save Indices: Party Test = {party_idx}, Dog Test = {dog_idx}")
        assert party_idx is not None, "Party Test save (Slot 1) not found"
        assert dog_idx is not None, "Dog Test save (Slot 2) not found"

        # =====================================================================
        # PHASE 1: Party Test Save (Hero & Companion)
        # =====================================================================
        print("\n--- PHASE 1: Party Test Save ---")
        client.send_request("load_save", {"index": party_idx})
        load_start = time.time()
        while time.time() - load_start < 60.0:
            if client.ping().get("result", {}).get("is_in_world"):
                break
            time.sleep(1.0)
        time.sleep(2.5)

        # PTY-01: Verify multi-character party
        party_info = client.send_request("get_party").get("result", {})
        members = party_info.get("members", [])
        print(f"[+] Party members count: {len(members)}")
        assert len(members) >= 2, f"Expected at least 2 members in Party Test, got {len(members)}"
        hero_id = members[0]["id"]
        comp_id = members[1]["id"]
        print(f"[+] Hero ID: {hero_id}, Companion ID: {comp_id}")
        results["PTY-01"] = True

        # Open Inventory on Hero
        client.open_screen("Inventory")
        time.sleep(1.5)

        # Open Transmog Browser
        client.send_request("trigger_action", {"command": "DAOTransmog_Toggle"})
        time.sleep(1.0)

        # Verify Hero slot tabs (all 6 humanoid slots visible)
        hero_tabs = client.send_request("inspect_ui_batch", {
            "paths": [
                "_root.DAOTransmog_Browser.slot0._visible",
                "_root.DAOTransmog_Browser.slot0.lbl.TextField.text",
                "_root.DAOTransmog_Browser.slot1._visible",
                "_root.DAOTransmog_Browser.slot1.lbl.TextField.text",
                "_root.DAOTransmog_Browser.slot2._visible",
                "_root.DAOTransmog_Browser.slot5._visible",
                "_root.DAOTransmog_Browser.syncBtn._visible",
                "_root.DAOTransmog_Browser.hdr.lbl.TextField.text"
            ]
        }).get("result", {}).get("values", {})
        print(f"[+] Hero UI Tabs: {json.dumps(hero_tabs, indent=2)}")

        assert hero_tabs.get("_root.DAOTransmog_Browser.slot0.lbl.TextField.text") == "Chest"
        assert hero_tabs.get("_root.DAOTransmog_Browser.slot1.lbl.TextField.text") == "Gloves"
        assert hero_tabs.get("_root.DAOTransmog_Browser.slot2._visible") is True
        assert hero_tabs.get("_root.DAOTransmog_Browser.slot5._visible") is True

        # Capture Hero Transmog UI
        shot_hero = os.path.join(OUT_DIR, "01_party_hero_transmog.png")
        client.take_screenshot(shot_hero)
        print(f"[+] Captured Hero Transmog: {shot_hero}")

        # PTY-02 & PTY-03: Cycle to Companion
        print("\n[*] Cycling to Companion...")
        nxt_res = client.send_request("next_character").get("result", {})
        print(f"[+] Next character result: {nxt_res}")
        time.sleep(1.5)

        comp_screen_id = nxt_res.get("screen_character")
        assert comp_screen_id == comp_id, f"Expected ScreenCharacter {comp_id}, got {comp_screen_id}"
        results["PTY-02"] = True

        # Capture Companion Transmog UI
        shot_comp = os.path.join(OUT_DIR, "02_party_companion_transmog.png")
        client.take_screenshot(shot_comp)
        print(f"[+] Captured Companion Transmog: {shot_comp}")
        results["PTY-03"] = True

        client.close_screen("Inventory")
        time.sleep(1.5)

        # =====================================================================
        # PHASE 2: Dog Test Save (Hero & Mabari War Hound)
        # =====================================================================
        print("\n--- PHASE 2: Dog Test Save ---")
        client.send_request("load_save", {"index": dog_idx})
        load_start = time.time()
        while time.time() - load_start < 60.0:
            if client.ping().get("result", {}).get("is_in_world"):
                break
            time.sleep(1.0)
        time.sleep(2.5)

        # DOG-01: Detect Dog in party
        dog_party = client.send_request("get_party").get("result", {})
        dog_members = dog_party.get("members", [])
        dog_member = None
        for m in dog_members:
            if m.get("appearance_type") == 35 or m.get("racial_type") == 5:
                dog_member = m
                break

        assert dog_member is not None, "Dog member (AppType 35 / Racial 5) not found in Dog Test save"
        dog_id = dog_member["id"]
        print(f"[+] Dog detected: ID {dog_id}, AppType {dog_member.get('appearance_type')}, Racial {dog_member.get('racial_type')}")
        results["DOG-01"] = True

        # Open Inventory
        client.open_screen("Inventory")
        time.sleep(1.5)

        # Cycle to Dog
        print("[*] Stepping to Dog character...")
        step_res = client.send_request("next_character").get("result", {})
        time.sleep(1.5)
        print(f"[+] Screen character is now: {step_res.get('screen_character')}")
        assert step_res.get("screen_character") == dog_id

        # Open Transmog Browser on Dog
        client.send_request("trigger_action", {"command": "DAOTransmog_Toggle"})
        time.sleep(1.0)

        # DOG-02 & DOG-03: Inspect Dog Transmog UI
        dog_ui = client.send_request("inspect_ui_batch", {
            "paths": [
                "_root.DAOTransmog_Browser.slot0._visible",
                "_root.DAOTransmog_Browser.slot0.lbl.TextField.text",
                "_root.DAOTransmog_Browser.slot1._visible",
                "_root.DAOTransmog_Browser.slot1.lbl.TextField.text",
                "_root.DAOTransmog_Browser.slot2._visible",
                "_root.DAOTransmog_Browser.slot3._visible",
                "_root.DAOTransmog_Browser.slot4._visible",
                "_root.DAOTransmog_Browser.slot5._visible",
                "_root.DAOTransmog_Browser.syncBtn._visible",
                "_root.DAOTransmog_Browser.zoomBtn._visible",
                "_root.DAOTransmog_Browser.hdr.lbl.TextField.text"
            ]
        }).get("result", {}).get("values", {})
        print(f"[+] Dog UI State:\n{json.dumps(dog_ui, indent=2)}")

        # Verify Dog slot tabs
        assert dog_ui.get("_root.DAOTransmog_Browser.slot0._visible") is True
        assert dog_ui.get("_root.DAOTransmog_Browser.slot0.lbl.TextField.text") == "Collar"
        assert dog_ui.get("_root.DAOTransmog_Browser.slot1._visible") is True
        assert dog_ui.get("_root.DAOTransmog_Browser.slot1.lbl.TextField.text") == "Warpaint"
        results["DOG-02"] = True

        # Verify humanoid slots are completely hidden on Dog
        assert dog_ui.get("_root.DAOTransmog_Browser.slot2._visible") is False, "Boots tab should be hidden on Dog"
        assert dog_ui.get("_root.DAOTransmog_Browser.slot3._visible") is False, "Main tab should be hidden on Dog"
        assert dog_ui.get("_root.DAOTransmog_Browser.slot4._visible") is False, "Offhand tab should be hidden on Dog"
        assert dog_ui.get("_root.DAOTransmog_Browser.slot5._visible") is False, "Helmet tab should be hidden on Dog"
        assert dog_ui.get("_root.DAOTransmog_Browser.syncBtn._visible") is False, "Sync button should be hidden on Dog"
        assert dog_ui.get("_root.DAOTransmog_Browser.zoomBtn._visible") is False, "Zoom button should be hidden on Dog"
        results["DOG-03"] = True

        # Capture Dog Collar Transmog
        shot_dog_collar = os.path.join(OUT_DIR, "03_dog_transmog_collar.png")
        client.take_screenshot(shot_dog_collar)
        print(f"[+] Captured Dog Collar Transmog: {shot_dog_collar}")

        # Select Warpaint (slot 1)
        client.send_request("trigger_action", {"command": "DAOTransmog_SelectSlot", "arg": "1"})
        time.sleep(0.5)
        warpaint_hdr = client.send_request("inspect_ui", {"path": "_root.DAOTransmog_Browser.hdr.lbl.TextField.text"}).get("result", {}).get("value")
        print(f"[+] Dog Warpaint Header: {warpaint_hdr}")
        assert "Dog Warpaint" in warpaint_hdr

        shot_dog_warpaint = os.path.join(OUT_DIR, "04_dog_transmog_warpaint.png")
        client.take_screenshot(shot_dog_warpaint)
        print(f"[+] Captured Dog Warpaint Transmog: {shot_dog_warpaint}")

        # Attempt to select humanoid slot 2 (Boots) on Dog -> must be rejected
        blocked_res = client.send_request("trigger_action", {"command": "DAOTransmog_SelectSlot", "arg": "2"}).get("result", {})
        print(f"[+] Selecting Boots on Dog result (should be rejected): {blocked_res}")
        assert blocked_res.get("executed") is False

        # DOG-04: Cycle from Dog back to Hero -> UI must dynamically restore all humanoid slots
        print("\n[*] Stepping from Dog back to Hero...")
        hero_step = client.send_request("next_character").get("result", {})
        time.sleep(1.5)
        print(f"[+] Screen character back to Hero: {hero_step.get('screen_character')}")

        hero_restored_ui = client.send_request("inspect_ui_batch", {
            "paths": [
                "_root.DAOTransmog_Browser.slot0.lbl.TextField.text",
                "_root.DAOTransmog_Browser.slot1.lbl.TextField.text",
                "_root.DAOTransmog_Browser.slot2._visible",
                "_root.DAOTransmog_Browser.slot5._visible",
                "_root.DAOTransmog_Browser.syncBtn._visible",
                "_root.DAOTransmog_Browser.hdr.lbl.TextField.text"
            ]
        }).get("result", {}).get("values", {})
        print(f"[+] Restored Hero UI:\n{json.dumps(hero_restored_ui, indent=2)}")

        assert hero_restored_ui.get("_root.DAOTransmog_Browser.slot0.lbl.TextField.text") == "Chest"
        assert hero_restored_ui.get("_root.DAOTransmog_Browser.slot2._visible") is True
        assert hero_restored_ui.get("_root.DAOTransmog_Browser.slot5._visible") is True
        results["DOG-04"] = True

        shot_hero_restored = os.path.join(OUT_DIR, "05_hero_restored_from_dog.png")
        client.take_screenshot(shot_hero_restored)
        print(f"[+] Captured Hero restored from Dog: {shot_hero_restored}")

        client.close_screen("Inventory")

        print("\n" + "=" * 70)
        print("ALL MULTI-CHARACTER & DOG TEST CASES PASSED!")
        print(json.dumps(results, indent=2))
        print("=" * 70)
        return 0

    except Exception as e:
        print(f"\n[-] TEST FAILED WITH EXCEPTION: {e}")
        import traceback
        traceback.print_exc()
        return 1

    finally:
        print("[*] Teardown: Terminating game process...")
        terminate_game()


if __name__ == "__main__":
    sys.exit(main())
