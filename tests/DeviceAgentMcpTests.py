from __future__ import annotations

import importlib.util
import json
import socket
from pathlib import Path
import struct
import subprocess
import sys
import threading
import unittest


ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "build-agent" / "DeviceLinkAgentBridge.exe"


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
                        if message_type == 0x1003:
                            self.pan, self.tilt = struct.unpack(">hh", payload)
                        elif message_type == 0x1007:
                            self.fault = payload[0]
                        acknowledgement = struct.pack(">HB", message_type, 0)
                        connection.sendall(frame(0x6001, sequence, acknowledgement))
                        if message_type in {0x1003, 0x1006, 0x1007}:
                            state = 4 if self.fault else 1
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
        process = subprocess.Popen(
            [str(BRIDGE)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, encoding="utf-8", bufsize=1,
        )
        assert process.stdin is not None and process.stdout is not None

        def call(command: str) -> dict[str, object]:
            process.stdin.write(command + "\n")
            process.stdin.flush()
            return json.loads(process.stdout.readline())

        try:
            self.assertTrue(call(f"CONNECT 127.0.0.1 {device.port}")["ok"])
            angle = call("SET_ANGLE 30 -10 2000 0.1")
            self.assertTrue(angle["ok"])
            self.assertEqual(angle["telemetry"]["pan_degrees"], 30)
            self.assertEqual(angle["telemetry"]["tilt_degrees"], -10)
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
            call("QUIT")
            process.wait(timeout=3)
            process.stdin.close()
            process.stdout.close()
        finally:
            if process.poll() is None:
                process.terminate()
        device.join(timeout=2)
        if device.failure:
            raise device.failure


class McpServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        spec = importlib.util.spec_from_file_location("devicelink_mcp", ROOT / "mcp_server" / "server.py")
        assert spec and spec.loader
        cls.server = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.server)

    def test_lists_six_device_tools_and_manual_search(self) -> None:
        response = self.server.handle(None, {"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
        names = {tool["name"] for tool in response["result"]["tools"]}
        self.assertTrue({
            "connect_device", "set_gimbal_angle", "get_telemetry",
            "inject_fault", "clear_fault", "run_test_scenario", "search_manual",
        }.issubset(names))
        result = self.server.call_tool(None, "search_manual", {"query": "Motor Stall recovery", "limit": 2})
        self.assertTrue(result["ok"])
        self.assertIn("MotorStall", result["matches"][0]["excerpt"])


if __name__ == "__main__":
    unittest.main()
