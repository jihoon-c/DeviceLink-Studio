#!/usr/bin/env python3
"""DeviceLink Studio MCP stdio server.

The server owns no device protocol logic. It maps MCP calls to the native
DeviceLinkAgentBridge, which calls the Application layer and existing protocol
and transport implementations.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import threading
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
PROTOCOL_VERSION = "2025-11-25"
SERVER_INFO = {"name": "devicelink-studio", "version": "0.1.0"}


def _schema(properties: dict[str, Any], required: list[str] | None = None) -> dict[str, Any]:
    value: dict[str, Any] = {
        "type": "object",
        "properties": properties,
        "additionalProperties": False,
    }
    if required:
        value["required"] = required
    return value


TOOLS = [
    {
        "name": "connect_device",
        "description": "Connect DeviceLink Studio to the Unreal EO/IR virtual device over TCP.",
        "inputSchema": _schema(
            {
                "host": {"type": "string", "minLength": 1, "description": "Device host or IP address"},
                "port": {"type": "integer", "minimum": 1, "maximum": 65535, "default": 5000},
            },
            ["host"],
        ),
        "annotations": {"readOnlyHint": False, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False},
    },
    {
        "name": "set_gimbal_angle",
        "description": "Command an absolute pan/tilt angle and verify from telemetry that the gimbal reached it.",
        "inputSchema": _schema(
            {
                "pan_degrees": {"type": "number", "minimum": -170, "maximum": 170},
                "tilt_degrees": {"type": "number", "minimum": -45, "maximum": 80},
                "timeout_ms": {"type": "integer", "minimum": 1, "maximum": 30000, "default": 5000},
                "tolerance_degrees": {"type": "number", "minimum": 0, "maximum": 10, "default": 0.5},
            },
            ["pan_degrees", "tilt_degrees"],
        ),
        "annotations": {"readOnlyHint": False, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False},
    },
    {
        "name": "get_telemetry",
        "description": "Request current device state, fault, actual and target angles, temperature, and voltage.",
        "inputSchema": _schema(
            {"timeout_ms": {"type": "integer", "minimum": 1, "maximum": 30000, "default": 2000}}
        ),
        "annotations": {"readOnlyHint": True, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False},
    },
    {
        "name": "inject_fault",
        "description": "Inject a supported fault and verify the Fault state through telemetry.",
        "inputSchema": _schema(
            {
                "fault": {"type": "string", "enum": ["MotorStall", "OverTemperature", "SensorFailure"]},
                "timeout_ms": {"type": "integer", "minimum": 1, "maximum": 30000, "default": 3000},
            },
            ["fault"],
        ),
        "annotations": {"readOnlyHint": False, "destructiveHint": True, "idempotentHint": True, "openWorldHint": False},
    },
    {
        "name": "clear_fault",
        "description": "Clear the active simulated fault and verify that the device returns to Ready.",
        "inputSchema": _schema(
            {"timeout_ms": {"type": "integer", "minimum": 1, "maximum": 30000, "default": 3000}}
        ),
        "annotations": {"readOnlyHint": False, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False},
    },
    {
        "name": "run_test_scenario",
        "description": "Run a deterministic built-in equipment test and return its observed steps and verdict.",
        "inputSchema": _schema(
            {
                "scenario": {"type": "string", "enum": ["motor_stall_recovery"]},
                "timeout_ms": {"type": "integer", "minimum": 1, "maximum": 30000, "default": 5000},
            },
            ["scenario"],
        ),
        "annotations": {"readOnlyHint": False, "destructiveHint": True, "idempotentHint": True, "openWorldHint": False},
    },
    {
        "name": "search_manual",
        "description": "Search the local EO/IR operating manual and fault code guide before choosing an action.",
        "inputSchema": _schema(
            {"query": {"type": "string", "minLength": 2}, "limit": {"type": "integer", "minimum": 1, "maximum": 5, "default": 3}},
            ["query"],
        ),
        "annotations": {"readOnlyHint": True, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False},
    },
]

DOCUMENTS = {
    "devicelink://manual/eo-ir": ROOT / "docs" / "agent" / "eo-ir-manual.md",
    "devicelink://manual/fault-codes": ROOT / "docs" / "agent" / "fault-codes.md",
}


class Bridge:
    def __init__(self) -> None:
        self._process: subprocess.Popen[str] | None = None
        self._lock = threading.Lock()

    @staticmethod
    def _find_executable() -> Path:
        configured = os.environ.get("DEVICELINK_AGENT_BRIDGE")
        candidates = [Path(configured)] if configured else []
        candidates.extend(
            [
                ROOT / "build" / "Release" / "DeviceLinkAgentBridge.exe",
                ROOT / "build" / "Debug" / "DeviceLinkAgentBridge.exe",
                ROOT / "build" / "DeviceLinkAgentBridge.exe",
            ]
        )
        for candidate in candidates:
            if candidate.is_file():
                return candidate
        raise FileNotFoundError(
            "DeviceLinkAgentBridge.exe was not found. Build the CMake target or set "
            "DEVICELINK_AGENT_BRIDGE to its absolute path."
        )

    def call(self, line: str) -> dict[str, Any]:
        with self._lock:
            if self._process is None or self._process.poll() is not None:
                executable = self._find_executable()
                self._process = subprocess.Popen(
                    [str(executable)],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                    stderr=sys.stderr,
                    text=True,
                    encoding="utf-8",
                    bufsize=1,
                )
            assert self._process.stdin is not None
            assert self._process.stdout is not None
            self._process.stdin.write(line + "\n")
            self._process.stdin.flush()
            response = self._process.stdout.readline()
            if not response:
                raise RuntimeError("DeviceLinkAgentBridge stopped without a response")
            return json.loads(response)

    def close(self) -> None:
        with self._lock:
            if self._process is None or self._process.poll() is not None:
                return
            try:
                assert self._process.stdin is not None
                self._process.stdin.write("QUIT\n")
                self._process.stdin.flush()
                self._process.wait(timeout=2)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                self._process.terminate()


def _number(arguments: dict[str, Any], key: str) -> float:
    value = arguments.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{key} must be a number")
    return float(value)


def _integer(arguments: dict[str, Any], key: str, default: int) -> int:
    value = arguments.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{key} must be an integer")
    return value


def _search_manual(query: str, limit: int) -> dict[str, Any]:
    terms = {term.lower() for term in query.replace("/", " ").replace("_", " ").split() if len(term) > 1}
    matches: list[tuple[int, dict[str, str]]] = []
    for uri, path in DOCUMENTS.items():
        text = path.read_text(encoding="utf-8")
        sections = [section.strip() for section in text.split("\n## ") if section.strip()]
        for section in sections:
            lowered = section.lower()
            score = sum(lowered.count(term) for term in terms)
            if score:
                matches.append((score, {"source": uri, "excerpt": section[:900]}))
    matches.sort(key=lambda item: item[0], reverse=True)
    return {
        "ok": bool(matches),
        "message": "Manual evidence found" if matches else "No matching manual section found",
        "matches": [item[1] for item in matches[:limit]],
    }


def call_tool(bridge: Bridge, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    if name == "connect_device":
        host = arguments.get("host")
        if not isinstance(host, str) or not host or any(character.isspace() for character in host):
            raise ValueError("host must be a non-empty host name or IP address")
        port = _integer(arguments, "port", 5000)
        if not 1 <= port <= 65535:
            raise ValueError("port must be between 1 and 65535")
        return bridge.call(f"CONNECT {host} {port}")
    if name == "set_gimbal_angle":
        pan = _number(arguments, "pan_degrees")
        tilt = _number(arguments, "tilt_degrees")
        timeout = _integer(arguments, "timeout_ms", 5000)
        tolerance = _number(arguments, "tolerance_degrees") if "tolerance_degrees" in arguments else 0.5
        return bridge.call(f"SET_ANGLE {pan} {tilt} {timeout} {tolerance}")
    if name == "get_telemetry":
        return bridge.call(f"GET_TELEMETRY {_integer(arguments, 'timeout_ms', 2000)}")
    if name == "inject_fault":
        fault = arguments.get("fault")
        if fault not in {"MotorStall", "OverTemperature", "SensorFailure"}:
            raise ValueError("fault must be MotorStall, OverTemperature, or SensorFailure")
        return bridge.call(f"INJECT_FAULT {fault} {_integer(arguments, 'timeout_ms', 3000)}")
    if name == "clear_fault":
        return bridge.call(f"CLEAR_FAULT {_integer(arguments, 'timeout_ms', 3000)}")
    if name == "run_test_scenario":
        scenario = arguments.get("scenario")
        if scenario != "motor_stall_recovery":
            raise ValueError("scenario must be motor_stall_recovery")
        return bridge.call(f"RUN_SCENARIO {scenario} {_integer(arguments, 'timeout_ms', 5000)}")
    if name == "search_manual":
        query = arguments.get("query")
        if not isinstance(query, str) or len(query.strip()) < 2:
            raise ValueError("query must contain at least two characters")
        return _search_manual(query, _integer(arguments, "limit", 3))
    raise ValueError(f"Unknown tool: {name}")


def _result(request_id: Any, value: dict[str, Any]) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "result": value}


def _error(request_id: Any, code: int, message: str) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


def handle(bridge: Bridge, request: dict[str, Any]) -> dict[str, Any] | None:
    request_id = request.get("id")
    method = request.get("method")
    if method and str(method).startswith("notifications/"):
        return None
    if method == "initialize":
        return _result(
            request_id,
            {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {"listChanged": False}, "resources": {"subscribe": False, "listChanged": False}},
                "serverInfo": SERVER_INFO,
                "instructions": "Use manual search for domain questions. Device-changing tools return observed telemetry; use that evidence for verdicts.",
            },
        )
    if method == "ping":
        return _result(request_id, {})
    if method == "tools/list":
        return _result(request_id, {"tools": TOOLS})
    if method == "tools/call":
        params = request.get("params", {})
        try:
            data = call_tool(bridge, params.get("name", ""), params.get("arguments", {}))
            return _result(
                request_id,
                {
                    "content": [{"type": "text", "text": json.dumps(data, ensure_ascii=False)}],
                    "structuredContent": data,
                    "isError": not data.get("ok", False),
                },
            )
        except (ValueError, FileNotFoundError, RuntimeError, json.JSONDecodeError) as exception:
            data = {"ok": False, "message": str(exception)}
            return _result(
                request_id,
                {"content": [{"type": "text", "text": json.dumps(data, ensure_ascii=False)}], "structuredContent": data, "isError": True},
            )
    if method == "resources/list":
        return _result(
            request_id,
            {
                "resources": [
                    {"uri": uri, "name": path.stem, "title": path.stem.replace("-", " ").title(), "mimeType": "text/markdown"}
                    for uri, path in DOCUMENTS.items()
                ]
            },
        )
    if method == "resources/read":
        uri = request.get("params", {}).get("uri")
        path = DOCUMENTS.get(uri)
        if path is None:
            return _error(request_id, -32602, "Unknown DeviceLink resource URI")
        return _result(request_id, {"contents": [{"uri": uri, "mimeType": "text/markdown", "text": path.read_text(encoding="utf-8")}]})
    return _error(request_id, -32601, f"Method not found: {method}")


def main() -> int:
    bridge = Bridge()
    try:
        for line in sys.stdin:
            try:
                request = json.loads(line)
                response = handle(bridge, request)
            except (json.JSONDecodeError, TypeError) as exception:
                response = _error(None, -32700, f"Invalid JSON-RPC request: {exception}")
            if response is not None:
                sys.stdout.write(json.dumps(response, ensure_ascii=False, separators=(",", ":")) + "\n")
                sys.stdout.flush()
    finally:
        bridge.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
