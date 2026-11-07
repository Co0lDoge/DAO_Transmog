"""
Dragon Age: Origins - In-Game MCP Test Bridge Client
Communicates with DAOTransmog.dll over Windows Named Pipe \\\\.\\pipe\\dao_mcp_bridge
"""

import ctypes
import ctypes.wintypes as wt
import json
import os
import sys
import time

try:
    from PIL import Image
except ImportError:
    Image = None

PIPE_NAME = r"\\.\pipe\dao_mcp_bridge"

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
FILE_ATTRIBUTE_NORMAL = 0x80


class DaoBridgeClient:
    def __init__(self, pipe_name=PIPE_NAME):
        self.pipe_name = pipe_name
        self.handle = None
        self._req_id = 1

    def connect(self, timeout_sec=5.0):
        start = time.time()
        while time.time() - start < timeout_sec:
            h_pipe = ctypes.windll.kernel32.CreateFileW(
                self.pipe_name,
                GENERIC_READ | GENERIC_WRITE,
                0,
                None,
                OPEN_EXISTING,
                0,
                None,
            )
            if h_pipe != -1 and h_pipe != 0:
                self.handle = h_pipe
                return True
            time.sleep(0.2)
        return False

    def close(self):
        if self.handle:
            ctypes.windll.kernel32.CloseHandle(self.handle)
            self.handle = None

    def send_request(self, method, params=None):
        if not self.handle:
            if not self.connect():
                raise ConnectionError(f"Could not connect to named pipe: {self.pipe_name}. Is DAOrigins.exe running with DAOTransmog loaded?")

        req_id = self._req_id
        self._req_id += 1

        payload = {"id": req_id, "method": method, "params": params or {}}
        raw_send = (json.dumps(payload) + "\n").encode("utf-8")

        written = wt.DWORD(0)
        ok = ctypes.windll.kernel32.WriteFile(
            self.handle,
            raw_send,
            len(raw_send),
            ctypes.byref(written),
            None,
        )
        if not ok:
            self.close()
            raise IOError("Failed to write request to pipe")

        buffer = ctypes.create_string_buffer(65536)
        read = wt.DWORD(0)
        ok = ctypes.windll.kernel32.ReadFile(
            self.handle,
            buffer,
            len(buffer) - 1,
            ctypes.byref(read),
            None,
        )
        if not ok or read.value == 0:
            self.close()
            raise IOError("Failed to read response from pipe")

        resp_str = buffer.raw[: read.value].decode("utf-8").strip()
        return json.loads(resp_str)

    def ping(self):
        return self.send_request("ping")

    def open_screen(self, screen="Inventory"):
        return self.send_request("open_screen", {"screen": screen})

    def close_screen(self, screen="Inventory"):
        return self.send_request("close_screen", {"screen": screen})

    def inspect_ui(self, path):
        res = self.send_request("inspect_ui", {"path": path})
        return res.get("result", {})

    def inspect_ui_batch(self, paths):
        res = self.send_request("inspect_ui_batch", {"paths": paths})
        return res.get("result", {}).get("values", {})

    def trigger_action(self, command, arg=""):
        res = self.send_request("trigger_action", {"command": command, "arg": str(arg)})
        return res.get("result", {})

    def get_state(self):
        res = self.send_request("get_state")
        return res.get("result", {})

    def list_saves(self):
        res = self.send_request("list_saves")
        return res.get("result", {})

    def load_save(self, index=0):
        res = self.send_request("load_save", {"index": index})
        return res.get("result", {})

    def force_load(self):
        res = self.send_request("force_load")
        return res.get("result", {})

    def quit_game(self):
        res = self.send_request("quit_game")
        return res.get("result", {})

    def send_key(self, key):
        res = self.send_request("send_key", {"key": key})
        return res.get("result", {})

    def take_screenshot(self, output_path):
        import shutil
        os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
        user_shots = os.path.expanduser("~/Documents/BioWare/Dragon Age/Screenshots/User")
        before_files = set(os.listdir(user_shots)) if os.path.exists(user_shots) else set()

        res = self.send_request("take_screenshot", {"filepath": os.path.abspath(output_path)})
        r = res.get("result", {})

        # Poll up to 3 seconds for the game engine to finish writing the DirectX 9 surface JPG
        start = time.time()
        new_shot = None
        while time.time() - start < 3.0:
            if os.path.exists(user_shots):
                current_files = set(os.listdir(user_shots))
                diff = current_files - before_files
                if diff:
                    new_shot = os.path.join(user_shots, sorted(list(diff))[-1])
                    break
            time.sleep(0.1)

        if new_shot and os.path.exists(new_shot):
            # Wait for game process to release file lock on the written JPG
            saved = False
            for _ in range(30):
                try:
                    if output_path.lower().endswith(".png") and Image:
                        with Image.open(new_shot) as im:
                            im.load()  # force read bytes into memory
                            im.save(output_path, "PNG")
                    else:
                        shutil.copyfile(new_shot, output_path)
                    saved = True
                    r["filepath"] = os.path.abspath(output_path)
                    r["success"] = True
                    r.pop("png_error", None)
                    break
                except (IOError, PermissionError) as e:
                    time.sleep(0.1)
                except Exception as e:
                    r["png_error"] = str(e)
                    break

            if not saved and "png_error" not in r:
                r["png_error"] = f"Failed to acquire file lock on {new_shot}"
        return r


if __name__ == "__main__":
    client = DaoBridgeClient()
    print("Connecting to DAO Test Bridge at", PIPE_NAME)
    if client.connect(timeout_sec=2.0):
        print("Connected successfully!")
        ping_res = client.ping()
        print("Ping response:", json.dumps(ping_res, indent=2))
        client.close()
    else:
        print("Could not connect to pipe (game not running or bridge inactive).")
