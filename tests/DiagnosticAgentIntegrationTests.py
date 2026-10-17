from __future__ import annotations

from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import types
import unittest
from unittest.mock import patch

from mcp_server import server
from mcp_server.diagnostic_reports import DiagnosticReports
from mcp_server import llm_diagnostics
from tests.DeviceAgentMcpTests import BRIDGE, ROOT, VirtualDevice


@contextmanager
def local_api(proposal: dict[str, object], status: int = 200):
    requests: list[dict[str, object]] = []

    class Handler(BaseHTTPRequestHandler):
        def do_POST(self) -> None:
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            requests.append({'body': body, 'authorization': self.headers.get('Authorization'),
                             'path': self.path})
            payload = {'id': 'chatcmpl-local-test', 'object': 'chat.completion',
                       'created': int(time.time()), 'model': 'local-test-model',
                       'choices': [{'index': 0, 'finish_reason': 'stop',
                                    'message': {'role': 'assistant', 'content': json.dumps(proposal)}}]}
            if status != 200:
                payload = {'error': {'message': 'fixture rejection', 'type': 'rate_limit_error',
                                     'code': 'rate_limit_exceeded'}}
            serialized = json.dumps(payload).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(serialized)))
            self.end_headers()
            self.wfile.write(serialized)

        def log_message(self, *args: object) -> None:
            pass

    api = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=api.serve_forever, daemon=True)
    thread.start()
    environment = {'DEVICELINK_LLM_PROVIDER': 'local', 'DEVICELINK_LLM_MODEL': 'local-test-model',
                   'DEVICELINK_LLM_BASE_URL': f'http://127.0.0.1:{api.server_port}/v1',
                   'DEVICELINK_LLM_TIMEOUT_SECONDS': '5', 'OPENAI_API_KEY': 'must-not-be-forwarded'}
    try:
        with patch.dict(os.environ, environment):
            yield requests
    finally:
        api.shutdown()
        api.server_close()
        thread.join(timeout=2)


class DiagnosticAgentIntegrationTests(unittest.TestCase):
    def evidence(self) -> list[dict[str, object]]:
        return server._search_manual('MotorStall', 3)['matches']

    def proposal(self) -> dict[str, object]:
        choices = llm_diagnostics._manual_choices(self.evidence())
        return {'possible_cause': next(iter(choices['possible_cause'])),
                'recommended_checks': list(choices['recommended_checks']),
                'recovery_steps': list(choices['recovery_steps']),
                'citation_ids': ['E1']}

    def test_real_sdk_to_loopback_native_device_and_persisted_report(self) -> None:
        device = VirtualDevice()
        device.start()
        with tempfile.TemporaryDirectory(dir=ROOT) as directory, \
             local_api(self.proposal()) as requests, \
             patch.dict(os.environ, {'DEVICELINK_AGENT_BRIDGE': str(BRIDGE),
                                     'DEVICELINK_AGENT_DB': str(Path(directory) / 'events.sqlite')}):
            reports = Path(directory) / 'reports'
            bridge = server.Bridge(reports)
            try:
                self.assertTrue(server.call_tool(bridge, 'connect_device', {
                    'host': '127.0.0.1', 'port': device.port})['ok'])
                self.assertTrue(server.call_tool(bridge, 'power_on', {})['ok'])
                self.assertTrue(server.call_tool(bridge, 'inject_fault', {'fault': 'MotorStall'})['ok'])
                diagnosed = server.call_tool(bridge, 'diagnose_device', {})
                self.assertEqual(diagnosed['llm_explanation']['status'], 'generated', diagnosed)
                self.assertEqual(diagnosed['report']['status'], 'saved')
                self.assertEqual(diagnosed['actions_taken'], [])
                diagnosis_id = diagnosed['diagnosis_id']
                before = server.call_tool(bridge, 'verify_diagnosis', {'diagnosis_id': diagnosis_id})
                self.assertEqual(before['verdict'], 'FAIL')
                self.assertTrue(server.call_tool(bridge, 'clear_fault', {})['ok'])
                after = server.call_tool(bridge, 'verify_diagnosis', {'diagnosis_id': diagnosis_id})
                self.assertEqual(after['verdict'], 'PASS')
                self.assertTrue(after['events'])
                evidence_snapshot = diagnosed['evidence_refs']
            finally:
                bridge.close()
            restarted = server.Bridge(reports)
            report = server.call_tool(restarted, 'get_diagnosis_report', {'diagnosis_id': diagnosis_id})
            self.assertTrue(report['ok'])
            self.assertEqual(report['report']['diagnosis']['evidence_refs'], evidence_snapshot)
            self.assertEqual([item['verdict'] for item in report['report']['verifications']], ['FAIL', 'PASS'])
            self.assertEqual(restarted.diagnosis(diagnosis_id)['telemetry']['fault'], 'MotorStall')
            with patch.object(restarted, 'call', side_effect=[{
                'ok': True, 'device_id': 'unreal-gimbal-01', 'observed_at_unix_ms': int(time.time() * 1000),
                'telemetry': {'state': 'Ready', 'fault': 'None'}}, {'ok': True, 'events': []}]):
                verified_again = server.call_tool(restarted, 'verify_diagnosis', {'diagnosis_id': diagnosis_id})
            self.assertEqual(verified_again['verdict'], 'PASS')
            self.assertTrue((reports / (diagnosis_id + '.md')).is_file())
            self.assertEqual(len(requests), 1)
            self.assertEqual(requests[0]['authorization'], 'Bearer local-llm')
            self.assertEqual(requests[0]['path'], '/v1/chat/completions')
            body = requests[0]['body']
            self.assertNotIn('tools', body)
            self.assertEqual(body['reasoning_effort'], 'none')
            self.assertTrue(body['response_format']['json_schema']['strict'])
        device.join(timeout=2)
        self.assertFalse(device.is_alive())
        self.assertIsNone(device.failure)

    def test_provider_rate_limit_is_sanitized_and_not_retried(self) -> None:
        with local_api(self.proposal(), status=429) as requests:
            result = llm_diagnostics.explain({}, [], self.evidence())
        self.assertEqual(result, {'status': 'unavailable', 'error_code': 'rate_limited'})
        self.assertEqual(len(requests), 1)
        self.assertNotIn('must-not-be-forwarded', json.dumps(result))

    def test_unknown_citation_is_rejected_through_actual_sdk(self) -> None:
        proposal = self.proposal()
        proposal['citation_ids'] = ['E999']
        with local_api(proposal):
            result = llm_diagnostics.explain({}, [], self.evidence())
        self.assertEqual(result['error_code'], 'invalid_response')

    def test_invented_temperature_threshold_is_rejected_even_with_valid_citation(self) -> None:
        proposal = self.proposal()
        proposal['recommended_checks'] = ['Check whether motor temperature exceeds 40 degrees.']
        with local_api(proposal):
            result = llm_diagnostics.explain({}, [], self.evidence())
        self.assertEqual(result['error_code'], 'unsupported_procedure')

    def test_local_model_requires_explicit_manual_procedures(self) -> None:
        with local_api(self.proposal()) as requests:
            result = llm_diagnostics.explain({}, [], [{'citation_id': 'E1', 'excerpt': 'No procedures here.'}])
        self.assertEqual(result['status'], 'skipped_no_procedure_catalog')
        self.assertEqual(requests, [])

    def test_local_endpoint_must_be_loopback(self) -> None:
        for endpoint in ('https://api.openai.com/v1', 'http://127.0.0.1:11434/v1?x=1',
                         'http://name:secret@localhost:11434/v1', 'file:///v1'):
            with self.subTest(endpoint=endpoint), patch.dict(os.environ, {
                'DEVICELINK_LLM_PROVIDER': 'local', 'DEVICELINK_LLM_MODEL': 'local-test-model',
                'DEVICELINK_LLM_BASE_URL': endpoint}):
                result = llm_diagnostics.explain({}, [], [{'citation_id': 'E1'}])
                self.assertEqual(result['error_code'], 'invalid_configuration')

    def test_model_refusal_and_truncated_output_are_not_proposals(self) -> None:
        responses = [({'raw': types.SimpleNamespace(additional_kwargs={'refusal': 'no'}),
                       'parsed': None}, 'refused'),
                     ({'parsed': None, 'parsing_error': ValueError('truncated')}, 'invalid_response')]
        for response, expected in responses:
            model = unittest.mock.MagicMock()
            model.return_value.with_structured_output.return_value.invoke.return_value = response
            with self.subTest(expected=expected), patch.dict(os.environ, {
                'DEVICELINK_LLM_PROVIDER': 'openai', 'DEVICELINK_LLM_MODEL': 'test-model',
                'OPENAI_API_KEY': 'test-key'}), patch('langchain_openai.ChatOpenAI', model):
                result = llm_diagnostics.explain({}, [], [{'citation_id': 'E1'}])
                self.assertEqual(result['error_code'], expected)

    def test_timeout_failure_is_sanitized(self) -> None:
        class APITimeoutError(Exception):
            pass

        model = unittest.mock.MagicMock()
        model.return_value.with_structured_output.return_value.invoke.side_effect = APITimeoutError('secret fixture')
        with patch.dict(os.environ, {'DEVICELINK_LLM_PROVIDER': 'openai',
                                    'DEVICELINK_LLM_MODEL': 'test-model', 'OPENAI_API_KEY': 'test-key'}), \
             patch('langchain_openai.ChatOpenAI', model):
            result = llm_diagnostics.explain({}, [], [{'citation_id': 'E1'}])
        self.assertEqual(result, {'status': 'unavailable', 'error_code': 'timeout'})

    def test_missing_key_and_missing_evidence_do_not_invoke_model(self) -> None:
        with patch.dict(os.environ, {'DEVICELINK_LLM_MODEL': 'test-model',
                                    'DEVICELINK_LLM_PROVIDER': 'openai', 'OPENAI_API_KEY': ''}), \
             patch('langchain_openai.ChatOpenAI') as model:
            self.assertEqual(llm_diagnostics.explain({}, [], [])['status'], 'skipped_no_manual_evidence')
            self.assertEqual(llm_diagnostics.explain({}, [], [{'citation_id': 'E1'}])['error_code'], 'api_key_missing')
            model.assert_not_called()

    def test_report_write_failure_preserves_observed_fault(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            blocked = Path(directory) / 'not-a-directory'
            blocked.write_text('fixture', encoding='utf-8')
            bridge = server.Bridge(blocked)
            with patch.object(bridge, 'call', side_effect=[{
                'ok': True, 'device_id': 'test-device', 'observed_at_unix_ms': int(time.time() * 1000),
                'telemetry': {'state': 'Fault', 'fault': 'MotorStall'}}, {'ok': True, 'events': []}]), \
                 patch.object(server, '_langchain_explanation', return_value={'status': 'unavailable'}):
                result = server.call_tool(bridge, 'diagnose_device', {})
            self.assertEqual(result['verdict'], 'OBSERVED')
            self.assertEqual(result['report']['status'], 'unavailable')

    def test_report_reader_rejects_path_traversal_and_corrupt_record(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            reports = DiagnosticReports(Path(directory))
            for bad_id in ('../outside', 'A' * 32, '', 'a' * 33):
                with self.subTest(bad_id=bad_id), self.assertRaises(ValueError):
                    reports.load(bad_id)
            diagnosis_id = 'a' * 32
            (Path(directory) / (diagnosis_id + '.json')).write_text('{}', encoding='utf-8')
            bridge = server.Bridge(Path(directory))
            result = server.handle(bridge, {'id': 1, 'method': 'tools/call', 'params': {
                'name': 'get_diagnosis_report', 'arguments': {'diagnosis_id': diagnosis_id}}})
            self.assertTrue(result['result']['isError'])

    def test_stdio_server_discovers_new_tool_and_reads_report_after_restart(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            diagnosis_id = 'b' * 32
            reports = DiagnosticReports(Path(directory))
            reports.save({'diagnosis_id': diagnosis_id, 'device_id': 'test-device',
                          'observed_at_unix_ms': 1234, 'telemetry': {'state': 'Fault', 'fault': 'MotorStall'},
                          'llm_explanation': {'possible_cause': '한국어 진단 — 원인 미확인'}})
            requests = [{'id': 1, 'method': 'initialize'}, {'id': 2, 'method': 'tools/list'},
                        {'id': 3, 'method': 'tools/call', 'params': {'name': 'get_diagnosis_report',
                                                                   'arguments': {'diagnosis_id': diagnosis_id}}}]
            environment = dict(os.environ, DEVICELINK_DIAGNOSTIC_REPORT_DIR=directory)
            process = subprocess.run([sys.executable, str(ROOT / 'mcp_server' / 'server.py')],
                                     input='\n'.join(json.dumps(item) for item in requests) + '\n',
                                     capture_output=True, encoding='utf-8', env=environment,
                                     cwd=directory, timeout=10)
            self.assertEqual(process.returncode, 0, process.stderr)
            responses = [json.loads(line) for line in process.stdout.splitlines()]
            self.assertEqual([item['id'] for item in responses], [1, 2, 3])
            tools = {item['name']: item for item in responses[1]['result']['tools']}
            self.assertIn('get_diagnosis_report', tools)
            self.assertFalse(tools['diagnose_device']['annotations']['readOnlyHint'])
            self.assertTrue(responses[2]['result']['structuredContent']['ok'])
            self.assertEqual(responses[2]['result']['structuredContent']['report']['diagnosis']['device_id'], 'test-device')
            self.assertEqual(responses[2]['result']['structuredContent']['report']['diagnosis']['llm_explanation']['possible_cause'],
                             '한국어 진단 — 원인 미확인')

    def test_long_document_excerpt_contains_the_actual_match(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            manual = Path(directory) / 'late-section.md'
            manual.write_text('# Manual\n' + 'Unrelated text. ' * 100 + '\nMotorStall recovery procedure.', encoding='utf-8')
            with patch.object(server, 'MANUAL_DIR', Path(directory)):
                result = server._search_manual('MotorStall', 3)
            self.assertIn('MotorStall', result['matches'][0]['excerpt'])


if __name__ == '__main__':
    unittest.main()
