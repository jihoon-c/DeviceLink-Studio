#!/usr/bin/env python3
"""DeviceLink Studio MCP stdio server.

The server owns no device protocol logic. It maps MCP calls to the native
DeviceLinkAgentBridge, which calls the Application layer and existing protocol
and transport implementations.
"""

from __future__ import annotations

import json
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time
import uuid
from typing import Any

if __package__:
    from .instruction_service import execute_instruction
    from .diagnostic_service import diagnose_device
    from .diagnostic_reports import DiagnosticReports
    from .llm_diagnostics import explain as _langchain_explanation
else:
    from instruction_service import execute_instruction
    from diagnostic_service import diagnose_device
    from diagnostic_reports import DiagnosticReports
    from llm_diagnostics import explain as _langchain_explanation


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
        "name": "get_recent_events",
        "description": "Read a bounded history of this bridge session's device events without changing the device.",
        "inputSchema": _schema({"limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 20}}),
        "annotations": {"readOnlyHint": True, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False},
    },
    {
        "name": "get_event_history",
        "description": "Read persisted device events for an inclusive Unix-millisecond time window, capped at 100 entries.",
        "inputSchema": _schema({
            "from_unix_ms": {"type": "integer", "minimum": 0},
            "through_unix_ms": {"type": "integer", "minimum": 0},
            "limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 50},
        }, ["from_unix_ms", "through_unix_ms"]),
        "annotations": {"readOnlyHint": True, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False},
    },
    {
        "name": "diagnose_device",
        "description": "Rule-based diagnosis using fresh telemetry, events and manual evidence. Saves a report without changing device state. LLM explanation is optional.",
        "inputSchema": _schema({
            "timeout_ms": {"type": "integer", "minimum": 1, "maximum": 30000, "default": 2000},
            "event_limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 20},
            "include_llm": {"type": "boolean", "default": False},
        }),
        "annotations": {"readOnlyHint": False, "destructiveHint": False, "idempotentHint": False, "openWorldHint": False},
    },
    {
        "name": "verify_recovery",
        "description": "Request new telemetry and deterministically verify Ready state with no active fault.",
        "inputSchema": _schema({"timeout_ms": {"type": "integer", "minimum": 1, "maximum": 30000, "default": 2000}}),
        "annotations": {"readOnlyHint": True, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False},
    },
    {
        "name": "verify_diagnosis",
        "description": "Compare a prior diagnosed fault with new telemetry and persisted events; append the observed transition to its report.",
        "inputSchema": _schema({
            "diagnosis_id": {"type": "string", "minLength": 1},
            "timeout_ms": {"type": "integer", "minimum": 1, "maximum": 30000, "default": 2000},
        }, ["diagnosis_id"]),
        "annotations": {"readOnlyHint": False, "destructiveHint": False, "idempotentHint": False, "openWorldHint": False},
    },
    {
        "name": "get_diagnosis_report",
        "description": "Read a saved diagnosis, its cited evidence and subsequent verification records, including after a server restart.",
        "inputSchema": _schema({"diagnosis_id": {"type": "string", "pattern": "^[0-9a-f]{32}$"}}, ["diagnosis_id"]),
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
                "preview_only": {"type": "boolean", "default": False},
                "include_llm": {"type": "boolean", "default": False},
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
MANUAL_DIR = ROOT / "docs" / "agent"


class Bridge:
    def __init__(self, report_directory: Path | None = None,
                 environment: dict[str, str] | None = None) -> None:
        self._process: subprocess.Popen[str] | None = None
        self._environment = dict(environment) if environment is not None else None
        self._lock = threading.Lock()
        self._connected_endpoint: tuple[str, int] | None = None
        self._diagnoses: dict[str, dict[str, Any]] = {}
        self._reports = DiagnosticReports(report_directory or Path(os.environ.get(
            "DEVICELINK_DIAGNOSTIC_REPORT_DIR", str(ROOT / "artifacts" / "diagnostics"))))

    def record_diagnosis(self, device_id: str, observed_at: int,
                         telemetry: dict[str, Any]) -> str:
        diagnosis_id = uuid.uuid4().hex
        if len(self._diagnoses) >= 100:
            del self._diagnoses[next(iter(self._diagnoses))]
        self._diagnoses[diagnosis_id] = {"device_id": device_id,
                                         "observed_at_unix_ms": observed_at,
                                         "telemetry": telemetry}
        return diagnosis_id

    def diagnosis(self, diagnosis_id: str) -> dict[str, Any] | None:
        baseline = self._diagnoses.get(diagnosis_id)
        if baseline is not None:
            return baseline
        record = self._reports.load(diagnosis_id)
        return record["diagnosis"] if record else None

    def save_report(self, result: dict[str, Any], *, verification: bool = False) -> dict[str, Any]:
        try:
            return self._reports.save(result, verification=verification)
        except (OSError, ValueError):
            return {"status": "unavailable", "error_code": "report_write_failed"}

    def report(self, diagnosis_id: str) -> dict[str, Any]:
        record = self._reports.load(diagnosis_id)
        if record is None:
            return {"ok": False, "message": "Diagnostic report was not found"}
        return {"ok": True, "report": record}

    @property
    def connected_endpoint(self) -> tuple[str, int] | None:
        return self._connected_endpoint

    @staticmethod
    def _find_executable(environment: dict[str, str] | None = None) -> Path:
        configured = (environment if environment is not None else os.environ).get("DEVICELINK_AGENT_BRIDGE")
        candidates = [Path(configured)] if configured else []
        candidates.extend(
            [
                ROOT / "build-demo" / "DeviceLinkAgentBridge.exe",
                ROOT / "build-agent-diag" / "DeviceLinkAgentBridge.exe",
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
                if self._process is not None:
                    for stream in (self._process.stdin, self._process.stdout):
                        if stream is not None:
                            stream.close()
                executable = self._find_executable(self._environment)
                self._process = subprocess.Popen(
                    [str(executable)],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                    stderr=sys.stderr,
                    text=True,
                    encoding="utf-8",
                    bufsize=1,
                    env=self._environment,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
                )
                self._connected_endpoint = None
                self._diagnoses.clear()
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
                self._process.stdin.close()
                self._process.stdout.close()
                self._process = None
                self._connected_endpoint = None
            return result

    def close(self) -> None:
        with self._lock:
            if self._process is None:
                self._connected_endpoint = None
                self._diagnoses.clear()
                return
            try:
                if self._process.poll() is None:
                    assert self._process.stdin is not None
                    self._process.stdin.write("QUIT\n")
                    self._process.stdin.flush()
                    self._process.wait(timeout=2)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                self._process.terminate()
                self._process.wait(timeout=2)
            finally:
                for stream in (self._process.stdin, self._process.stdout):
                    if stream is not None:
                        stream.close()
                self._process = None
                self._connected_endpoint = None
                self._diagnoses.clear()


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
    terms = {term.lower() for term in re.findall(r"[A-Za-z0-9가-힣]+", query) if len(term) > 1}
    matches: list[tuple[int, dict[str, Any]]] = []
    errors: list[str] = []
    for path in sorted(MANUAL_DIR.glob("*")):
        if path.suffix.lower() not in {".md", ".pdf"} or not path.is_file():
            continue
        if path.stat().st_size > 20 * 1024 * 1024:
            errors.append(f"{path.name}: manual exceeds 20 MiB limit")
            continue
        try:
            content = path.read_bytes()
        except OSError as exception:
            errors.append(f"{path.name}: {type(exception).__name__}")
            continue
        version = hashlib.sha256(content).hexdigest()[:16]
        uri = f"devicelink://manual/{path.name}"
        chunks: list[tuple[str, int | None, str]] = []
        if path.suffix.lower() == ".md":
            try:
                sections = re.split(r"(?=^## )", content.decode("utf-8"), flags=re.MULTILINE)
            except UnicodeDecodeError:
                errors.append(f"{path.name}: Markdown must be UTF-8")
                continue
            for section in sections:
                section = section.strip()
                if section:
                    chunks.append((section.splitlines()[0].removeprefix("## "), None, section))
        else:
            try:
                from io import BytesIO
                from pypdf import PdfReader
                pages = PdfReader(BytesIO(content)).pages
                if len(pages) > 200:
                    errors.append(f"{path.name}: PDF exceeds 200-page limit")
                    continue
                for page_number, page in enumerate(pages, start=1):
                    page_text = page.extract_text() or ""
                    if page_text.strip():
                        chunks.append((f"Page {page_number}", page_number, page_text.strip()))
                if not chunks:
                    errors.append(f"{path.name}: no extractable text; OCR is required")
            except Exception as exception:
                errors.append(f"{path.name}: {exception}")
                continue
        for section_name, page_number, chunk in chunks:
            lowered = chunk.lower()
            score = sum(lowered.count(term) for term in terms)
            if score:
                first_match = min(lowered.find(term) for term in terms if term in lowered)
                excerpt_start = max(0, first_match - 150)
                matches.append((score, {"source": uri, "document_version": version,
                                        "section": section_name, "page": page_number,
                                        "excerpt": chunk[excerpt_start:excerpt_start + 900]}))
    matches.sort(key=lambda item: (-item[0], item[1]["source"], item[1]["page"] or 0))
    selected = [item[1] for item in matches[:limit]]
    for index, item in enumerate(selected, start=1):
        item["citation_id"] = f"E{index}"
    return {
        "ok": bool(matches),
        "message": "Manual evidence found" if matches else "No matching manual section found",
        "matches": selected,
        "errors": errors,
    }


def _bounded_integer(arguments: dict[str, Any], key: str, default: int, minimum: int, maximum: int) -> int:
    value = _integer(arguments, key, default)
    if not minimum <= value <= maximum:
        raise ValueError(f"{key} must be between {minimum} and {maximum}")
    return value


def _fresh_telemetry(bridge: Bridge, timeout_ms: int) -> dict[str, Any]:
    result = bridge.call(f"GET_TELEMETRY {timeout_ms}")
    observed_at = result.get("observed_at_unix_ms")
    if not result.get("ok") or not isinstance(result.get("telemetry"), dict) or not isinstance(observed_at, int):
        return {"ok": False, "message": result.get("message", "Telemetry unavailable"), "raw": result}
    age_ms = int(time.time() * 1000) - observed_at
    if age_ms < -1000 or age_ms > timeout_ms + 1000:
        return {"ok": False, "message": "Telemetry observation is stale", "age_ms": age_ms, "raw": result}
    return {"ok": True, "age_ms": age_ms, "raw": result}


def _diagnose_device(bridge: Bridge, timeout_ms: int, event_limit: int,
                     include_llm: bool = False) -> dict[str, Any]:
    return diagnose_device(bridge, timeout_ms, event_limit,
        fresh_telemetry=_fresh_telemetry, search_manual=_search_manual,
        explain=_langchain_explanation if include_llm else None)


def _verify_recovery(bridge: Bridge, timeout_ms: int) -> dict[str, Any]:
    observation = _fresh_telemetry(bridge, timeout_ms)
    if not observation["ok"]:
        return {"ok": True, "verdict": "INCONCLUSIVE", "message": observation["message"],
                "conditions": {"fresh_telemetry": False, "state_ready": False, "fault_none": False}}
    raw = observation["raw"]
    telemetry = raw["telemetry"]
    conditions = {"fresh_telemetry": True, "state_ready": telemetry.get("state") == "Ready",
                  "fault_none": telemetry.get("fault") == "None"}
    return {"ok": True, "verdict": "PASS" if all(conditions.values()) else "FAIL",
            "conditions": conditions, "device_id": raw.get("device_id"),
            "observed_at_unix_ms": raw["observed_at_unix_ms"], "telemetry": telemetry,
            "verification_scope": "current_state_only"}


def _verify_diagnosis(bridge: Bridge, diagnosis_id: str, timeout_ms: int) -> dict[str, Any]:
    baseline = bridge.diagnosis(diagnosis_id)
    if baseline is None:
        raise ValueError("Unknown diagnosis_id; no saved baseline is available")
    observation = _fresh_telemetry(bridge, timeout_ms)
    if not observation["ok"]:
        result = {"ok": True, "diagnosis_id": diagnosis_id, "verdict": "INCONCLUSIVE",
                "message": observation["message"], "baseline": baseline,
                "verification_scope": "observed_state_transition"}
        result["report"] = bridge.save_report(result, verification=True)
        return result
    raw = observation["raw"]
    telemetry = raw["telemetry"]
    baseline_time = baseline["observed_at_unix_ms"]
    conditions = {
        "same_device": raw.get("device_id") == baseline["device_id"],
        "new_observation": raw["observed_at_unix_ms"] > baseline_time,
        "state_ready": telemetry.get("state") == "Ready",
        "fault_none": telemetry.get("fault") == "None",
    }
    if not conditions["same_device"] or not conditions["new_observation"]:
        verdict = "INCONCLUSIVE"
    else:
        verdict = "PASS" if all(conditions.values()) else "FAIL"
    history = bridge.call(f"GET_HISTORY {max(0, baseline_time - 5000)} "
                          f"{raw['observed_at_unix_ms'] + 1000} 100")
    result = {"ok": True, "diagnosis_id": diagnosis_id, "verdict": verdict,
            "baseline": baseline, "verification": raw, "conditions": conditions,
            "events": history.get("events", []),
            "event_status": "available" if history.get("ok") else "unavailable",
            "verification_scope": "observed_state_transition"}
    result["report"] = bridge.save_report(result, verification=True)
    return result


def _execute_instruction(bridge: Bridge, arguments: dict[str, Any]) -> dict[str, Any]:
    include_llm = arguments.get('include_llm', False)
    if not isinstance(include_llm, bool):
        raise ValueError('include_llm must be a boolean')
    return execute_instruction(bridge, arguments, diagnose=lambda: _diagnose_device(
        bridge, 2000, 20, include_llm))


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
    if name == "get_recent_events":
        return bridge.call(f"GET_EVENTS {_bounded_integer(arguments, 'limit', 20, 1, 100)}")
    if name == "get_event_history":
        start = _bounded_integer(arguments, "from_unix_ms", 0, 0, 2**63 - 1)
        end = _bounded_integer(arguments, "through_unix_ms", 0, 0, 2**63 - 1)
        if end < start:
            raise ValueError("through_unix_ms must be at or after from_unix_ms")
        limit = _bounded_integer(arguments, "limit", 50, 1, 100)
        return bridge.call(f"GET_HISTORY {start} {end} {limit}")
    if name == "diagnose_device":
        include_llm = arguments.get("include_llm", False)
        if not isinstance(include_llm, bool):
            raise ValueError("include_llm must be a boolean")
        return _diagnose_device(bridge,
            _bounded_integer(arguments, "timeout_ms", 2000, 1, 30000),
            _bounded_integer(arguments, "event_limit", 20, 1, 100), include_llm)
    if name == "verify_recovery":
        return _verify_recovery(bridge, _bounded_integer(arguments, "timeout_ms", 2000, 1, 30000))
    if name == "verify_diagnosis":
        diagnosis_id = arguments.get("diagnosis_id")
        if not isinstance(diagnosis_id, str) or not diagnosis_id:
            raise ValueError("diagnosis_id is required")
        return _verify_diagnosis(bridge, diagnosis_id,
                                 _bounded_integer(arguments, "timeout_ms", 2000, 1, 30000))
    if name == "get_diagnosis_report":
        diagnosis_id = arguments.get("diagnosis_id")
        if not isinstance(diagnosis_id, str):
            raise ValueError("diagnosis_id is required")
        return bridge.report(diagnosis_id)
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
        return _search_manual(query, _bounded_integer(arguments, "limit", 3, 1, 5))
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
        except (ValueError, RuntimeError, OSError) as exception:
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
    # MCP stdio is UTF-8 even when Windows redirects pipes using a legacy locale.
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8")
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
