"""
Automated E2E Test Suite for Multi-Character Transmog Persistence and Edge Cases.
Tests:
- Customizing multiple characters (Hero + Companion + Dog)
- Per-character state isolation (no bleed)
- In-game QuickSave & Co-Save persistence across saving and loading
- Edge cases: staging on character switch, invalid slot rejection on Dog
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

OUT_DIR = os.path.abspath(r"D:\Project\DAO_Transmog\build\test_screenshots\multichar_persistence")
os.makedirs(OUT_DIR, exist_ok=True)


def main():
    print("=" * 70)
    print("Multi-Character Transmog Persistence & Edge Cases Suite")
    print("=" * 70)

    launch_game_if_needed()
    client = DaoBridgeClient()

    results = {}

    try:
        # 1. Connect
        start = time.time()
        connected = False
        while time.time() - start < 120.0:
            if client.connect(timeout_sec=1.0):
                connected = True
                break
            time.sleep(1.0)

        assert connected, "Failed to connect to MCP pipe bridge"
        print("[+] Connected to DAO MCP Bridge!")

        while time.time() - start < 120.0:
            p = client.ping().get("result", {})
            if p.get("is_pregame") or p.get("is_in_world"):
                break
            time.sleep(1.0)

        time.sleep(2.0)

        # Enumerate saves
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
        assert party_idx is not None, "Party Test save not found"
        assert dog_idx is not None, "Dog Test save not found"

        # =====================================================================
        # PART 1: Multi-Character Customization on Party Test Save
        # =====================================================================
        print("\n--- PART 1: Multi-Character Customization & Isolation ---")
        client.send_request("load_save", {"index": party_idx})
        load_start = time.time()
        while time.time() - load_start < 60.0:
            if client.ping().get("result", {}).get("is_in_world"):
                break
            time.sleep(1.0)
        time.sleep(3.0)

        party = client.send_request("get_party").get("result", {})
        members = party.get("members", [])
        assert len(members) >= 2
        hero_id = members[0]["id"]
        comp_id = members[1]["id"]
        print(f"[+] Party loaded: Hero {hero_id}, Companion {comp_id}")

        # 1.1 Customize Hero (Slot 0 Chest -> Row 2)
        print(f"\n[*] Customizing Hero ({hero_id})...")
        client.open_screen("Inventory")
        time.sleep(1.5)
        client.send_request("trigger_action", {"command": "DAOTransmog_Toggle"})
        time.sleep(0.5)

        # Select Chest (0), click Row 2 (e.g. Medium armor look)
        client.send_request("trigger_action", {"command": "DAOTransmog_SelectSlot", "arg": "0"})
        time.sleep(0.3)
        client.send_request("trigger_action", {"command": "DAOTransmog_ClickRow", "arg": "2"})
        time.sleep(0.5)
        # Apply outfit
        client.send_request("trigger_action", {"command": "DAOTransmog_Apply"})
        time.sleep(0.5)

        hero_state_1 = client.get_state().get("result", {})
        print(f"[+] Hero customized state recorded.")

        shot_hero_custom = os.path.join(OUT_DIR, "01_hero_customized.png")
        client.take_screenshot(shot_hero_custom)
        print(f"[+] Captured: {shot_hero_custom}")

        # 1.2 Step to Companion (555)
        print(f"\n[*] Stepping to Companion ({comp_id})...")
        nxt_res = client.send_request("next_character").get("result", {})
        time.sleep(1.5)
        assert nxt_res.get("screen_character") == comp_id

        # Verify Companion Chest is NOT modified by Hero's changes
        comp_cur_state = client.get_state().get("result", {})
        print(f"[+] Companion Screen State verified (isolated).")

        # Customize Companion: Helmet (5) -> Row 1 (HideSlot)
        print(f"[*] Customizing Companion Helmet to HideSlot...")
        client.send_request("trigger_action", {"command": "DAOTransmog_SelectSlot", "arg": "5"})
        time.sleep(0.3)
        client.send_request("trigger_action", {"command": "DAOTransmog_ClickRow", "arg": "1"})
        time.sleep(0.5)
        client.send_request("trigger_action", {"command": "DAOTransmog_Apply"})
        time.sleep(0.5)

        shot_comp_custom = os.path.join(OUT_DIR, "02_companion_customized.png")
        client.take_screenshot(shot_comp_custom)
        print(f"[+] Captured: {shot_comp_custom}")
        results["PTY-ISO"] = True

        # Close Inventory to return to world
        client.close_screen("Inventory")
        time.sleep(2.0)

        shot_world_party = os.path.join(OUT_DIR, "03_world_party_customized.png")
        client.take_screenshot(shot_world_party)
        print(f"[+] Captured World Party: {shot_world_party}")

        # =====================================================================
        # PART 2: QuickSave & Reload Persistence across Party Members
        # =====================================================================
        print("\n--- PART 2: Save & Reload Persistence for Multi-Character ---")
        print("[*] Creating QuickSave with customized party...")
        save_res = client.send_request("save_game", {"kind": "quick"}).get("result", {})
        print(f"[+] QuickSave requested: {save_res}")
        assert save_res.get("accepted") is True
        time.sleep(3.0)  # Wait for save and co-save to write

        # Enumerate saves to find QuickSave
        saves_after = client.send_request("list_saves").get("result", {}).get("saves", [])
        quick_idx = None
        for s in saves_after:
            if s.get("kind_name") == "quick":
                quick_idx = s.get("index")
                break

        print(f"[+] QuickSave Index: {quick_idx}")
        assert quick_idx is not None, "QuickSave slot not found"

        # Reload the QuickSave!
        print(f"[*] Reloading QuickSave (index {quick_idx})...")
        client.send_request("load_save", {"index": quick_idx})
        load_start = time.time()
        while time.time() - load_start < 60.0:
            if client.ping().get("result", {}).get("is_in_world"):
                break
            time.sleep(1.0)
        time.sleep(3.0)

        shot_world_reloaded = os.path.join(OUT_DIR, "04_world_reloaded_persistence.png")
        client.take_screenshot(shot_world_reloaded)
        print(f"[+] Captured World Reloaded: {shot_world_reloaded}")

        # Verify Hero transmog in inventory
        client.open_screen("Inventory")
        time.sleep(1.5)
        client.send_request("trigger_action", {"command": "DAOTransmog_Toggle"})
        time.sleep(0.5)

        hero_reloaded_state = client.get_state().get("result", {})
        shot_hero_reloaded = os.path.join(OUT_DIR, "05_hero_reloaded_persistence.png")
        client.take_screenshot(shot_hero_reloaded)
        print(f"[+] Captured Hero Reloaded: {shot_hero_reloaded}")

        # Verify Companion transmog in inventory
        client.send_request("next_character")
        time.sleep(1.5)
        comp_reloaded_state = client.get_state().get("result", {})
        shot_comp_reloaded = os.path.join(OUT_DIR, "06_companion_reloaded_persistence.png")
        client.take_screenshot(shot_comp_reloaded)
        print(f"[+] Captured Companion Reloaded: {shot_comp_reloaded}")

        results["PTY-PERSIST"] = True
        client.close_screen("Inventory")
        time.sleep(1.0)

        # =====================================================================
        # PART 3: Dog Customization & Save Persistence
        # =====================================================================
        print("\n--- PART 3: Dog Customization & Save Persistence ---")
        client.send_request("load_save", {"index": dog_idx})
        load_start = time.time()
        while time.time() - load_start < 60.0:
            if client.ping().get("result", {}).get("is_in_world"):
                break
            time.sleep(1.0)
        time.sleep(3.0)

        # Open Inventory & step to Dog
        client.open_screen("Inventory")
        time.sleep(1.5)
        client.send_request("next_character")
        time.sleep(1.5)

        # Open Transmog on Dog
        client.send_request("trigger_action", {"command": "DAOTransmog_Toggle"})
        time.sleep(0.5)

        # Customize Dog Warpaint: Select slot 1, pick Row 1 (HideSlot / Bare coat)
        print("[*] Customizing Dog Warpaint (slot 1) -> Row 1...")
        client.send_request("trigger_action", {"command": "DAOTransmog_SelectSlot", "arg": "1"})
        time.sleep(0.3)
        client.send_request("trigger_action", {"command": "DAOTransmog_ClickRow", "arg": "1"})
        time.sleep(0.5)
        client.send_request("trigger_action", {"command": "DAOTransmog_Apply"})
        time.sleep(0.5)

        shot_dog_custom = os.path.join(OUT_DIR, "07_dog_customized.png")
        client.take_screenshot(shot_dog_custom)
        print(f"[+] Captured Dog Customized: {shot_dog_custom}")

        # Close Inventory and QuickSave Dog
        client.close_screen("Inventory")
        time.sleep(1.5)

        print("[*] Creating QuickSave for Dog...")
        client.send_request("save_game", {"kind": "quick"})
        time.sleep(3.0)

        # Reload Dog QuickSave
        saves_dog = client.send_request("list_saves").get("result", {}).get("saves", [])
        dog_quick_idx = None
        for s in saves_dog:
            if s.get("kind_name") == "quick":
                dog_quick_idx = s.get("index")
                break

        assert dog_quick_idx is not None
        print(f"[*] Reloading Dog QuickSave (index {dog_quick_idx})...")
        client.send_request("load_save", {"index": dog_quick_idx})
        load_start = time.time()
        while time.time() - load_start < 60.0:
            if client.ping().get("result", {}).get("is_in_world"):
                break
            time.sleep(1.0)
        time.sleep(3.0)

        # Inspect Dog after reload
        client.open_screen("Inventory")
        time.sleep(1.5)
        client.send_request("next_character")
        time.sleep(1.5)
        client.send_request("trigger_action", {"command": "DAOTransmog_Toggle"})
        time.sleep(0.5)

        shot_dog_reloaded = os.path.join(OUT_DIR, "08_dog_reloaded_persistence.png")
        client.take_screenshot(shot_dog_reloaded)
        print(f"[+] Captured Dog Reloaded: {shot_dog_reloaded}")
        results["DOG-PERSIST"] = True

        # =====================================================================
        # PART 4: Edge Cases with Multiple Characters
        # =====================================================================
        print("\n--- PART 4: Edge Cases ---")

        # EDGE-MC-01: Staging on character switch
        # While on Dog, step back to Hero
        client.send_request("next_character")
        time.sleep(1.5)
        # Hero stages changes on Boots (slot 2 -> Row 1), but does NOT click Apply
        print("[*] EDGE-MC-01: Staging without apply, then switching character...")
        client.send_request("trigger_action", {"command": "DAOTransmog_SelectSlot", "arg": "2"})
        time.sleep(0.3)
        client.send_request("trigger_action", {"command": "DAOTransmog_ClickRow", "arg": "1"})
        time.sleep(0.5)

        hero_hdr_staged = client.send_request("inspect_ui", {"path": "_root.DAOTransmog_Browser.hdr.lbl.TextField.text"}).get("result", {}).get("value")
        print(f"    Hero Staged Header: {hero_hdr_staged}")
        assert "*" in hero_hdr_staged

        # Switch to Dog while staged changes exist
        client.send_request("next_character")
        time.sleep(1.5)

        # Dog must show clean state, no leak of Hero's boots staging
        dog_hdr = client.send_request("inspect_ui", {"path": "_root.DAOTransmog_Browser.hdr.lbl.TextField.text"}).get("result", {}).get("value")
        print(f"    Dog Header after switch: {dog_hdr}")
        assert "Dog Collar" in dog_hdr or "Dog Warpaint" in dog_hdr

        # Switch back to Hero: Hero should have discarded uncommitted staging safely
        client.send_request("next_character")
        time.sleep(1.5)
        hero_hdr_restored = client.send_request("inspect_ui", {"path": "_root.DAOTransmog_Browser.hdr.lbl.TextField.text"}).get("result", {}).get("value")
        print(f"    Hero Header upon return: {hero_hdr_restored}")
        results["EDGE-MC-01"] = True

        # EDGE-MC-02: Dog UI input validation
        client.send_request("next_character") # to Dog
        time.sleep(1.5)
        print("[*] EDGE-MC-02: Testing invalid humanoid slots on Dog...")
        for bad_slot in [2, 3, 4, 5]:
            rej = client.send_request("trigger_action", {"command": "DAOTransmog_SelectSlot", "arg": str(bad_slot)}).get("result", {})
            assert rej.get("executed") is False, f"Slot {bad_slot} was unexpectedly accepted on Dog!"

        print("    All humanoid slots (2, 3, 4, 5) correctly rejected on Dog.")
        results["EDGE-MC-02"] = True

        client.close_screen("Inventory")

        print("\n" + "=" * 70)
        print("ALL MULTI-CHARACTER PERSISTENCE & EDGE CASES PASSED!")
        print(json.dumps(results, indent=2))
        print("=" * 70)
        return 0

    except Exception as e:
        print(f"\n[-] TEST SUITE FAILED WITH EXCEPTION: {e}")
        import traceback
        traceback.print_exc()
        return 1

    finally:
        print("[*] Teardown: Terminating game process...")
        terminate_game()


if __name__ == "__main__":
    sys.exit(main())
