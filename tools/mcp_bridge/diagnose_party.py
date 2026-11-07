import os
import sys
import time
import json

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from client import DaoBridgeClient
from run_matrix_standalone import launch_game_if_needed, terminate_game

def run_diagnostics():
    print("=" * 60)
    print("Party & Dog Creature Inspection")
    print("=" * 60)

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

        if not connected:
            print("[-] Timed out connecting to bridge.")
            return 1

        print("[+] Connected! Waiting for Main Menu...")
        while time.time() - start < 120.0:
            ping_res = client.ping().get("result", {})
            if ping_res.get("is_pregame") or ping_res.get("is_in_world"):
                break
            time.sleep(1.0)

        time.sleep(2.0)

        saves_info = client.send_request("list_saves")
        saves = saves_info.get("result", {}).get("saves", [])
        party_idx = None
        dog_idx = None
        for s in saves:
            if s.get("kind_name") == "manual":
                if s.get("number") == 1:
                    party_idx = s.get("index")
                elif s.get("number") == 2:
                    dog_idx = s.get("index")

        print(f"[*] Party Save Index: {party_idx}, Dog Save Index: {dog_idx}")

        # Test Party Save
        if party_idx is not None:
            print(f"\n[*] Loading Party Test save (index {party_idx})...")
            client.send_request("load_save", {"index": party_idx})
            load_start = time.time()
            while time.time() - load_start < 60.0:
                p = client.ping().get("result", {})
                if p.get("is_in_world"):
                    break
                time.sleep(1.0)
            time.sleep(2.0)

            # Open inventory
            client.open_screen("Inventory")
            time.sleep(1.5)

            party_info = client.send_request("get_party")
            print(f"[+] Party Info:\n{json.dumps(party_info, indent=2)}")

            # Cycle screen character using next_character
            print("\n[*] Stepping through characters in Party Test:")
            for step in range(5):
                cur_screen = client.send_request("inspect_creature").get("result", {})
                print(f"  Step {step}: ScreenCharacter {cur_screen.get('character_id')}, AppType: {cur_screen.get('appearance_type')}, Racial: {cur_screen.get('racial_type')}, Items: {list(cur_screen.get('equipped_items', {}).keys())}")
                nxt = client.send_request("next_character").get("result", {})
                print(f"    next_character result: {nxt}")
                time.sleep(0.5)

            client.close_screen("Inventory")
            time.sleep(1.0)

        # Test Dog Save
        if dog_idx is not None:
            print(f"\n[*] Loading Dog Test save (index {dog_idx})...")
            client.send_request("load_save", {"index": dog_idx})
            load_start = time.time()
            while time.time() - load_start < 60.0:
                p = client.ping().get("result", {})
                if p.get("is_in_world"):
                    break
                time.sleep(1.0)
            time.sleep(2.0)

            client.open_screen("Inventory")
            time.sleep(1.5)

            dog_party = client.send_request("get_party")
            print(f"[+] Dog Party Info:\n{json.dumps(dog_party, indent=2)}")

            print("\n[*] Stepping through characters in Dog Test:")
            for step in range(5):
                cur_screen = client.send_request("inspect_creature").get("result", {})
                print(f"  Step {step}: ScreenCharacter {cur_screen.get('character_id')}, AppType: {cur_screen.get('appearance_type')}, Racial: {cur_screen.get('racial_type')}, Items: {list(cur_screen.get('equipped_items', {}).keys())}")
                nxt = client.send_request("next_character").get("result", {})
                print(f"    next_character result: {nxt}")
                time.sleep(0.5)

            client.close_screen("Inventory")

        print("\n[+] Diagnostics completed successfully!")
        return 0
    finally:
        print("[*] Terminating game process...")
        terminate_game()

if __name__ == "__main__":
    sys.exit(run_diagnostics())
