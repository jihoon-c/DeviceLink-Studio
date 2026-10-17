"""Validate the real local LLM through MCP stdio against an already running Unreal device."""

from __future__ import annotations

import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time

from mcp_server.llm_diagnostics import settings


class McpSession:
    def __init__(self, root: Path, destination: Path) -> None:
        self._responses: queue.Queue[str] = queue.Queue()
        self._request_id = 0
        self._error_log = (destination / 'mcp.stderr.log').open('a', encoding='utf-8')
        environment = dict(os.environ,
                           DEVICELINK_DIAGNOSTIC_REPORT_DIR=str(destination),
                           DEVICELINK_AGENT_DB=str(destination / 'events.sqlite'))
        self._process = subprocess.Popen([sys.executable, str(root / 'mcp_server' / 'server.py')],
                                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         stderr=self._error_log, text=True, encoding='utf-8',
                                         env=environment, cwd=root, bufsize=1)
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()

    def _read(self) -> None:
        assert self._process.stdout is not None
        for line in self._process.stdout:
            self._responses.put(line)

    def request(self, method: str, params: dict[str, object] | None = None) -> dict[str, object]:
        self._request_id += 1
        request = {'jsonrpc': '2.0', 'id': self._request_id, 'method': method, 'params': params or {}}
        assert self._process.stdin is not None
        self._process.stdin.write(json.dumps(request, ensure_ascii=False) + '\n')
        self._process.stdin.flush()
        response = json.loads(self._responses.get(timeout=45))
        if response.get('id') != self._request_id or 'error' in response:
            raise RuntimeError('Unexpected MCP response')
        return response['result']

    def initialize(self) -> None:
        self.request('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                                     'clientInfo': {'name': 'unreal-llm-validation', 'version': '1'}})
        assert self._process.stdin is not None
        self._process.stdin.write(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}) + '\n')
        self._process.stdin.flush()

    def tool(self, name: str, arguments: dict[str, object] | None = None) -> dict[str, object]:
        response = self.request('tools/call', {'name': name, 'arguments': arguments or {}})
        data = response['structuredContent']
        if response.get('isError') or not data.get('ok'):
            raise RuntimeError(f'{name} failed: {data}')
        return data

    def close(self) -> None:
        try:
            assert self._process.stdin is not None
            self._process.stdin.close()
            self._process.wait(timeout=5)
        except (BrokenPipeError, subprocess.TimeoutExpired):
            self._process.terminate()
            self._process.wait(timeout=3)
        finally:
            self._reader.join(timeout=2)
            if self._process.stdout is not None:
                self._process.stdout.close()
            self._error_log.close()


def run() -> dict[str, object]:
    config = settings()
    if config['provider'] != 'local' or not config['model']:
        raise RuntimeError('Configure a local model first')
    root = Path(__file__).resolve().parents[1]
    destination = root / 'artifacts' / 'diagnostics' / 'unreal'
    destination.mkdir(parents=True, exist_ok=True)
    session = McpSession(root, destination)
    cases = []
    try:
        session.initialize()
        names = [item['name'] for item in session.request('tools/list')['tools']]
        if 'get_diagnosis_report' not in names:
            raise RuntimeError('New diagnostic report tool is missing')
        session.tool('connect_device', {'host': '127.0.0.1', 'port': 5000})
        session.tool('power_on')
        session.tool('initialize_device')
        angle = session.tool('set_gimbal_angle', {'pan_degrees': 30, 'tilt_degrees': -10})
        for fault in ('MotorStall', 'OverTemperature', 'SensorFailure'):
            session.tool('inject_fault', {'fault': fault})
            started = time.monotonic()
            diagnosis = session.tool('diagnose_device')
            elapsed = round(time.monotonic() - started, 2)
            explanation = diagnosis.get('llm_explanation', {})
            if explanation.get('status') != 'generated' or explanation.get('source_validation') != 'verbatim_manual_procedures':
                raise RuntimeError(f'Local model proposal failed validation: {explanation}')
            identifier = diagnosis['diagnosis_id']
            before = session.tool('verify_diagnosis', {'diagnosis_id': identifier})
            session.tool('clear_fault')
            after = session.tool('verify_diagnosis', {'diagnosis_id': identifier})
            if before['verdict'] != 'FAIL' or after['verdict'] != 'PASS' or after['report']['status'] != 'saved':
                raise RuntimeError(f'Unexpected recovery verdict for {fault}')
            cases.append({'fault': fault, 'diagnosis_id': identifier, 'llm_seconds': elapsed,
                          'before_recovery': before['verdict'], 'after_recovery': after['verdict'],
                          'observed_telemetry': diagnosis['telemetry'], 'llm_explanation': explanation,
                          'report': after['report']})
        session.tool('power_off')
    finally:
        session.close()
    restarted = McpSession(root, destination)
    try:
        restarted.initialize()
        for item in cases:
            report = restarted.tool('get_diagnosis_report', {'diagnosis_id': item['diagnosis_id']})
            if report['report']['verifications'][-1]['verdict'] != 'PASS':
                raise RuntimeError('Saved verification was not preserved after restart')
    finally:
        restarted.close()
    result = {'ok': True, 'test_scope': 'Real Unreal virtual device + native bridge + MCP stdio + real local LLM',
              'model': config['model'], 'completed_at_unix_ms': int(time.time() * 1000),
              'gimbal_angle_observation': angle['telemetry'], 'cases': cases}
    (destination / 'unreal-llm-demo.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    return result


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    print(json.dumps(run(), ensure_ascii=False, indent=2))
