from __future__ import annotations

import json
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import MagicMock, patch

from mcp_server import demo_runtime


class FakeProcess:
    def __init__(self, pid: int) -> None:
        self.pid = pid
        self.exited = False

    def poll(self):
        return 0 if self.exited else None


class FakePlatform:
    def __init__(self) -> None:
        self.ports = {5000: True, 11434: True}
        self.launched = []
        self.closed = []
        self.model_available = True
        self.mfc_connected = True

    def port_open(self, port: int) -> bool:
        return self.ports[port]

    def http_json(self, url, body=None, timeout=3):
        if not self.model_available:
            raise OSError('fixture model server unavailable')
        return {'models': [{'name': 'test-local-model'}]}

    def spawn(self, args, root, log, environment, hidden):
        process = FakeProcess(1000 + len(self.launched))
        self.launched.append((args, process, environment))
        if args[0].endswith('unreal.exe'):
            self.ports[5000] = True
        if '--demo-hidden' in args or '--demo' in args:
            (root / 'artifacts' / 'demo' / 'mfc-ready.json').write_text(json.dumps({
                'pid': process.pid, 'connected': self.mfc_connected}), encoding='utf-8')
        return process

    def close_process(self, process):
        self.closed.append(process.pid)
        process.exited = True


class DemoLauncherTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1])
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.platform = FakePlatform()
        self.session = demo_runtime.DemoSession(self.root, platform=self.platform)
        self.configuration = {'model': 'test-local-model', 'paths': {
            name: self.root / (name + '.exe') for name in ('mfc', 'bridge', 'unreal', 'project', 'ollama')}}

    def test_reuses_servers_and_stops_only_owned_mfc(self) -> None:
        bridge = MagicMock()
        with patch.object(self.session, 'preflight', return_value=self.configuration), \
             patch.object(demo_runtime.server, 'Bridge', return_value=bridge), \
             patch.object(demo_runtime.server, 'call_tool', return_value={
                 'ok': True, 'telemetry': {'state': 'Ready', 'fault': 'None'}}):
            result = self.session.start(hidden=True)
            self.assertTrue(result['ok'])
            self.assertEqual(len(self.platform.launched), 1)
            self.assertIn('--demo-hidden', self.platform.launched[0][0])
            self.assertNotIn('OPENAI_API_KEY', self.platform.launched[0][2])
            self.assertTrue(self.session.start()['already_ready'])
            self.assertEqual(len(self.platform.launched), 1)
            self.session.stop()
            bridge.close.assert_called_once()
        self.assertEqual(self.platform.closed, [1000])
        self.assertFalse(self.session.ready)

    def test_mfc_startup_failure_rolls_back_owned_process(self) -> None:
        self.platform.mfc_connected = False

        def immediate_wait(predicate, seconds, process=None):
            if not predicate():
                raise RuntimeError('fixture readiness failure')

        with patch.object(self.session, 'preflight', return_value=self.configuration), \
             patch.object(self.session, '_wait', side_effect=immediate_wait), \
             patch.object(demo_runtime.server, 'Bridge'), \
             patch.object(demo_runtime.server, 'call_tool', return_value={'ok': True}):
            with self.assertRaisesRegex(RuntimeError, 'readiness'):
                self.session.start(hidden=True)
        self.assertEqual(self.platform.closed, [1000])
        self.assertFalse(self.session.ready)

    def test_background_and_visual_device_modes_keep_mfc_visible(self) -> None:
        for visual in (False, True):
            with self.subTest(visual=visual):
                self.platform.ports[5000] = False
                self.platform.launched.clear()
                with patch.object(self.session, 'preflight', return_value=self.configuration), \
                     patch.object(demo_runtime.server, 'Bridge'), \
                     patch.object(demo_runtime.server, 'call_tool', return_value={'ok': True}):
                    self.session.start(visual_device=visual)
                    device_args = self.platform.launched[0][0]
                    self.assertEqual('-nullrhi' in device_args, not visual)
                    self.assertEqual(any(arg.startswith('-ExecCmds=t.MaxFPS 30') for arg in device_args), visual)
                    self.assertEqual('-ini:Engine:[SystemSettings]:r.RayTracing=0' in device_args, visual)
                    self.assertIn('--demo', self.platform.launched[1][0])
                    self.session.stop()

    def test_startup_waits_for_first_telemetry_before_power_on(self) -> None:
        calls = []
        attempts = 0

        def call(bridge, name, arguments):
            nonlocal attempts
            calls.append(name)
            if name == 'get_telemetry':
                attempts += 1
                if attempts < 3:
                    return {'ok': False, 'message': 'Telemetry response timed out'}
            return {'ok': True}

        with patch.object(self.session, 'preflight', return_value=self.configuration), \
             patch.object(demo_runtime.server, 'Bridge'), \
             patch.object(demo_runtime.server, 'call_tool', side_effect=call):
            self.session.start(visual_device=True)
            self.assertEqual(attempts, 3)
            self.assertGreater(calls.index('power_on'), max(
                index for index, name in enumerate(calls) if name == 'get_telemetry'))
            self.session.stop()

    def test_busy_model_port_does_not_spawn_duplicate_server(self) -> None:
        self.platform.model_available = False
        with patch.object(self.session, 'preflight', return_value=self.configuration):
            with self.assertRaisesRegex(RuntimeError, '11434'):
                self.session.start(hidden=True)
        self.assertEqual(self.platform.launched, [])

    def test_cancel_stops_readiness_wait(self) -> None:
        self.session.cancel.set()
        with self.assertRaisesRegex(RuntimeError, '취소'):
            self.session._wait(lambda: False, 180)

    def test_llm_failure_still_clears_injected_fault(self) -> None:
        self.session.ready = True
        self.session.bridge = MagicMock()
        calls = []

        def call(bridge, name, arguments):
            calls.append(name)
            if name == 'diagnose_device':
                return {'ok': True, 'llm_explanation': {'status': 'unavailable'}}
            return {'ok': True}

        with patch.object(demo_runtime.server, 'call_tool', side_effect=call):
            with self.assertRaisesRegex(RuntimeError, 'LLM'):
                self.session.fault_demo()
        self.assertEqual(calls, ['inject_fault', 'diagnose_device', 'clear_fault'])

    def test_successful_fault_demo_requires_fail_then_pass(self) -> None:
        self.session.ready = True
        self.session.bridge = MagicMock()
        fault = False

        def call(bridge, name, arguments):
            nonlocal fault
            if name == 'inject_fault':
                fault = True
            if name == 'clear_fault':
                fault = False
            if name == 'diagnose_device':
                return {'ok': True, 'diagnosis_id': 'a' * 32,
                        'llm_explanation': {'status': 'generated'}}
            if name == 'verify_diagnosis':
                return {'ok': True, 'verdict': 'FAIL' if fault else 'PASS', 'report': {'status': 'saved'}}
            return {'ok': True}

        with patch.object(demo_runtime.server, 'call_tool', side_effect=call):
            result = self.session.fault_demo()
        self.assertEqual((result['before'], result['after']), ('FAIL', 'PASS'))

    @unittest.skipUnless(os.name == 'nt', 'Windows job ownership test')
    def test_owned_process_job_closes_descendant_process(self) -> None:
        platform = demo_runtime.DemoPlatform()
        marker = self.root / 'child.pid'
        script = ('import subprocess,sys,time; from pathlib import Path; '
                  'p=subprocess.Popen([sys.executable,"-c","import time; time.sleep(60)"]); '
                  f'Path({str(marker)!r}).write_text(str(p.pid)); time.sleep(60)')
        process = platform.spawn([sys.executable, '-c', script], self.root,
                                 self.root / 'test.log', dict(os.environ), True)
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        child = None
        try:
            deadline = time.monotonic() + 8
            while not marker.exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertTrue(marker.exists(), 'Child process did not start')
            child = kernel.OpenProcess(0x00100000, False, int(marker.read_text()))
            self.assertTrue(child)
            platform.close_process(process)
            self.assertEqual(kernel.WaitForSingleObject(child, 3000), 0,
                             'A launcher-owned descendant was left running')
        finally:
            platform.close_process(process)
            if child:
                kernel.CloseHandle(child)


if __name__ == '__main__':
    unittest.main()
