"""Exercise the installed local LLM and native bridge using a TCP device test double."""

from __future__ import annotations

import json
import os
from pathlib import Path
import sys
import time
from unittest.mock import patch

from mcp_server import server
from mcp_server.llm_diagnostics import settings
from tests.DeviceAgentMcpTests import ROOT, VirtualDevice


def run() -> dict[str, object]:
    config = settings()
    if config['provider'] != 'local' or not config['model']:
        raise RuntimeError('Configure a local model before running this demo')
    destination = ROOT / 'artifacts' / 'diagnostics'
    destination.mkdir(parents=True, exist_ok=True)
    device = VirtualDevice()
    bridge = server.Bridge(destination)
    device.start()
    summaries = []
    with patch.dict(os.environ, {'DEVICELINK_AGENT_DB': str(destination / 'demo-events.sqlite')}):
        try:
            connected = server.call_tool(bridge, 'connect_device', {'host': '127.0.0.1', 'port': device.port})
            if not connected['ok']:
                raise RuntimeError('Could not connect the TCP test double')
            for name in ('power_on', 'initialize_device'):
                if not server.call_tool(bridge, name, {})['ok']:
                    raise RuntimeError(f'{name} failed')
            for fault in ('MotorStall', 'OverTemperature', 'SensorFailure'):
                if not server.call_tool(bridge, 'inject_fault', {'fault': fault})['ok']:
                    raise RuntimeError(f'Could not inject {fault}')
                started = time.monotonic()
                diagnosis = server.call_tool(bridge, 'diagnose_device', {})
                elapsed = time.monotonic() - started
                if diagnosis.get('llm_explanation', {}).get('status') != 'generated':
                    raise RuntimeError(f'Local LLM did not produce a validated explanation: {diagnosis.get("llm_explanation")}')
                if diagnosis['report']['status'] != 'saved' or diagnosis['actions_taken']:
                    raise RuntimeError('Diagnosis persistence or read-only contract failed')
                identifier = diagnosis['diagnosis_id']
                before = server.call_tool(bridge, 'verify_diagnosis', {'diagnosis_id': identifier})
                if not server.call_tool(bridge, 'clear_fault', {})['ok']:
                    raise RuntimeError(f'Could not clear {fault}')
                after = server.call_tool(bridge, 'verify_diagnosis', {'diagnosis_id': identifier})
                if before['verdict'] != 'FAIL' or after['verdict'] != 'PASS':
                    raise RuntimeError(f'Unexpected verification transition for {fault}')
                summaries.append({'fault': fault, 'diagnosis_id': identifier,
                                  'llm_seconds': round(elapsed, 2), 'before_recovery': before['verdict'],
                                  'after_recovery': after['verdict'], 'report': after['report'],
                                  'proposal': diagnosis['llm_explanation']['proposal']})
        finally:
            bridge.close()
    device.join(timeout=2)
    if device.is_alive() or device.failure:
        raise RuntimeError('The TCP test double did not stop cleanly')
    restarted = server.Bridge(destination)
    for item in summaries:
        report = server.call_tool(restarted, 'get_diagnosis_report', {'diagnosis_id': item['diagnosis_id']})
        if not report['ok'] or report['report']['verifications'][-1]['verdict'] != 'PASS':
            raise RuntimeError('Report retrieval after restart failed')
    result = {'ok': True, 'model': config['model'], 'provider': 'local',
              'test_scope': 'Real local LLM + native bridge + TCP device test double; not Unreal or physical hardware',
              'completed_at_unix_ms': int(time.time() * 1000), 'cases': summaries}
    (destination / 'local-llm-demo.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    return result


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    print(json.dumps(run(), ensure_ascii=False, indent=2))
