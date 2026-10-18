"""Application orchestration for a local demonstration; independent of the GUI."""

from __future__ import annotations

import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import socket
import subprocess
import threading
import time
from typing import Any, Callable
from urllib.request import ProxyHandler, Request, build_opener

if __package__:
    from . import server
    from .demo_processes import ManagedProcess, ProcessJob
    from .llm_diagnostics import settings
else:
    import server
    from demo_processes import ManagedProcess, ProcessJob
    from llm_diagnostics import settings


class DemoPlatform:
    def port_open(self, port: int) -> bool:
        try:
            with socket.create_connection(('127.0.0.1', port), timeout=0.5):
                return True
        except OSError:
            return False

    def http_json(self, url: str, body: dict[str, Any] | None = None,
                  timeout: int = 3) -> dict[str, Any]:
        request = Request(url, data=json.dumps(body).encode() if body is not None else None,
                          headers={'Content-Type': 'application/json'})
        with build_opener(ProxyHandler({})).open(request, timeout=timeout) as response:
            return json.load(response)

    def spawn(self, args: list[str], root: Path, log: Path,
              environment: dict[str, str], hidden: bool) -> ManagedProcess:
        startup = subprocess.STARTUPINFO()
        startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0 if hidden else 1
        job = ProcessJob()
        process = None
        try:
            with log.open('a', encoding='utf-8') as output:
                process = subprocess.Popen(args, cwd=root, env=environment, stdout=output, stderr=output,
                                           startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
            job.attach(process.pid)
            return ManagedProcess(process, job)
        except Exception:
            if process is not None:
                process.terminate()
                process.wait(timeout=3)
            job.close()
            raise

    def close_process(self, process: ManagedProcess) -> None:
        if process.poll() is not None:
            process.close_group()
            return
        user32 = ctypes.WinDLL('user32', use_last_error=True)
        callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
        user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

        @callback_type
        def close_window(window, parameter):
            owner = wintypes.DWORD()
            user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
            if owner.value == process.pid:
                user32.PostMessageW(window, 0x0010, 0, 0)  # WM_CLOSE to owned top-level windows.
            return True

        try:
            user32.EnumWindows(close_window, 0)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=3)
        finally:
            process.close_group()


class DemoSession:
    def __init__(self, root: Path, emit: Callable[[dict[str, Any]], None] | None = None,
                 platform: DemoPlatform | None = None) -> None:
        self.root = root.resolve()
        self.directory = self.root / 'artifacts' / 'demo'
        self.directory.mkdir(parents=True, exist_ok=True)
        self.emit = emit or (lambda event: None)
        self.platform = platform or DemoPlatform()
        self.cancel = threading.Event()
        self.owned: list[tuple[str, subprocess.Popen]] = []
        self.bridge: server.Bridge | None = None
        self.ready = False

    def _event(self, component: str, message: str, state: str = 'working') -> None:
        event = {'component': component, 'message': message, 'state': state, 'time': int(time.time() * 1000)}
        with (self.directory / 'launcher.log').open('a', encoding='utf-8') as stream:
            stream.write(json.dumps(event, ensure_ascii=False) + '\n')
        self.emit(event)

    def preflight(self) -> dict[str, Any]:
        mfc_candidates = [self.root / name / 'DeviceLinkMfcHost.exe'
                          for name in ('build-demo', 'build-mfc-host-ninja', 'build-mfc-host', 'build-agent-diag')]
        runtime = self.root / 'artifacts' / 'local-llm'
        ollama_candidates = sorted(runtime.glob('ollama-v*/ollama.exe'), reverse=True)
        config = settings()
        if config['provider'] != 'local':
            raise RuntimeError('시범운영은 로컬 LLM 설정이 필요합니다. Setup-LocalDiagnosticLlm.ps1을 실행하세요.')
        if config['base_url'].rstrip('/') != 'http://127.0.0.1:11434/v1':
            raise RuntimeError('시범운영 모델 주소는 http://127.0.0.1:11434/v1로 설정하세요.')
        paths = {
            'mfc': next((path for path in mfc_candidates if path.is_file()), mfc_candidates[0]),
            'bridge': server.Bridge._find_executable(),
            'ollama': ollama_candidates[0] if ollama_candidates else runtime / 'ollama-v0.40.5' / 'ollama.exe',
            'unreal': Path(os.environ.get('DEVICELINK_UNREAL_EDITOR', r'F:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe')),
            'project': self.root / 'UnrealVirtualDevice' / 'UnrealVirtualDevice.uproject',
            'module': self.root / 'UnrealVirtualDevice' / 'Binaries' / 'Win64' / 'UnrealEditor-UnrealVirtualDevice.dll',
        }
        missing = [str(path) for path in paths.values() if not path.is_file()]
        if missing:
            raise RuntimeError('필수 파일이 없습니다:\n' + '\n'.join(missing) +
                               '\nREADME의 최초 준비 및 Build-DemoLauncher.ps1을 확인하세요.')
        return {'paths': paths, 'model': config['model']}

    def _spawn(self, component: str, args: list[str], *, hidden: bool,
               environment: dict[str, str] | None = None) -> subprocess.Popen:
        environment = dict(environment if environment is not None else os.environ)
        environment.pop('OPENAI_API_KEY', None)
        process = self.platform.spawn(args, self.root, self.directory / (component + '.process.log'),
                                      environment, hidden)
        self.owned.append((component, process))
        return process

    def _wait(self, predicate: Callable[[], bool], seconds: int,
              process: subprocess.Popen | None = None) -> None:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if self.cancel.is_set():
                raise RuntimeError('시범운영 준비를 취소했습니다.')
            if process is not None and process.poll() is not None:
                raise RuntimeError('프로그램이 준비 전에 종료됐습니다. artifacts/demo 로그를 확인하세요.')
            if predicate():
                return
            self.cancel.wait(0.25)
        raise RuntimeError('준비 대기 시간이 초과됐습니다. artifacts/demo 로그를 확인하세요.')

    def _llm_available(self) -> bool:
        try:
            return 'models' in self.platform.http_json('http://127.0.0.1:11434/api/tags')
        except (OSError, ValueError):
            return False

    def _ensure_llm(self, configuration: dict[str, Any]) -> None:
        if self._llm_available():
            self._event('llm', '기존 Ollama 서버 재사용')
        else:
            if self.platform.port_open(11434):
                raise RuntimeError('11434 포트를 다른 프로그램이 사용 중입니다.')
            environment = dict(os.environ, OLLAMA_HOST='127.0.0.1:11434',
                               OLLAMA_MODELS=str(self.root / 'artifacts' / 'local-llm' / 'models'),
                               OLLAMA_NO_CLOUD='1', OLLAMA_CONTEXT_LENGTH='8192',
                               OLLAMA_MAX_LOADED_MODELS='1', OLLAMA_NUM_PARALLEL='1')
            process = self._spawn('llm', [str(configuration['paths']['ollama']), 'serve'],
                                  hidden=True, environment=environment)
            self._event('llm', 'Ollama 시작 중')
            self._wait(self._llm_available, 30, process)
        models = self.platform.http_json('http://127.0.0.1:11434/api/tags')['models']
        if configuration['model'] not in [item['name'] for item in models]:
            raise RuntimeError('로컬 모델이 없습니다. Setup-LocalDiagnosticLlm.ps1을 먼저 실행하세요.')
        self._event('llm', '모델 준비 중: ' + configuration['model'])
        self.platform.http_json('http://127.0.0.1:11434/api/generate', {
            'model': configuration['model'], 'prompt': '', 'stream': False,
            'keep_alive': '10m', 'options': {'num_ctx': 8192}}, timeout=120)
        self._event('llm', '로컬 모델 준비 완료: ' + configuration['model'], 'ready')

    def _call(self, name: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        if self.bridge is None:
            raise RuntimeError('가상장비 연결이 준비되지 않았습니다.')
        result = server.call_tool(self.bridge, name, arguments or {})
        if not result.get('ok'):
            raise RuntimeError(f'{name}: {result.get("message", "작업 실패")}')
        return result

    def start(self, *, hidden: bool = False, visual_device: bool = False) -> dict[str, Any]:
        if self.ready:
            return {'ok': True, 'already_ready': True}
        try:
            self._save_state({'ok': True, 'state': 'starting'})
            if self.cancel.is_set():
                raise RuntimeError('시범운영 준비를 취소했습니다.')
            self._event('startup', '필수 파일과 설정 확인 중')
            configuration = self.preflight()
            self._ensure_llm(configuration)
            if self.cancel.is_set():
                raise RuntimeError('시범운영 준비를 취소했습니다.')
            if self.platform.port_open(5000):
                self._event('unreal', '기존 TCP 5000 가상장비 연결 확인 중')
            else:
                args = [str(configuration['paths']['unreal']), str(configuration['paths']['project']),
                        '/Game/Maps/DeviceLinkLab', '-game', '-DeviceLinkAutoStart', '-DeviceLinkPort=5000',
                        '-abslog=' + str(self.directory / 'unreal.log')]
                args += ['-nullrhi', '-unattended', '-nosplash', '-nosound'] if hidden or not visual_device else [
                    '-windowed', '-ResX=1280', '-ResY=720',
                    '-ini:Engine:[SystemSettings]:r.RayTracing=0',
                    '-ExecCmds=t.MaxFPS 30,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 1,sg.ShadowQuality 1,sg.TextureQuality 1,r.ScreenPercentage 75']
                process = self._spawn('unreal', args, hidden=hidden or not visual_device)
                self._event('unreal', '가상장비 시작 중: TCP 5000 준비 대기')
                self._wait(lambda: self.platform.port_open(5000), 180, process)
            environment = dict(os.environ, DEVICELINK_AGENT_BRIDGE=str(configuration['paths']['bridge']),
                               DEVICELINK_AGENT_DB=str(self.directory / 'bridge.sqlite'))
            environment.pop('OPENAI_API_KEY', None)
            self.bridge = server.Bridge(self.directory / 'reports', environment=environment)
            self._call('connect_device', {'host': '127.0.0.1', 'port': 5000})
            self._event('unreal', '장비 첫 Telemetry 응답 대기 (3D 첫 화면 준비 포함)')

            def telemetry_ready() -> bool:
                try:
                    self._call('get_telemetry')
                    return True
                except RuntimeError as error:
                    if 'timed out' not in str(error).lower():
                        raise
                    return False

            self._wait(telemetry_ready, 120)
            self._call('power_on')
            observed = self._call('initialize_device')
            if self.cancel.is_set():
                raise RuntimeError('시범운영 준비를 취소했습니다.')
            self._event('unreal', '장비 Ready / fault=None 확인' +
                        (' (3D 화면 요청)' if visual_device and not hidden else ' (백그라운드 통신 모드)'), 'ready')
            self._event('bridge', 'C++ Application 경유 연결 완료', 'ready')
            marker = self.directory / 'mfc-ready.json'
            marker.unlink(missing_ok=True)
            mfc = self._spawn('mfc', [str(configuration['paths']['mfc']),
                                    '--demo-hidden' if hidden else '--demo'], hidden=hidden)

            def mfc_ready() -> bool:
                try:
                    value = json.loads(marker.read_text(encoding='utf-8'))
                    return value.get('pid') == mfc.pid and value.get('connected') is True
                except (OSError, ValueError):
                    return False

            self._event('mfc', '관제 콘솔 시작 및 자동 연결 중')
            self._wait(mfc_ready, 30, mfc)
            self._event('mfc', '관제 콘솔 연결 완료: 127.0.0.1:5000', 'ready')
            self.ready = True
            result = {'ok': True, 'state': 'ready', 'telemetry': observed.get('telemetry'),
                      'visual_device_requested': visual_device and not hidden,
                      'model': configuration['model'], 'owned_processes': {
                          name: process.pid for name, process in self.owned}}
            self._save_state(result)
            self._event('startup', '시범운영 준비 완료. MFC 조작 또는 고장 시연을 실행하세요.')
            return result
        except Exception:
            self.stop()
            raise

    def diagnose(self, *, include_llm: bool = False) -> dict[str, Any]:
        if not self.ready:
            raise RuntimeError('시범운영 준비가 먼저 필요합니다.')
        self._event('diagnosis', '새 Telemetry·이벤트·매뉴얼로 진단 중')
        return self._call('diagnose_device', {'include_llm': include_llm})

    def fault_demo(self) -> dict[str, Any]:
        if not self.ready:
            raise RuntimeError('시범운영 준비가 먼저 필요합니다.')
        self._event('diagnosis', 'MotorStall 주입 → 진단 → 해제 → 재검증')
        self._call('inject_fault', {'fault': 'MotorStall'})
        diagnosis = None
        try:
            diagnosis = self.diagnose(include_llm=True)
            if diagnosis.get('llm_explanation', {}).get('status') != 'generated':
                raise RuntimeError('LLM 설명이 생성되지 않았습니다: ' +
                                   str(diagnosis.get('llm_explanation', {})))
            before = self._call('verify_diagnosis', {'diagnosis_id': diagnosis['diagnosis_id']})
        finally:
            self._call('clear_fault')
        after = self._call('verify_diagnosis', {'diagnosis_id': diagnosis['diagnosis_id']})
        if before['verdict'] != 'FAIL' or after['verdict'] != 'PASS':
            raise RuntimeError('기대한 FAIL → PASS 전환이 관측되지 않았습니다.')
        self._event('diagnosis', '고장 시연 완료: FAIL → PASS')
        return {'ok': True, 'diagnosis': diagnosis, 'before': before['verdict'],
                'after': after['verdict'], 'report': after['report']}

    def _save_state(self, value: dict[str, Any]) -> None:
        temporary = self.directory / 'launcher-state.tmp'
        temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')
        temporary.replace(self.directory / 'launcher-state.json')

    def stop(self) -> None:
        self.ready = False
        if self.bridge is not None:
            self.bridge.close()
            self.bridge = None
        errors = []
        remaining = []
        for name, process in reversed(self.owned):
            try:
                self.platform.close_process(process)
                self._event(name, '런처가 시작한 프로세스 종료', 'stopped')
            except (OSError, subprocess.SubprocessError):
                errors.append(name)
                remaining.append((name, process))
        self.owned = list(reversed(remaining))
        self._save_state({'ok': not errors, 'state': 'stopped', 'cleanup_errors': errors})
