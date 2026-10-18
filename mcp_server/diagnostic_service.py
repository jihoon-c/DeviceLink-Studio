"""Diagnostic orchestration shared by adapters; contains no MCP or UI dependencies."""

from __future__ import annotations

import time
from typing import Any, Callable


def diagnose_device(bridge: Any, timeout_ms: int, event_limit: int, *,
                    fresh_telemetry: Callable, search_manual: Callable,
                    explain: Callable | None = None) -> dict[str, Any]:
    observation = fresh_telemetry(bridge, timeout_ms)
    window_end = int(time.time() * 1000)
    if observation["ok"]:
        window_end = observation["raw"]["observed_at_unix_ms"] + 1000
    window_start = window_end - 60000
    events = bridge.call(f"GET_HISTORY {window_start} {window_end} {event_limit}")
    event_status = "available" if events.get("ok") else "unavailable"
    if not observation["ok"]:
        return {"ok": True, "verdict": "INCONCLUSIVE", "message": observation["message"],
                "observation": observation.get("raw"), "events": events.get("events", []),
                "event_status": event_status, "event_window": [window_start, window_end],
                "evidence_refs": []}
    raw = observation["raw"]
    telemetry = raw["telemetry"]
    fault = telemetry.get("fault")
    state = telemetry.get("state")
    known_faults = {"MotorStall", "OverTemperature", "SensorFailure"}
    if state == "Fault" and fault in known_faults:
        manual = search_manual(fault, 3)
        hypothesis = fault
        message = f"Observed {fault} in fresh telemetry; consult cited manual sections before recovery."
    elif state == "Ready" and fault == "None":
        manual = {"matches": []}
        hypothesis = None
        message = "Fresh telemetry reports Ready with no active fault."
    else:
        manual = search_manual("telemetry recovery", 3)
        hypothesis = None
        message = "Current state does not identify a supported fault."
    diagnosis_id = bridge.record_diagnosis(raw.get("device_id", ""),
        raw["observed_at_unix_ms"], telemetry) if hypothesis else None
    result = {"ok": True, "verdict": "OBSERVED" if hypothesis or (state == "Ready" and fault == "None") else "INCONCLUSIVE",
            "diagnosis_id": diagnosis_id,
            "message": message, "device_id": raw.get("device_id"),
            "observed_at_unix_ms": raw["observed_at_unix_ms"], "telemetry_age_ms": observation["age_ms"],
            "telemetry": telemetry, "events": events.get("events", []),
            "event_status": event_status,
            "event_window": [window_start, window_end],
            "hypothesis": hypothesis, "evidence_refs": manual["matches"],
            "diagnosis_mode": "rule_based",
            "llm_explanation": explain(telemetry, events.get("events", []), manual["matches"])
                if explain is not None else {"status": "skipped_disabled"},
            "recommended_check": "Use verify_recovery after an explicitly requested recovery action" if hypothesis else None,
            "actions_taken": []}
    if diagnosis_id:
        result["report"] = bridge.save_report(result)
    return result
