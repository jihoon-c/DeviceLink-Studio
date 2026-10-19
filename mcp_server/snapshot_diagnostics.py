"""Read-only diagnostic worker for observations supplied by MFC; never connects to equipment."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import time
import uuid

if __package__:
    from .diagnostic_service import diagnose_device
    from .diagnostic_reports import DiagnosticReports
    from .manual_search import search_manual
    from .llm_diagnostics import explain, settings
    from .local_model_runtime import local_model
else:
    from diagnostic_service import diagnose_device
    from diagnostic_reports import DiagnosticReports
    from manual_search import search_manual
    from llm_diagnostics import explain, settings
    from local_model_runtime import local_model


def validate(snapshot: dict) -> None:
    if not isinstance(snapshot, dict):
        raise ValueError('invalid_snapshot')
    observed = snapshot.get('observed_at_unix_ms')
    if not isinstance(observed, int) or isinstance(observed, bool) or not -1000 <= int(time.time()*1000)-observed <= 30000:
        raise ValueError('stale_observation')
    device = snapshot.get('device_id')
    if not isinstance(device, str) or not 1 <= len(device) <= 200:
        raise ValueError('invalid_device')
    if not isinstance(snapshot.get('include_llm', False), bool):
        raise ValueError('invalid_llm_option')
    telemetry = snapshot.get('telemetry')
    if not isinstance(telemetry, dict) or telemetry.get('state') not in {'Offline', 'Initializing', 'Ready', 'Scanning', 'Fault'}:
        raise ValueError('invalid_state')
    if telemetry.get('fault') not in {'None', 'MotorStall', 'OverTemperature', 'SensorFailure'}:
        raise ValueError('invalid_fault')
    for field in ('pan_degrees', 'tilt_degrees', 'temperature_celsius', 'supply_voltage'):
        value = telemetry.get(field)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise ValueError('invalid_telemetry')
    events = snapshot.get('events', [])
    if not isinstance(events, list) or len(events) > 20 or any(not isinstance(event, dict) for event in events):
        raise ValueError('invalid_events')
    baseline = snapshot.get('baseline_id')
    if baseline is not None and (not isinstance(baseline, str) or not re.fullmatch('[0-9a-f]{32}', baseline)):
        raise ValueError('invalid_baseline')


class SnapshotContext:
    def __init__(self, snapshot: dict, reports: DiagnosticReports) -> None:
        self.snapshot, self.reports = snapshot, reports

    def call(self, command: str) -> dict:
        if not command.startswith('GET_HISTORY '):
            raise ValueError('snapshot_worker_cannot_control_equipment')
        _, start, end, limit = command.split()
        events = [event for event in self.snapshot.get('events', [])
                  if isinstance(event.get('timestamp_unix_ms'), int)
                  and int(start) <= event['timestamp_unix_ms'] <= int(end)]
        return {'ok': True, 'events': events[-int(limit):] if int(limit) > 0 else []}

    def record_diagnosis(self, *_args) -> str:
        return uuid.uuid4().hex

    def save_report(self, result: dict) -> dict:
        try:
            return self.reports.save(result)
        except (OSError, ValueError):
            return {'status': 'unavailable'}


def run(snapshot: dict, root: Path, report_directory: Path) -> dict:
    validate(snapshot)
    reports = DiagnosticReports(report_directory)
    if snapshot.get('baseline_id'):
        identifier = snapshot['baseline_id']
        record = reports.load(identifier)
        if record is None:
            raise ValueError('baseline_not_found')
        baseline = record['diagnosis']
        conditions = {
            'same_device': snapshot['device_id'] == baseline['device_id'],
            'new_observation': snapshot['observed_at_unix_ms'] > baseline['observed_at_unix_ms'],
            'state_ready': snapshot['telemetry']['state'] == 'Ready',
            'fault_none': snapshot['telemetry']['fault'] == 'None',
        }
        verdict = ('INCONCLUSIVE' if not conditions['same_device'] or not conditions['new_observation']
                   else 'PASS' if all(conditions.values()) else 'FAIL')
        result = {'ok': True, 'diagnosis_id': identifier, 'verdict': verdict, 'conditions': conditions,
                  'telemetry': snapshot['telemetry'], 'device_id': snapshot['device_id'],
                  'observed_at_unix_ms': snapshot['observed_at_unix_ms'],
                  'verification_scope': 'observed_state_transition'}
        result['report'] = reports.save(result, verification=True)
        return result
    context = SnapshotContext(snapshot, reports)
    result = diagnose_device(context, 30000, 20,
        fresh_telemetry=lambda *_: {'ok': True, 'age_ms': int(time.time()*1000)-snapshot['observed_at_unix_ms'],
                                   'raw': snapshot},
        search_manual=lambda query, limit: search_manual(query, limit, root / 'docs/agent'))
    if result.get('diagnosis_id') is None:
        result['diagnosis_id'] = uuid.uuid4().hex
    # Save the deterministic result before optional model startup/inference.
    result['report'] = context.save_report(result)
    if snapshot.get('include_llm') and result.get('evidence_refs'):
        try:
            configuration = settings()
            if configuration['provider'] != 'local' or configuration['base_url'].rstrip('/') != 'http://127.0.0.1:11434/v1':
                raise RuntimeError('local_configuration_required')
            with local_model(root, configuration['model']):
                result['llm_explanation'] = explain(snapshot['telemetry'], snapshot.get('events', []), result['evidence_refs'])
        except (OSError, ValueError, RuntimeError):
            result['llm_explanation'] = {'status': 'unavailable', 'error_code': 'local_model_unavailable'}
        result['report'] = context.save_report(result)
    return result


def display(result: dict) -> str:
    telemetry = result['telemetry']
    lines = [f"관측 진단: {result['verdict']} / {telemetry['state']} / {telemetry['fault']}",
             f"장비: {result['device_id']}", f"관측 시각(ms): {result['observed_at_unix_ms']}"]
    for evidence in result.get('evidence_refs', []):
        lines += ['', f"[{evidence['citation_id']}] {evidence['source']} / {evidence.get('section', '')}",
                  f"문서 해시: {evidence['document_version']}", evidence['excerpt']]
    explanation = result.get('llm_explanation', {})
    proposal = explanation.get('proposal')
    if proposal:
        lines += ['', 'AI 설명: ' + proposal['possible_cause'], '확인 절차:']
        lines += ['· ' + item for item in proposal['recommended_checks']]
        lines += ['복구 제안:'] + ['· ' + item for item in proposal['recovery_steps']]
        lines += ['인용: ' + ', '.join(proposal['citation_ids'])]
    elif explanation:
        lines += ['', 'AI 설명: ' + ('선택하지 않음' if explanation['status'] == 'skipped_disabled'
                                     else '사용 불가 — 규칙 기반 진단과 보고서를 사용하세요.')]
    report = result.get('report', {})
    lines += ['', '보고서: ' + report.get('markdown_path', report.get('json_path', '저장 실패')),
              'PASS는 관측된 상태 전환이며 물리적 수리 성공을 보증하지 않습니다.']
    return '\r\n'.join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--request', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        if args.request.stat().st_size > 65536:
            raise ValueError('request_too_large')
        snapshot = json.loads(args.request.read_text(encoding='utf-8'))
        result = run(snapshot, root, root / 'artifacts/demo/mfc-diagnostics/reports')
        args.output.write_text(display(result)[:30000], encoding='utf-8')
        if result.get('report', {}).get('status') == 'saved':
            args.output.with_suffix('.report-id').write_text(result['diagnosis_id'], encoding='ascii')
        return 0
    except (OSError, ValueError, KeyError, TypeError):
        args.output.write_text('진단 요청·관측 시각·저장 경로를 확인하세요. 기존 장비 관측은 유지됩니다.', encoding='utf-8')
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
