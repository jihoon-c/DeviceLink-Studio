from __future__ import annotations

import copy
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

from mcp_server import snapshot_diagnostics as worker
from mcp_server.diagnostic_reports import DiagnosticReports

ROOT = Path(__file__).resolve().parents[1]


class SnapshotDiagnosticTests(unittest.TestCase):
    def snapshot(self, fault='MotorStall'):
        return {'device_id': 'mfc-shared-device', 'observed_at_unix_ms': int(time.time()*1000),
                'include_llm': False, 'events': [], 'telemetry': {
                    'state': 'Fault' if fault != 'None' else 'Ready', 'fault': fault,
                    'pan_degrees': 0, 'tilt_degrees': 0, 'temperature_celsius': 25, 'supply_voltage': 24}}

    def test_rule_diagnosis_never_loads_model_and_persists_citations(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory, \
             patch.object(worker, 'settings', side_effect=AssertionError('No model configuration required')):
            result = worker.run(self.snapshot(), ROOT, Path(directory))
            self.assertEqual(result['llm_explanation']['status'], 'skipped_disabled')
            self.assertTrue(result['evidence_refs'])
            self.assertEqual(result['actions_taken'], [])
            self.assertEqual(result['report']['status'], 'saved')
            self.assertIn('문서 해시', worker.display(result))
            self.assertEqual(DiagnosticReports(Path(directory)).load(result['diagnosis_id'])['diagnosis']['device_id'],
                             'mfc-shared-device')

    def test_history_evidence_excludes_old_and_future_events(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            snapshot = self.snapshot()
            now = snapshot['observed_at_unix_ms']
            snapshot['events'] = [
                {'timestamp_unix_ms': now - 61000, 'detail': 'old'},
                {'timestamp_unix_ms': now - 500, 'detail': 'current'},
                {'timestamp_unix_ms': now + 10000, 'detail': 'future'}]
            result = worker.run(snapshot, ROOT, Path(directory))
            self.assertEqual([event['detail'] for event in result['events']], ['current'])

    def test_model_failure_preserves_previously_saved_observation(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            snapshot = self.snapshot(); snapshot['include_llm'] = True

            def fail_runtime(*_args):
                self.assertEqual(len(list(Path(directory).glob('*.json'))), 1)
                raise RuntimeError('fixture unavailable')

            with patch.object(worker, 'settings', return_value={
                    'provider': 'local', 'model': 'fixture', 'base_url': 'http://127.0.0.1:11434/v1'}), \
                 patch.object(worker, 'local_model', side_effect=fail_runtime):
                result = worker.run(snapshot, ROOT, Path(directory))
            self.assertEqual(result['telemetry']['fault'], 'MotorStall')
            self.assertEqual(result['llm_explanation']['status'], 'unavailable')
            self.assertEqual(result['report']['status'], 'saved')

    def test_selected_explanation_is_invoked_with_read_only_observations(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory, \
             patch.object(worker, 'settings', return_value={
                 'provider': 'local', 'model': 'fixture', 'base_url': 'http://127.0.0.1:11434/v1'}), \
             patch.object(worker, 'local_model') as runtime, \
             patch.object(worker, 'explain', return_value={'status': 'generated', 'proposal': {
                 'possible_cause': 'fixture', 'recommended_checks': ['check'],
                 'recovery_steps': ['recover'], 'citation_ids': ['E1']}}) as explain:
            snapshot = self.snapshot(); snapshot['include_llm'] = True
            result = worker.run(snapshot, ROOT, Path(directory))
            runtime.assert_called_once_with(ROOT, 'fixture')
            self.assertEqual(explain.call_args.args[0], snapshot['telemetry'])
            self.assertEqual(result['llm_explanation']['status'], 'generated')

    def test_verification_requires_same_device_and_later_observation(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            snapshot = self.snapshot()
            baseline = worker.run(snapshot, ROOT, Path(directory))
            later = copy.deepcopy(snapshot)
            later['baseline_id'] = baseline['diagnosis_id']
            later['observed_at_unix_ms'] += 1
            self.assertEqual(worker.run(later, ROOT, Path(directory))['verdict'], 'FAIL')
            later['telemetry'].update(state='Ready', fault='None')
            self.assertEqual(worker.run(later, ROOT, Path(directory))['verdict'], 'PASS')
            later['device_id'] = 'other-device'
            self.assertEqual(worker.run(later, ROOT, Path(directory))['verdict'], 'INCONCLUSIVE')
            later['device_id'] = snapshot['device_id']
            later['observed_at_unix_ms'] = snapshot['observed_at_unix_ms']
            self.assertEqual(worker.run(later, ROOT, Path(directory))['verdict'], 'INCONCLUSIVE')

    def test_invalid_or_stale_observations_never_create_report(self):
        for field, value in [('observed_at_unix_ms', 1), ('include_llm', 'false'), ('baseline_id', '../bad')]:
            with tempfile.TemporaryDirectory(dir=ROOT) as directory:
                snapshot = self.snapshot(); snapshot[field] = value
                with self.assertRaises(ValueError):
                    worker.run(snapshot, ROOT, Path(directory))
                self.assertEqual(list(Path(directory).iterdir()), [])

    def test_normal_state_also_has_a_saved_report(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            result = worker.run(self.snapshot('None'), ROOT, Path(directory))
            self.assertEqual(result['report']['status'], 'saved')
            self.assertTrue(result['diagnosis_id'])


if __name__ == '__main__':
    unittest.main()
