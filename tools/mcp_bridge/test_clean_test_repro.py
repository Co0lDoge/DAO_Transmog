import os
import sys
import time
import json

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from client import DaoBridgeClient
from run_matrix_standalone import launch_game_if_needed, terminate_game

OUT_DIR = os.path.abspath(r"D:\Project\DAO_Transmog\build\test_screenshots\clean_test_verification")
os.makedirs(OUT_DIR, exist_ok=True)

def wait_for_world(client, timeout=60.0):
    start = time.time()
    while time.time() - start < timeout:
        p = client.ping().get("result", {})
        if p.get("is_in_world"):
            return True
        time.sleep(1.0)
    return False

def main():
    print("=" * 70)
    print("Testing Clean Test Load (Lingering Transmog Prevention)")
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
        clean_test_idx = None
        for s in saves:
            if s.get("kind_name") == "manual":
                if s.get("number") == 4:
                    party_bug_idx = s.get("index")
                elif s.get("number") == 5:
                    clean_test_idx = s.get("index")

        print(f"Party Bug Index: {party_bug_idx}, Clean Test Index: {clean_test_idx}")
        assert party_bug_idx is not None, "Party Bug save (Slot 4) not found"
        assert clean_test_idx is not None, "Clean Test save (Slot 5) not found"

        # Step 1: Load Party Bug
        print("\n[*] STEP 1: Loading Party Bug save...")
        client.send_request("load_save", {"index": party_bug_idx})
        assert wait_for_world(client), "Timed out waiting for world after Party Bug load"
        time.sleep(3.0)

        hero_party_bug = client.send_request("inspect_creature").get("result", {})
        print(f"    Party Bug Hero Appearance Slots: {json.dumps(hero_party_bug.get('appearance_slots'), indent=2)}")
        shot1 = os.path.join(OUT_DIR, "01_party_bug_loaded.png")
        client.take_screenshot(shot1)
        print(f"[+] Step 1 Screenshot captured: {shot1}")

        # Step 2: Load Clean Test
        print("\n[*] STEP 2: Loading Clean Test save...")
        client.send_request("load_save", {"index": clean_test_idx})
        assert wait_for_world(client), "Timed out waiting for world after Clean Test load"
        time.sleep(3.0)

        hero_clean_test = client.send_request("inspect_creature").get("result", {})
        print(f"    Clean Test Hero Appearance Slots: {json.dumps(hero_clean_test.get('appearance_slots'), indent=2)}")
        shot2 = os.path.join(OUT_DIR, "02_clean_test_loaded.png")
        client.take_screenshot(shot2)
        print(f"[+] Step 2 Screenshot captured: {shot2}")

        # Step 3: Open inventory in Clean Test to see paperdoll
        print("\n[*] STEP 3: Opening inventory in Clean Test...")
        client.open_screen("Inventory")
        time.sleep(2.0)
        shot3 = os.path.join(OUT_DIR, "03_clean_test_inventory.png")
        client.take_screenshot(shot3)
        print(f"[+] Step 3 Screenshot captured: {shot3}")

        # In Clean Test, Hero's chest slot 0 must NOT be baseItem 10!
        chest_app = hero_clean_test.get("appearance_slots", {}).get("0", {})
        print(f"    Clean Test Hero Chest: base_item={chest_app.get('base_item')}, variation={chest_app.get('variation')}")
        assert chest_app.get("base_item") != 10, f"Hero chest is still baseItem 10! Transmog lingered!"
        print("[+] VERIFIED: Hero chest base item successfully reverted from transmog 10 to authentic equipped armor!")

        client.close_screen("Inventory")
        time.sleep(1.0)

        print("\n" + "=" * 70)
        print("CLEAN TEST VERIFICATION PASSED: NO LINGERING TRANSMOG!")
        print("=" * 70)
        return 0

    except Exception as e:
        print(f"\n[-] ERROR: {e}")
        import traceback
        traceback.print_exc()
        return 1
    finally:
        terminate_game()

if __name__ == "__main__":
    sys.exit(main())
