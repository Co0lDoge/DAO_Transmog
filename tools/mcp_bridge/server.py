"""
Dragon Age: Origins - Model Context Protocol (MCP) Stdio Server
Bridges Antigravity / AI Agent to live running DAOrigins.exe via Named Pipe.
"""

import sys
import json
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from client import DaoBridgeClient


def create_tools_list():
    return [
        {
            "name": "game_ping",
            "description": "Check if Dragon Age: Origins is running and connected via the Test Bridge named pipe.",
            "inputSchema": {
                "type": "object",
                "properties": {},
            },
        },
        {
            "name": "game_open_screen",
            "description": "Programmatically open a game screen (e.g. 'Inventory', 'optionsmenu').",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "screen": {"type": "string", "description": "Screen name (default: 'Inventory')"}
                },
            },
        },
        {
            "name": "game_close_screen",
            "description": "Programmatically close a game screen (e.g. 'Inventory').",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "screen": {"type": "string", "description": "Screen name (default: 'Inventory')"}
                },
            },
        },
        {
            "name": "game_inspect_ui",
            "description": "Inspect a live Scaleform ActionScript variable or property in the active screen.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "ActionScript path (e.g. '_root.DAOTransmog_mc._visible')"}
                },
                "required": ["path"],
            },
        },
        {
            "name": "game_inspect_ui_batch",
            "description": "Inspect multiple live Scaleform ActionScript variables in one batch call.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "paths": {"type": "array", "items": {"type": "string"}, "description": "List of ActionScript paths"}
                },
                "required": ["paths"],
            },
        },
        {
            "name": "game_trigger_action",
            "description": "Simulate clicking a Transmog UI button or executing an FSCommand.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "command": {"type": "string", "description": "Command name (e.g. 'DAOTransmog_Toggle', 'DAOTransmog_ClickRow')"},
                    "arg": {"type": "string", "description": "Optional command argument (e.g. '0', '1', '5')"}
                },
                "required": ["command"],
            },
        },
        {
            "name": "game_get_state",
            "description": "Query player character appearance state, active preset, and equipment slot overrides.",
            "inputSchema": {
                "type": "object",
                "properties": {},
            },
        },
        {
            "name": "game_send_key",
            "description": "Simulate keyboard input to the game window (e.g. 'I', 'T', 'H', 'P', 'ESC').",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "key": {"type": "string", "description": "Key name (e.g. 'I', 'T', 'H', 'P', 'ESC')"}
                },
                "required": ["key"],
            },
        },
        {
            "name": "game_take_screenshot",
            "description": "Capture the game window and save to disk as a PNG for multimodal visual inspection.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "filepath": {"type": "string", "description": "Destination file path (e.g. 'build/test_screenshots/check1.png')"}
                },
                "required": ["filepath"],
            },
        },
        {
            "name": "game_run_matrix",
            "description": "Execute the automated test matrix suite against the running game and return the report.",
            "inputSchema": {
                "type": "object",
                "properties": {},
            },
        },
    ]


def handle_call_tool(client, name, arguments):
    if name == "game_ping":
        return client.ping()
    elif name == "game_open_screen":
        return client.open_screen(arguments.get("screen", "Inventory"))
    elif name == "game_close_screen":
        return client.close_screen(arguments.get("screen", "Inventory"))
    elif name == "game_inspect_ui":
        return client.inspect_ui(arguments.get("path", ""))
    elif name == "game_inspect_ui_batch":
        return client.inspect_ui_batch(arguments.get("paths", []))
    elif name == "game_trigger_action":
        return client.trigger_action(arguments.get("command", ""), arguments.get("arg", ""))
    elif name == "game_get_state":
        return client.get_state()
    elif name == "game_send_key":
        return client.send_key(arguments.get("key", ""))
    elif name == "game_take_screenshot":
        fp = arguments.get("filepath", "build/test_screenshots/capture.png")
        return client.take_screenshot(fp)
    elif name == "game_run_matrix":
        import test_matrix_runner
        # Run test runner and read json report
        test_matrix_runner.run_tests()
        rep_path = os.path.abspath(r"build\test_screenshots\test_report.json")
        if os.path.exists(rep_path):
            with open(rep_path, "r", encoding="utf-8") as f:
                return json.load(f)
        return {"error": "Report not generated"}
    else:
        raise ValueError(f"Unknown tool: {name}")


def main():
    client = DaoBridgeClient()

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue

        try:
            req = json.loads(line)
        except Exception as e:
            continue

        msg_id = req.get("id")
        method = req.get("method")
        params = req.get("params", {})

        if method == "initialize":
            resp = {
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "dao-mcp-bridge", "version": "1.0.0"},
                },
            }
            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()
        elif method == "notifications/initialized":
            pass
        elif method == "tools/list":
            resp = {
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": {"tools": create_tools_list()},
            }
            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()
        elif method == "tools/call":
            tool_name = params.get("name")
            tool_args = params.get("arguments", {})
            try:
                result_data = handle_call_tool(client, tool_name, tool_args)
                resp = {
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "result": {
                        "content": [
                            {"type": "text", "text": json.dumps(result_data, indent=2)}
                        ]
                    },
                }
            except Exception as e:
                resp = {
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "isError": True,
                    "result": {
                        "content": [{"type": "text", "text": f"Error: {str(e)}"}]
                    },
                }
            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()
        else:
            if msg_id is not None:
                resp = {
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "error": {"code": -32601, "message": f"Method not found: {method}"},
                }
                sys.stdout.write(json.dumps(resp) + "\n")
                sys.stdout.flush()


if __name__ == "__main__":
    main()
