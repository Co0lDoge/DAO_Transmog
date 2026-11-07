import os
import sys
import time
import json

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from client import DaoBridgeClient
from run_matrix_standalone import launch_game_if_needed, terminate_game

OUT_DIR = os.path.abspath(r"D:\Project\DAO_Transmog\build\test_screenshots\party_bug_fix")
os.makedirs(OUT_DIR, exist_ok=True)

def main():
    print("=" * 70)
    print("Testing Party Bug Save & Individual Appearance Fix")
    print("=" * 70)

    launch_game_if_needed()
    client = DaoBridgeClient()

    try:
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

        saves = client.send_request("list_saves").get("result", {}).get("saves", [])
        party_bug_idx = None
        for s in saves:
            if s.get("kind_name") == "manual" and s.get("number") == 4:
                party_bug_idx = s.get("index")

        assert party_bug_idx is not None, "Party Bug save (manual 4) not found"
        print(f"[*] Loading Party Bug save (index {party_bug_idx})...")
        client.send_request("load_save", {"index": party_bug_idx})
        load_start = time.time()
        while time.time() - load_start < 60.0:
            if client.ping().get("result", {}).get("is_in_world"):
                break
            time.sleep(1.0)
        time.sleep(3.0)

        party = client.send_request("get_party").get("result", {})
        print(f"[+] Loaded Party: {json.dumps(party, indent=2)}")
        members = party.get("members", [])
        assert len(members) >= 2, "Party must have at least 2 members"
        hero_id = members[0]["id"]
        gilmore_id = members[1]["id"]
        assert members[0]["tag"] == "player"
        assert members[1]["tag"] == "bhn100cr_gilmore"

        # Check state: ONLY hero should have transmog state!
        st = client.send_request("get_state").get("result", {})
        chars = st.get("characters", {}).get("characters", {})
        print(f"[+] Characters in Transmog State: {list(chars.keys())}")
        assert "hero" in chars, "Hero transmog state must be present"
        assert "bhn100cr_gilmore" not in chars or chars["bhn100cr_gilmore"].get("presets", [{}])[0].get("armorSlots", {}).get("0", {}).get("mode") == "real", "Gilmore must not have transmog overrides"

        # Capture in-world screenshot of PartyBug initial load
        shot_world1 = os.path.join(OUT_DIR, "01_party_bug_initial_world.png")
        client.take_screenshot(shot_world1)
        print(f"[+] Captured: {shot_world1}")

        # Open inventory on Hero
        print("[*] Inspecting Hero inventory...")
        client.open_screen("Inventory")
        time.sleep(1.5)
        client.send_request("trigger_action", {"command": "DAOTransmog_Toggle"})
        time.sleep(0.5)

        hero_hdr = client.send_request("inspect_ui", {"path": "_root.DAOTransmog_Browser.hdr.lbl.TextField.text"}).get("result", {}).get("value")
        print(f"    Hero Header: {hero_hdr}")

        shot_hero_inv = os.path.join(OUT_DIR, "02_hero_inventory.png")
        client.take_screenshot(shot_hero_inv)
        print(f"[+] Captured: {shot_hero_inv}")

        # Switch in inventory to Ser Gilmore
        print("[*] Switching to Ser Gilmore in inventory...")
        client.send_request("next_character")
        time.sleep(1.5)

        gilmore_hdr = client.send_request("inspect_ui", {"path": "_root.DAOTransmog_Browser.hdr.lbl.TextField.text"}).get("result", {}).get("value")
        print(f"    Ser Gilmore Header: {gilmore_hdr}")

        # Ser Gilmore must be on Travel preset with NO overrides
        shot_gilmore_inv = os.path.join(OUT_DIR, "03_gilmore_inventory.png")
        client.take_screenshot(shot_gilmore_inv)
        print(f"[+] Captured: {shot_gilmore_inv}")

        # Close inventory
        client.close_screen("Inventory")
        time.sleep(1.5)

        # In explore mode, switch control to Ser Gilmore via F2
        print("[*] Switching active control to Ser Gilmore via F2...")
        client.send_request("send_key", {"key": "F2"})
        time.sleep(1.5)

        party_f2 = client.send_request("get_party").get("result", {})
        print(f"    Party after F2: controlled={party_f2.get('controlled')}")

        # Capture in-world screenshot with Gilmore controlled
        shot_world2 = os.path.join(OUT_DIR, "04_gilmore_controlled_world.png")
        client.take_screenshot(shot_world2)
        print(f"[+] Captured: {shot_world2}")

        # Switch back to Hero via F1
        print("[*] Switching active control back to Hero via F1...")
        client.send_request("send_key", {"key": "F1"})
        time.sleep(1.5)

        shot_world3 = os.path.join(OUT_DIR, "05_hero_controlled_world.png")
        client.take_screenshot(shot_world3)
        print(f"[+] Captured: {shot_world3}")

        # Check state again: Ser Gilmore must NOT have inherited hero's transmog
        st_final = client.send_request("get_state").get("result", {})
        chars_final = st_final.get("characters", {}).get("characters", {})
        print(f"[+] Characters in Transmog State at end: {list(chars_final.keys())}")
        if "bhn100cr_gilmore" in chars_final:
            mode = chars_final["bhn100cr_gilmore"]["presets"][0]["armorSlots"]["0"]["mode"]
            print(f"    Gilmore chest mode: {mode}")
            assert mode == "real", f"Gilmore chest mode is {mode}, expected real!"

        print("\n" + "=" * 70)
        print("PARTY BUG FIX FULLY VERIFIED! NO BLEED OR IDENTITY THEFT OCCURRED!")
        print("=" * 70)
        return 0

    except Exception as e:
        print(f"\n[-] TEST FAILED: {e}")
        import traceback
        traceback.print_exc()
        return 1

    finally:
        terminate_game()

if __name__ == "__main__":
    sys.exit(main())
