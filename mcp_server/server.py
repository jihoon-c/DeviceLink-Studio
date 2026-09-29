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
import re
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
        "name": "power_on",
        "description": "Power on the EO/IR device and verify that telemetry reports Ready.",
        "inputSchema": _schema(
            {"timeout_ms": {"type": "integer", "minimum": 1, "maximum": 30000, "default": 3000}}
        ),
        "annotations": {"readOnlyHint": False, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False},
    },
    {
        "name": "power_off",
        "description": "Power off the EO/IR device and verify that telemetry reports Offline.",
        "inputSchema": _schema(
            {"timeout_ms": {"type": "integer", "minimum": 1, "maximum": 30000, "default": 3000}}
        ),
        "annotations": {"readOnlyHint": False, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False},
    },
    {
        "name": "initialize_device",
        "description": "Home and initialize the EO/IR device, then verify that it becomes Ready.",
        "inputSchema": _schema(
            {"timeout_ms": {"type": "integer", "minimum": 1, "maximum": 30000, "default": 5000}}
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
        "name": "execute_instruction",
        "description": "Execute a Korean or English natural-language EO/IR instruction without relying on a Codex client. Compound commands are executed in safe device order.",
        "inputSchema": _schema(
            {
                "instruction": {"type": "string", "minLength": 2},
                "host": {"type": "string", "minLength": 1, "default": "127.0.0.1"},
                "port": {"type": "integer", "minimum": 1, "maximum": 65535, "default": 5000},
                "auto_connect": {"type": "boolean", "default": True},
                "auto_prepare": {"type": "boolean", "default": True},
            },
            ["instruction"],
        ),
        "annotations": {"readOnlyHint": False, "destructiveHint": True, "idempotentHint": False, "openWorldHint": False},
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
        self._connected_endpoint: tuple[str, int] | None = None

    @property
    def connected_endpoint(self) -> tuple[str, int] | None:
        return self._connected_endpoint

    @staticmethod
    def _find_executable() -> Path:
        configured = os.environ.get("DEVICELINK_AGENT_BRIDGE")
        candidates = [Path(configured)] if configured else []
        candidates.extend(
            [
                ROOT / "build-agent" / "DeviceLinkAgentBridge.exe",
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
                self._connected_endpoint = None
            assert self._process.stdin is not None
            assert self._process.stdout is not None
            self._process.stdin.write(line + "\n")
            self._process.stdin.flush()
            response = self._process.stdout.readline()
            if not response:
                raise RuntimeError("DeviceLinkAgentBridge stopped without a response")
            result = json.loads(response)
            parts = line.split()
            if result.get("ok") and len(parts) == 3 and parts[0] == "CONNECT":
                self._connected_endpoint = (parts[1], int(parts[2]))
            elif not result.get("ok") and parts and parts[0] == "CONNECT":
                self._process.terminate()
                self._process.wait(timeout=2)
                self._process = None
                self._connected_endpoint = None
            return result

    def close(self) -> None:
        with self._lock:
            if self._process is None or self._process.poll() is not None:
                self._connected_endpoint = None
                return
            try:
                assert self._process.stdin is not None
                self._process.stdin.write("QUIT\n")
                self._process.stdin.flush()
                self._process.wait(timeout=2)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                self._process.terminate()
            finally:
                self._connected_endpoint = None


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


def _contains_any(text: str, values: tuple[str, ...]) -> bool:
    return any(value in text for value in values)


def _extract_angle(instruction: str, names: tuple[str, ...]) -> float | None:
    joined = "|".join(re.escape(name) for name in names)
    match = re.search(
        rf"(?:{joined})\s*(?:각도)?\s*(?:은|는|을|를|=|:)?\s*(-?\d+(?:\.\d+)?)",
        instruction,
        re.IGNORECASE,
    )
    return float(match.group(1)) if match else None


def _execute_instruction(bridge: Bridge, arguments: dict[str, Any]) -> dict[str, Any]:
    instruction = arguments.get("instruction")
    if not isinstance(instruction, str) or len(instruction.strip()) < 2:
        raise ValueError("instruction must contain at least two characters")
    text = instruction.strip()
    lowered = text.lower()
    host = arguments.get("host", "127.0.0.1")
    if not isinstance(host, str) or not host or any(character.isspace() for character in host):
        raise ValueError("host must be a non-empty host name or IP address")
    port = _integer(arguments, "port", 5000)
    if not 1 <= port <= 65535:
        raise ValueError("port must be between 1 and 65535")
    auto_connect = arguments.get("auto_connect", True)
    auto_prepare = arguments.get("auto_prepare", True)
    if not isinstance(auto_connect, bool) or not isinstance(auto_prepare, bool):
        raise ValueError("auto_connect and auto_prepare must be booleans")

    endpoint = re.search(r"((?:\d{1,3}\.){3}\d{1,3}|localhost)\s*:\s*(\d{1,5})", text, re.IGNORECASE)
    if endpoint:
        host, port = endpoint.group(1), int(endpoint.group(2))
        if not 1 <= port <= 65535:
            raise ValueError("port must be between 1 and 65535")

    pan = _extract_angle(text, ("pan", "팬"))
    tilt = _extract_angle(text, ("tilt", "틸트"))
    has_any_angle = pan is not None or tilt is not None
    if has_any_angle and (pan is None or tilt is None):
        raise ValueError("Both Pan and Tilt angles are required")
    if pan is not None and not -170 <= pan <= 170:
        raise ValueError("Pan must be between -170 and 170 degrees")
    if tilt is not None and not -45 <= tilt <= 80:
        raise ValueError("Tilt must be between -45 and 80 degrees")

    wants_connect = _contains_any(lowered, ("연결", "connect")) or endpoint is not None
    wants_power_off = _contains_any(lowered, ("전원 끄", "전원 off", "power off"))
    wants_power_on = _contains_any(lowered, ("전원 켜", "전원 on", "power on"))
    wants_initialize = _contains_any(lowered, ("초기화", "initialize", "home"))
    wants_telemetry = _contains_any(lowered, ("텔레메트리", "telemetry", "상태 확인", "status"))
    wants_clear_fault = _contains_any(lowered, ("fault 해제", "고장 해제", "장애 해제", "clear fault", "복구"))
    fault_name = next((name for name in ("MotorStall", "OverTemperature", "SensorFailure")
                       if name.lower() in lowered.replace(" ", "")), None)
    wants_inject_fault = fault_name is not None and _contains_any(
        lowered, ("주입", "발생", "inject", "simulate"))
    wants_scenario = _contains_any(lowered, ("motor_stall_recovery", "복구 시험", "recovery test"))

    device_action = any((wants_power_off, wants_power_on, wants_initialize, has_any_angle,
                         wants_telemetry, wants_clear_fault, wants_inject_fault, wants_scenario))
    if not wants_connect and not device_action:
        return {
            "ok": False,
            "message": "Unsupported instruction. Include connect, power, initialize, Pan/Tilt, telemetry, fault, or recovery test.",
        }
    if wants_power_off and (has_any_angle or wants_power_on or wants_initialize):
        return {"ok": False, "message": "Power-off cannot be combined with power-on, initialize, or movement."}

    steps: list[dict[str, Any]] = []

    def run(command: str) -> bool:
        result = bridge.call(command)
        steps.append({"command": command, "result": result})
        return bool(result.get("ok"))

    needs_connection = wants_connect or device_action
    if needs_connection and bridge.connected_endpoint not in {None, (host, port)}:
        if not auto_connect and not wants_connect:
            return {
                "ok": False,
                "message": "Device is connected to a different endpoint and auto_connect is disabled",
                "steps": steps,
            }
        bridge.close()
    if needs_connection and ((wants_connect and bridge.connected_endpoint is None) or
                             (auto_connect and bridge.connected_endpoint is None)):
        if not run(f"CONNECT {host} {port}"):
            return {"ok": False, "message": "Connection failed", "steps": steps}
    elif needs_connection and bridge.connected_endpoint is None:
        return {"ok": False, "message": "Device is not connected and auto_connect is disabled"}

    if wants_power_off:
        if not run("POWER_OFF 3000"):
            return {"ok": False, "message": "Power-off failed", "steps": steps}
    else:
        prepare_device = auto_prepare and (has_any_angle or wants_initialize)
        if wants_power_on or prepare_device:
            if not run("POWER_ON 3000"):
                return {"ok": False, "message": "Power-on failed", "steps": steps}
        if wants_initialize or (has_any_angle and auto_prepare):
            if not run("INITIALIZE 5000"):
                return {"ok": False, "message": "Initialization failed", "steps": steps}
        if wants_scenario:
            if not run("RUN_SCENARIO motor_stall_recovery 5000"):
                return {"ok": False, "message": "Recovery scenario failed", "steps": steps}
        elif wants_inject_fault:
            if not run(f"INJECT_FAULT {fault_name} 3000"):
                return {"ok": False, "message": "Fault injection failed", "steps": steps}
        elif wants_clear_fault:
            if not run("CLEAR_FAULT 3000"):
                return {"ok": False, "message": "Fault recovery failed", "steps": steps}
        if has_any_angle and not run(f"SET_ANGLE {pan:g} {tilt:g} 5000 0.5"):
            return {"ok": False, "message": "Angle command failed", "steps": steps}
        if wants_telemetry and not has_any_angle and not run("GET_TELEMETRY 2000"):
            return {"ok": False, "message": "Telemetry request failed", "steps": steps}

    last_result = steps[-1]["result"] if steps else {}
    response: dict[str, Any] = {
        "ok": True,
        "message": "Natural-language instruction completed",
        "instruction": text,
        "steps": steps,
    }
    if "telemetry" in last_result:
        response["telemetry"] = last_result["telemetry"]
    return response


def call_tool(bridge: Bridge, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    if name == "connect_device":
        host = arguments.get("host")
        if not isinstance(host, str) or not host or any(character.isspace() for character in host):
            raise ValueError("host must be a non-empty host name or IP address")
        port = _integer(arguments, "port", 5000)
        if not 1 <= port <= 65535:
            raise ValueError("port must be between 1 and 65535")
        return bridge.call(f"CONNECT {host} {port}")
    if name == "power_on":
        return bridge.call(f"POWER_ON {_integer(arguments, 'timeout_ms', 3000)}")
    if name == "power_off":
        return bridge.call(f"POWER_OFF {_integer(arguments, 'timeout_ms', 3000)}")
    if name == "initialize_device":
        return bridge.call(f"INITIALIZE {_integer(arguments, 'timeout_ms', 5000)}")
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
    if name == "execute_instruction":
        return _execute_instruction(bridge, arguments)
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
