from __future__ import annotations

import importlib.util
import json
import os
import socket
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import types
import unittest
import time
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
BRIDGE = Path(os.environ.get("DEVICELINK_AGENT_BRIDGE", ROOT / "build-agent-diag" / "DeviceLinkAgentBridge.exe"))


def crc16(data: bytes) -> int:
    value = 0xFFFF
    for byte in data:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ 0x1021) & 0xFFFF if value & 0x8000 else (value << 1) & 0xFFFF
    return value


def frame(message_type: int, sequence: int, payload: bytes = b"") -> bytes:
    header = struct.pack(">HBBHII", 0xD15C, 1, 0, message_type, sequence, len(payload))
    body = header + payload
    return body + struct.pack(">H", crc16(body))


class VirtualDevice(threading.Thread):
    def __init__(self) -> None:
        super().__init__(daemon=True)
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.port = self.listener.getsockname()[1]
        self.pan = 0
        self.tilt = 0
        self.fault = 0
        self.state = 0
        self.failure: BaseException | None = None

    def run(self) -> None:
        try:
            connection, _ = self.listener.accept()
            with connection:
                buffer = b""
                while True:
                    chunk = connection.recv(4096)
                    if not chunk:
                        return
                    buffer += chunk
                    while len(buffer) >= 16:
                        payload_size = struct.unpack_from(">I", buffer, 10)[0]
                        size = 14 + payload_size + 2
                        if len(buffer) < size:
                            break
                        packet, buffer = buffer[:size], buffer[size:]
                        message_type, sequence = struct.unpack_from(">HI", packet, 4)
                        payload = packet[14:-2]
                        if message_type == 0x1001:
                            self.state = 1 if payload[0] else 0
                        elif message_type == 0x1002:
                            self.pan = 0
                            self.tilt = 0
                            self.fault = 0
                            self.state = 1
                        elif message_type == 0x1003:
                            self.pan, self.tilt = struct.unpack(">hh", payload)
                        elif message_type == 0x1007:
                            self.fault = payload[0]
                        acknowledgement = struct.pack(">HB", message_type, 0)
                        connection.sendall(frame(0x6001, sequence, acknowledgement))
                        if message_type in {0x1001, 0x1002, 0x1003, 0x1006, 0x1007}:
                            state = 4 if self.fault else self.state
                            telemetry = struct.pack(
                                ">BBhhhhhH",
                                state,
                                self.fault,
                                self.pan,
                                self.tilt,
                                self.pan,
                                self.tilt,
                                2500,
                                24000,
                            )
                            connection.sendall(frame(0x7001, sequence, telemetry))
        except BaseException as exception:  # surfaced by the test thread
            self.failure = exception
        finally:
            self.listener.close()


class DeviceAgentBridgeTests(unittest.TestCase):
    def test_native_bridge_runs_angle_fault_and_recovery_workflow(self) -> None:
        self.assertTrue(BRIDGE.is_file(), f"Build bridge first: {BRIDGE}")
        device = VirtualDevice()
        device.start()
        database = tempfile.TemporaryDirectory(dir=ROOT)
        environment = os.environ.copy()
        environment["DEVICELINK_AGENT_DB"] = str(Path(database.name) / "agent-events.sqlite")
        process = subprocess.Popen(
            [str(BRIDGE)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, encoding="utf-8", bufsize=1, env=environment,
        )
        assert process.stdin is not None and process.stdout is not None

        def call(command: str) -> dict[str, object]:
            process.stdin.write(command + "\n")
            process.stdin.flush()
            return json.loads(process.stdout.readline())

        try:
            connected = call(f"CONNECT 127.0.0.1 {device.port}")
            self.assertTrue(connected["ok"], (connected, call("GET_EVENTS 10")))
            self.assertTrue(call("POWER_ON 2000")["ok"])
            self.assertTrue(call("INITIALIZE 2000")["ok"])
            angle = call("SET_ANGLE 30 -10 2000 0.1")
            self.assertTrue(angle["ok"])
            self.assertEqual(angle["telemetry"]["pan_degrees"], 30)
            self.assertEqual(angle["telemetry"]["tilt_degrees"], -10)
            observed = call("GET_TELEMETRY 2000")
            self.assertEqual(observed["device_id"], "unreal-gimbal-01")
            self.assertGreater(observed["observed_at_unix_ms"], 0)
            fault = call("INJECT_FAULT MotorStall 2000")
            self.assertTrue(fault["ok"])
            self.assertEqual(fault["telemetry"]["state"], "Fault")
            self.assertEqual(fault["telemetry"]["fault"], "MotorStall")
            cleared = call("CLEAR_FAULT 2000")
            self.assertTrue(cleared["ok"])
            self.assertEqual(cleared["telemetry"]["state"], "Ready")
            scenario = call("RUN_SCENARIO motor_stall_recovery 2000")
            self.assertTrue(scenario["ok"])
            self.assertEqual(len(scenario["steps"]), 4)
            events = call("GET_EVENTS 10")
            self.assertTrue(events["ok"])
            self.assertEqual(events["device_id"], "unreal-gimbal-01")
            self.assertTrue(any(event["category"] == "frame-received" for event in events["events"]))
            history = call(f"GET_HISTORY 0 {int(time.time() * 1000) + 1000} 100")
            self.assertTrue(history["ok"])
            self.assertTrue(any(event["category"] == "frame-received" for event in history["events"]))
            call("QUIT")
            process.wait(timeout=3)
            process.stdin.close()
            process.stdout.close()
            process = subprocess.Popen(
                [str(BRIDGE)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                text=True, encoding="utf-8", bufsize=1, env=environment,
            )
            assert process.stdin is not None and process.stdout is not None
            persisted = call(f"GET_HISTORY 0 {int(time.time() * 1000) + 1000} 100")
            self.assertTrue(persisted["ok"])
            self.assertTrue(any(event["category"] == "frame-received" for event in persisted["events"]))
            call("QUIT")
        finally:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=3)
            process.stdin.close()
            process.stdout.close()
            device.listener.close()
            database.cleanup()
        device.join(timeout=2)
        if device.failure:
            raise device.failure


class McpServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        spec = importlib.util.spec_from_file_location("mcp_server.server", ROOT / "mcp_server" / "server.py")
        assert spec and spec.loader
        cls.server = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.server)

    def setUp(self) -> None:
        # Unit tests never call a configured real model.
        self.environment = patch.dict(os.environ, {"DEVICELINK_LLM_MODEL": "",
                                                   "DEVICELINK_LLM_PROVIDER": "openai",
                                                   "OPENAI_API_KEY": "unit-test-placeholder"})
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def test_lists_device_tools_natural_language_and_manual_search(self) -> None:
        response = self.server.handle(None, {"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
        names = {tool["name"] for tool in response["result"]["tools"]}
        self.assertTrue({
            "connect_device", "set_gimbal_angle", "get_telemetry",
            "inject_fault", "clear_fault", "run_test_scenario", "search_manual",
            "power_on", "power_off", "initialize_device", "execute_instruction",
            "get_recent_events", "get_event_history", "diagnose_device", "verify_recovery",
            "verify_diagnosis",
            "get_diagnosis_report",
        }.issubset(names))
        result = self.server.call_tool(None, "search_manual", {"query": "Motor Stall recovery", "limit": 2})
        self.assertTrue(result["ok"])
        self.assertIn("MotorStall", result["matches"][0]["excerpt"])

    def test_diagnosis_uses_fresh_observation_and_manual_evidence_without_actuation(self) -> None:
        class FakeBridge:
            def __init__(self) -> None:
                self.commands: list[str] = []

            def record_diagnosis(self, device_id: str, observed_at: int,
                                 telemetry: dict[str, object]) -> str:
                return "diagnosis-1"

            def save_report(self, result: dict[str, object], **kwargs: object) -> dict[str, str]:
                return {"status": "not_saved_test_double"}

            def call(self, command: str) -> dict[str, object]:
                self.commands.append(command)
                if command.startswith("GET_TELEMETRY"):
                    return {"ok": True, "device_id": "unreal-gimbal-01",
                            "observed_at_unix_ms": int(time.time() * 1000),
                            "telemetry": {"state": "Fault", "fault": "MotorStall"}}
                return {"ok": True, "events": [{"category": "frame-received", "detail": "type=28673"}]}

        bridge = FakeBridge()
        with patch.object(self.server, '_langchain_explanation', side_effect=AssertionError('LLM must remain optional')) as model:
            result = self.server.call_tool(bridge, "diagnose_device", {})
            model.assert_not_called()
        self.assertEqual(result["verdict"], "OBSERVED")
        self.assertEqual(result["hypothesis"], "MotorStall")
        self.assertEqual(result["diagnosis_id"], "diagnosis-1")
        self.assertTrue(result["evidence_refs"])
        self.assertEqual(result['llm_explanation']['status'], 'skipped_disabled')
        self.assertEqual(bridge.commands[0], "GET_TELEMETRY 2000")
        self.assertTrue(bridge.commands[1].startswith("GET_HISTORY "))

    def test_diagnosis_rejects_non_boolean_llm_option_before_device_access(self) -> None:
        for value in ('false', 1, None, {}):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'include_llm'):
                self.server.call_tool(None, 'diagnose_device', {'include_llm': value})

    def test_history_rejects_reversed_window(self) -> None:
        with self.assertRaisesRegex(ValueError, "through_unix_ms"):
            self.server.call_tool(None, "get_event_history", {
                "from_unix_ms": 2000, "through_unix_ms": 1000,
            })

    def test_manual_search_reports_document_version_section_and_pdf_page(self) -> None:
        from pypdf import PdfWriter
        from pypdf.generic import DecodedStreamObject, DictionaryObject, NameObject
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            manual_directory = Path(directory)
            (manual_directory / "manual.md").write_text(
                "# Manual\n\n## SensorFailure\nCheck the sensor path before recovery.\n", encoding="utf-8")
            pdf_path = manual_directory / "faults.pdf"
            document = PdfWriter()
            page = document.add_blank_page(width=612, height=792)
            font = DictionaryObject({NameObject('/Type'): NameObject('/Font'),
                                     NameObject('/Subtype'): NameObject('/Type1'),
                                     NameObject('/BaseFont'): NameObject('/Helvetica')})
            page[NameObject('/Resources')] = DictionaryObject({
                NameObject('/Font'): DictionaryObject({NameObject('/F1'): font})})
            stream = DecodedStreamObject()
            stream.set_data(b'BT /F1 12 Tf 40 750 Td (MotorStall recovery procedure) Tj ET')
            page[NameObject('/Contents')] = stream
            document.write(pdf_path)
            with patch.object(self.server, "MANUAL_DIR", manual_directory):
                markdown = self.server.call_tool(None, "search_manual", {"query": "SensorFailure"})
                pdf = self.server.call_tool(None, "search_manual", {"query": "MotorStall"})
            self.assertEqual(markdown["matches"][0]["section"], "SensorFailure")
            self.assertEqual(len(markdown["matches"][0]["document_version"]), 16)
            self.assertEqual(pdf["matches"][0]["page"], 1)
            self.assertIn("MotorStall", pdf["matches"][0]["excerpt"])

    def test_langchain_explanation_keeps_manual_citations_and_cannot_set_verdict(self) -> None:
        class FakeChatOpenAI:
            def __init__(self, **kwargs: object) -> None:
                self.model = kwargs["model"]

            def with_structured_output(self, schema: dict[str, object], **kwargs: object) -> object:
                self_test.assertEqual(kwargs, {"method": "json_schema", "strict": True, "include_raw": True})
                self_test.assertEqual(schema["properties"]["citation_ids"]["items"]["enum"], ["E1"])
                return self

            def invoke(self, messages: list[tuple[str, str]]) -> object:
                self_test.assertIn("E1", messages[1][1])
                return {"parsed": {
                    "possible_cause": "MotorStall",
                    "recommended_checks": ["Read fresh telemetry"],
                    "recovery_steps": ["Clear fault after authorization"],
                    "citation_ids": ["E1"],
                    "verdict": "PASS",
                }}

        self_test = self
        fake_module = types.ModuleType("langchain_openai")
        fake_module.ChatOpenAI = FakeChatOpenAI
        evidence = [{"citation_id": "E1", "source": "devicelink://manual/fault-codes.md",
                     "excerpt": "MotorStall means a simulated motor fault"}]
        with patch.dict(os.environ, {"DEVICELINK_LLM_MODEL": "test-model"}), \
             patch.dict(sys.modules, {"langchain_openai": fake_module}):
            result = self.server._langchain_explanation(
                {"state": "Fault", "fault": "MotorStall"}, [], evidence)
        self.assertEqual(result["status"], "generated")
        self.assertEqual(result["proposal"]["citation_ids"], ["E1"])
        self.assertNotIn("verdict", result["proposal"])

    def test_langchain_explanation_rejects_unknown_citation(self) -> None:
        class FakeChatOpenAI:
            def __init__(self, **kwargs: object) -> None:
                pass

            def with_structured_output(self, *args: object, **kwargs: object) -> object:
                return self

            def invoke(self, messages: list[tuple[str, str]]) -> object:
                return {"parsed": {
                    "possible_cause": "unknown", "recommended_checks": [],
                    "recovery_steps": [], "citation_ids": ["E999"],
                }}

        fake_module = types.ModuleType("langchain_openai")
        fake_module.ChatOpenAI = FakeChatOpenAI
        with patch.dict(os.environ, {"DEVICELINK_LLM_MODEL": "test-model"}), \
             patch.dict(sys.modules, {"langchain_openai": fake_module}):
            result = self.server._langchain_explanation({}, [], [{"citation_id": "E1"}])
        self.assertEqual(result["status"], "unavailable")

    def test_recovery_verdict_requires_fresh_matching_telemetry(self) -> None:
        class FakeBridge:
            def __init__(self, state: str, fault: str, observed_at: int) -> None:
                self.state, self.fault, self.observed_at = state, fault, observed_at

            def call(self, command: str) -> dict[str, object]:
                return {"ok": True, "observed_at_unix_ms": self.observed_at,
                        "telemetry": {"state": self.state, "fault": self.fault}}

        now = int(time.time() * 1000)
        for state, fault, observed_at, expected in [
            ("Ready", "None", now, "PASS"),
            ("Fault", "MotorStall", now, "FAIL"),
            ("Ready", "None", now - 10000, "INCONCLUSIVE"),
        ]:
            with self.subTest(expected=expected):
                result = self.server.call_tool(FakeBridge(state, fault, observed_at), "verify_recovery", {})
                self.assertEqual(result["verdict"], expected)

    def test_verify_diagnosis_requires_later_ready_telemetry_for_same_device(self) -> None:
        class FakeBridge:
            def __init__(self, observed_at: int, device_id: str) -> None:
                self.observed_at = observed_at
                self.device_id = device_id

            def save_report(self, result: dict[str, object], **kwargs: object) -> dict[str, str]:
                return {"status": "not_saved_test_double"}

            def diagnosis(self, diagnosis_id: str) -> dict[str, object] | None:
                return {"device_id": "unreal-gimbal-01", "observed_at_unix_ms": 1000,
                        "telemetry": {"state": "Fault", "fault": "MotorStall"}}

            def call(self, command: str) -> dict[str, object]:
                if command.startswith("GET_TELEMETRY"):
                    return {"ok": True, "device_id": self.device_id,
                            "observed_at_unix_ms": self.observed_at,
                            "telemetry": {"state": "Ready", "fault": "None"}}
                return {"ok": True, "events": []}

        now = int(time.time() * 1000)
        for observed_at, device_id, expected in [
            (now, "unreal-gimbal-01", "PASS"),
            (1000, "unreal-gimbal-01", "INCONCLUSIVE"),
            (now, "other-device", "INCONCLUSIVE"),
        ]:
            with self.subTest(expected=expected):
                result = self.server.call_tool(
                    FakeBridge(observed_at, device_id), "verify_diagnosis",
                    {"diagnosis_id": "diagnosis-1"})
                self.assertEqual(result["verdict"], expected)

    def test_korean_natural_language_angle_command_auto_prepares_device(self) -> None:
        class FakeBridge:
            connected_endpoint = None

            def __init__(self) -> None:
                self.commands: list[str] = []

            def call(self, command: str) -> dict[str, object]:
                self.commands.append(command)
                if command.startswith("CONNECT "):
                    self.connected_endpoint = ("127.0.0.1", 5000)
                result: dict[str, object] = {"ok": True, "message": "ok"}
                if command.startswith("SET_ANGLE "):
                    result["telemetry"] = {"pan_degrees": 30, "tilt_degrees": -10, "state": "Ready"}
                return result

        bridge = FakeBridge()
        result = self.server.call_tool(
            bridge,
            "execute_instruction",
            {"instruction": "Pan 30도, Tilt -10도로 이동해줘"},
        )
        self.assertTrue(result["ok"])
        self.assertEqual(bridge.commands, [
            "CONNECT 127.0.0.1 5000",
            "POWER_ON 3000",
            "INITIALIZE 5000",
            "SET_ANGLE 30 -10 5000 0.5",
        ])
        self.assertEqual(result["telemetry"]["tilt_degrees"], -10)

    def test_instruction_preview_never_accesses_device(self) -> None:
        result = self.server.call_tool(None, 'execute_instruction', {
            'instruction': 'Pan 30도 Tilt -10도 이동', 'preview_only': True})
        self.assertTrue(result['ok'])
        self.assertEqual(result['commands'], [
            'POWER_ON 3000', 'INITIALIZE 5000', 'SET_ANGLE 30 -10 5000 0.5'])
        self.assertEqual(result['actions_taken'], [])

    def test_negated_ambiguous_and_conflicting_requests_never_access_device(self) -> None:
        for instruction in ('고장 해제하지 말고 상태 확인', '전원 끄지마',
                            "don't power off", 'no power off',
                            '연결 해제', 'MotorStall 주입하고 고장 해제',
                            'MotorStall SensorFailure 주입',
                            '전원 off Pan 30 Tilt 10', '진단하고 전원 켜줘'):
            with self.subTest(instruction=instruction):
                result = self.server.call_tool(None, 'execute_instruction', {'instruction': instruction})
                self.assertFalse(result['ok'])

    def test_duplicate_and_nonfinite_angles_never_access_device(self) -> None:
        for instruction in ('Pan 30 Pan 40 Tilt 10', 'Pan 1e309 Tilt 0', 'Pan 1e3 Tilt 0'):
            with self.subTest(instruction=instruction), self.assertRaises(ValueError):
                self.server.call_tool(None, 'execute_instruction', {'instruction': instruction})

    def test_recovery_question_routes_to_read_only_diagnosis(self) -> None:
        with patch.object(self.server, '_diagnose_device', return_value={'ok': True}) as diagnose:
            result = self.server.call_tool(None, 'execute_instruction', {'instruction': '고장 복구 방법 알려줘'})
            self.assertTrue(result['ok'])
            diagnose.assert_called_once_with(None, 2000, 20, False)

    def test_natural_language_rejects_incomplete_angle(self) -> None:
        class FakeBridge:
            connected_endpoint = None

        with self.assertRaisesRegex(ValueError, "Both Pan and Tilt"):
            self.server.call_tool(
                FakeBridge(), "execute_instruction", {"instruction": "Pan 30도로 이동"})

    def test_natural_language_reconnects_when_requested_endpoint_changes(self) -> None:
        class FakeBridge:
            connected_endpoint = ("10.0.0.1", 5000)

            def __init__(self) -> None:
                self.commands: list[str] = []
                self.closed = False

            def close(self) -> None:
                self.closed = True
                self.connected_endpoint = None

            def call(self, command: str) -> dict[str, object]:
                self.commands.append(command)
                if command.startswith("CONNECT "):
                    self.connected_endpoint = ("127.0.0.1", 5000)
                return {"ok": True, "message": "ok"}

        bridge = FakeBridge()
        result = self.server.call_tool(
            bridge,
            "execute_instruction",
            {
                "instruction": "현재 Telemetry 상태 확인해줘",
                "host": "127.0.0.1",
                "port": 5000,
            },
        )
        self.assertTrue(result["ok"])
        self.assertTrue(bridge.closed)
        self.assertEqual(bridge.commands, [
            "CONNECT 127.0.0.1 5000",
            "GET_TELEMETRY 2000",
        ])


if __name__ == "__main__":
    unittest.main()
