"""
DAOTransmog Autonomous Test Runner
Automates: Game Launch -> Save Loading -> Test Matrix Execution -> Screenshot Capture -> Report Generation
"""

import os
import sys
import time
import subprocess
import json

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from client import DaoBridgeClient
import test_matrix_runner

GAME_EXE = r"D:\Entertainment\BlackFlag\Dragon Age Origins\bin_ship\DAOrigins.exe"
GAME_DIR = r"D:\Entertainment\BlackFlag\Dragon Age Origins\bin_ship"


def is_game_running():
    cmd = 'tasklist /FI "IMAGENAME eq DAOrigins.exe" /NH'
    output = subprocess.check_output(cmd, shell=True, text=True)
    return "DAOrigins.exe" in output


def launch_game_if_needed():
    if is_game_running():
        print("[*] DAOrigins.exe is already running.")
        return None

    if not os.path.exists(GAME_EXE):
        print(f"[-] ERROR: Game executable not found at: {GAME_EXE}")
        return None

    print(f"[*] Launching Dragon Age: Origins via Scheduled Task DAO_AutoLauncher...")
    try:
        res = subprocess.run(["schtasks", "/run", "/tn", "DAO_AutoLauncher"], check=True, capture_output=True, text=True)
        print(f"[+] Scheduled task triggered: {res.stdout.strip()}")
    except Exception as e:
        print(f"[-] schtasks failed: {e}. Falling back to os.startfile...")
        os.startfile(GAME_EXE)
    return None


def terminate_game():
    if is_game_running():
        print("[*] Terminating DAOrigins.exe...")
        subprocess.run(["taskkill", "/F", "/IM", "DAOrigins.exe"], capture_output=True)
        time.sleep(1.0)
        print("[+] DAOrigins.exe terminated.")


def wait_and_load_save(client, timeout_sec=300.0):
    start = time.time()
    print("[*] Waiting for game to initialize named pipe...")

    connected = False
    last_print = time.time()
    while time.time() - start < timeout_sec:
        if is_game_running() and client.connect(timeout_sec=1.0):
            connected = True
            break
        now = time.time()
        if now - last_print > 5.0:
            last_print = now
            print("[*] Waiting for DAOrigins.exe and named pipe...")
        time.sleep(1.0)

    if not connected:
        print("[-] FAILED: Timed out waiting for named pipe connection.")
        return False

    print("[+] Bridge connected! Polling game mode...")

    # Wait for game to reach Main Menu or In-Game
    last_print = time.time()
    while time.time() - start < timeout_sec:
        ping_res = {}
        try:
            ping_res = client.ping().get("result", {})
        except Exception:
            pass

        game_mode = ping_res.get("game_mode", -1)
        in_world = ping_res.get("is_in_world", False)
        is_pregame = ping_res.get("is_pregame", False)
        is_loading = ping_res.get("is_loading", False)

        now = time.time()
        if now - last_print > 3.0:
            last_print = now
            print(f"[*] Engine status: mode={game_mode}, pregame={is_pregame}, loading={is_loading}, in_world={in_world}")

        if in_world:
            print("[+] Game is already in world and ready!")
            return True

        if is_pregame:
            print("[+] Game reached Main Menu (Pregame). Listing saves...")
            time.sleep(1.0)
            saves_info = client.list_saves()
            saves_list = saves_info.get("saves", [])
            char_name = saves_info.get("character", "Unknown")
            print(f"[+] Found {len(saves_list)} saves for character '{char_name}'")

            if not saves_list:
                print("[-] No saves found to load!")
                return False

            # Prefer QuickSave if available, otherwise latest slot
            selected_idx = 0
            for idx, s in enumerate(saves_list):
                if s.get("kind_name") == "quick":
                    selected_idx = idx
                    break

            chosen_save = saves_list[selected_idx]
            print(f"[*] Auto-loading save: {chosen_save.get('kind_name')} Slot {chosen_save.get('number')} (index {selected_idx})...")
            load_res = client.load_save(index=selected_idx)
            print(f"[+] Load command accepted: {load_res.get('accepted')}")
            break

        time.sleep(0.5)

    # Wait for loading screen to complete and enter world
    print("[*] Waiting for world area to load...")
    load_start = time.time()
    last_print = load_start
    while time.time() - load_start < timeout_sec:
        ping_res = {}
        try:
            ping_res = client.ping().get("result", {})
        except Exception:
            pass

        in_world = ping_res.get("is_in_world", False)
        if in_world:
            print("[+] In-world gameplay active! Exploration mode verified.")
            time.sleep(3.0)  # Let animations and D3D9 render targets fully settle
            return True

        now = time.time()
        if now - last_print > 3.0:
            last_print = now
            print(f"[*] Still loading: mode={ping_res.get('game_mode')}, is_loading={ping_res.get('is_loading')}")

        try:
            client.force_load()
        except Exception:
            pass
        time.sleep(0.5)

    print("[-] Timed out waiting for in-world gameplay.")
    return False


def main():
    print("=" * 60)
    print("DAOTransmog End-to-End Hands-Free Automation Runner")
    print("=" * 60)

    if "--no-launch" in sys.argv:
        print("[*] Mode: Waiting for DAOrigins.exe to be launched by user...")
    else:
        proc = launch_game_if_needed()

    client = DaoBridgeClient()

    ready = wait_and_load_save(client, timeout_sec=300.0)
    if not ready:
        print("[-] Automation aborted: game failed to reach in-world state.")
        return 1

    client.close()

    success = False
    try:
        # Run the test matrix / capture screenshots
        print("\n[*] Commencing automated test and screenshot capture...")
        show_summary = "--summary" in sys.argv
        success = test_matrix_runner.run_tests(show_summary=show_summary)
    finally:
        if "--keep-open" in sys.argv:
            print("\n[*] Keeping game running as requested by --keep-open.")
        else:
            print("\n[*] Closing game immediately after test and screenshot capture...")
            terminate_game()

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
