"""Desktop demo UI. Workers publish events; only the Tk thread updates controls."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import queue
import sys
import threading
import traceback

if __package__:
    from .demo_runtime import DemoSession
else:
    from demo_runtime import DemoSession


class DemoWindow:
    def __init__(self, root: Path) -> None:
        import tkinter as tk
        from tkinter import ttk
        from tkinter.scrolledtext import ScrolledText

        self.tk = tk
        self.window = tk.Tk()
        self.window.title('DeviceLink 시범운영')
        self.window.geometry('920x760')
        self.window.minsize(800, 650)
        self.window.configure(bg='#f3f6f9')
        self.events: queue.Queue[dict] = queue.Queue()
        self.session = DemoSession(root, self.events.put)
        self.worker: threading.Thread | None = None
        self.closing = False
        preferences = self.session.directory / 'launcher-preferences.json'
        try:
            visual = json.loads(preferences.read_text(encoding='utf-8')).get('visual_device') is True
        except (OSError, ValueError):
            visual = False
        self.visual_device = tk.BooleanVar(value=visual)
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('TFrame', background='#f3f6f9')
        style.configure('TLabel', background='#f3f6f9', font=('맑은 고딕', 10))
        style.configure('TButton', font=('맑은 고딕', 10), padding=8)
        content = ttk.Frame(self.window, padding=24)
        content.pack(fill='both', expand=True)
        ttk.Label(content, text='DeviceLink 시범운영', font=('맑은 고딕', 22, 'bold')).pack(anchor='w')
        ttk.Label(content, text='로컬 LLM · Unreal 가상장비 · MFC 관제 콘솔을 함께 준비합니다.').pack(anchor='w', pady=(5, 18))
        self.status = {}
        roles = [('llm', '로컬 LLM', '매뉴얼 근거를 바탕으로 한국어 진단·복구 절차 제안'),
                 ('unreal', 'Unreal 가상장비', '짐벌 움직임·Telemetry·가상 고장 재현'),
                 ('bridge', 'C++ 브리지', '기존 Application 계층을 통한 상태 수집과 검증'),
                 ('mfc', 'MFC 관제 콘솔', '연결·전원·초기화·각도·시나리오 조작')]
        for key, title, role in roles:
            row = ttk.Frame(content, padding=(0, 4))
            row.pack(fill='x')
            ttk.Label(row, text=title, width=15, font=('맑은 고딕', 10, 'bold')).pack(side='left')
            ttk.Label(row, text=role, width=49).pack(side='left')
            self.status[key] = tk.StringVar(value='대기')
            ttk.Label(row, textvariable=self.status[key], foreground='#007b75').pack(side='left')
        self.message = tk.StringVar(value='시범운영을 준비합니다…')
        ttk.Label(content, textvariable=self.message, wraplength=840,
                  font=('맑은 고딕', 11, 'bold')).pack(anchor='w', pady=(18, 8))
        self.progress = ttk.Progressbar(content, mode='indeterminate')
        self.progress.pack(fill='x', pady=(0, 12))
        self.visual_check = ttk.Checkbutton(content, variable=self.visual_device,
            text='3D 가상장비 화면 표시 (기본은 메모리를 적게 쓰는 백그라운드 통신 모드)')
        self.visual_check.pack(anchor='w', pady=(0, 10))
        buttons = ttk.Frame(content)
        buttons.pack(fill='x')
        self.start_button = ttk.Button(buttons, text='선택 모드로 준비', command=lambda: self.run('start'))
        self.start_button.pack(side='left')
        self.diagnose_button = ttk.Button(buttons, text='현재 상태 진단', command=lambda: self.run('diagnose'))
        self.diagnose_button.pack(side='left', padx=6)
        self.fault_button = ttk.Button(buttons, text='MotorStall 고장 시연', command=lambda: self.run('fault_demo'))
        self.fault_button.pack(side='left')
        ttk.Button(buttons, text='보고서 폴더', command=lambda: os.startfile(self.session.directory)).pack(side='left', padx=6)
        ttk.Button(buttons, text='시범운영 종료', command=self.request_close).pack(side='right')
        ttk.Label(content, text='준비 완료 후 MFC에서 장비를 조작하세요. 고장 시연은 주입 → 진단 → 해제 → 재검증을 실행합니다.',
                  wraplength=830).pack(anchor='w', pady=(14, 8))
        self.output = ScrolledText(content, font=('맑은 고딕', 10), wrap='word',
                                  background='white', relief='flat', padx=12, pady=10, state='disabled')
        self.output.pack(fill='both', expand=True)
        self.window.protocol('WM_DELETE_WINDOW', self.request_close)
        self.window.after(100, self.poll)
        self.window.after(200, lambda: self.run('start'))

    def append(self, text: str) -> None:
        self.output.configure(state='normal')
        self.output.insert('end', text + '\n')
        self.output.see('end')
        self.output.configure(state='disabled')

    def set_buttons(self, busy: bool) -> None:
        self.start_button.configure(state='disabled' if busy or self.closing else 'normal')
        self.visual_check.configure(state='disabled' if busy or self.closing else 'normal')
        active = not busy and self.session.ready and not self.closing
        for button in (self.diagnose_button, self.fault_button):
            button.configure(state='normal' if active else 'disabled')

    def run(self, operation: str) -> None:
        if self.worker is not None and self.worker.is_alive():
            return
        self.set_buttons(True)
        self.progress.start(12)
        visual = self.visual_device.get() if operation == 'start' else False
        if operation == 'start':
            self.session.cancel.clear()
            (self.session.directory / 'launcher-preferences.json').write_text(
                json.dumps({'visual_device': visual}), encoding='utf-8')

        def work() -> None:
            try:
                if operation == 'start':
                    if self.session.ready:
                        self.session.stop()
                    result = self.session.start(visual_device=visual)
                else:
                    result = getattr(self.session, operation)()
                self.events.put({'result': result, 'operation': operation})
            except Exception as error:
                with (self.session.directory / 'launcher-error.log').open('a', encoding='utf-8') as stream:
                    traceback.print_exc(file=stream)
                self.events.put({'error': str(error), 'operation': operation})
            finally:
                self.events.put({'finished': operation})

        self.worker = threading.Thread(target=work, daemon=True)
        self.worker.start()

    def show_result(self, result: dict) -> None:
        diagnosis = result.get('diagnosis', result)
        telemetry = diagnosis.get('telemetry')
        if telemetry:
            self.append('관측 상태: ' + str(telemetry.get('state')) + ' / ' + str(telemetry.get('fault')))
        explanation = diagnosis.get('llm_explanation', {})
        proposal = explanation.get('proposal', {})
        if proposal:
            self.append('\n원인 가설: ' + proposal['possible_cause'])
            for title, key in (('확인 절차', 'recommended_checks'), ('복구 제안', 'recovery_steps')):
                self.append(title + ':')
                for item in proposal[key]:
                    self.append('  · ' + item)
            self.append('근거: ' + ', '.join(proposal['citation_ids']))
        elif explanation:
            self.append({'skipped_disabled': '규칙 기반 진단입니다. LLM 설명은 요청한 경우에만 생성합니다.',
                         'skipped_no_manual_evidence': '활성 고장이 없어 LLM의 복구 제안 생성을 생략했습니다.',
                         'skipped_no_procedure_catalog': '매뉴얼의 진단 절차 목록을 확인해야 합니다.',
                         'not_configured': '로컬 모델 설정을 확인하세요.',
                         'unavailable': '로컬 모델 응답을 받지 못했습니다. 서버 상태를 확인하세요.'
                         }.get(explanation.get('status'), '진단 근거를 확인하세요.'))
        if 'before' in result:
            self.append('\n복구 검증: ' + result['before'] + ' → ' + result['after'])
        report = result.get('report', {})
        if report.get('json_path'):
            self.append('보고서: ' + report['json_path'])
        self.append('')

    def poll(self) -> None:
        try:
            while True:
                event = self.events.get_nowait()
                if 'component' in event:
                    self.message.set(event['message'])
                    if event['component'] in self.status:
                        self.status[event['component']].set({
                            'ready': '준비 완료', 'working': '준비 중', 'stopped': '종료'}.get(event['state'], '확인 필요'))
                    self.append(event['message'])
                elif 'error' in event:
                    self.message.set('작업 실패: 아래 안내와 로그를 확인하세요.')
                    self.append('\n[확인 필요] ' + event['error'])
                elif 'result' in event and isinstance(event['result'], dict):
                    self.show_result(event['result'])
                    self.message.set({'start': '시범운영 준비 완료', 'diagnose': '현재 상태 진단 완료',
                                      'fault_demo': '고장 시연 완료: FAIL → PASS'}.get(event['operation'], '작업 완료'))
                elif 'finished' in event:
                    self.progress.stop()
                    self.worker = None
                    self.set_buttons(False)
                    if event['finished'] == 'stop':
                        self.window.destroy()
                        return
                    if self.closing:
                        self.run('stop')
        except queue.Empty:
            pass
        self.window.after(100, self.poll)

    def request_close(self) -> None:
        if self.closing:
            return
        self.closing = True
        self.message.set('이 런처가 시작한 프로그램을 종료합니다…')
        self.session.cancel.set()
        if self.worker is None or not self.worker.is_alive():
            self.run('stop')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--smoke', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    session = DemoSession(root)
    if args.check or args.smoke:
        try:
            if args.check:
                checked = session.preflight()
                result = {'ok': True, 'model': checked['model'], 'paths': {
                    key: str(value) for key, value in checked['paths'].items()}}
            else:
                result = {'startup': session.start(hidden=True), 'fault_demo': session.fault_demo()}
                result['ok'] = True
        except Exception as error:
            result = {'ok': False, 'error': str(error)}
        finally:
            if args.smoke:
                session.stop()
        (session.directory / ('check-result.json' if args.check else 'smoke-result.json')).write_text(
            json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
        return 0 if result['ok'] else 1
    window = DemoWindow(root)
    window.window.mainloop()
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception:
        directory = Path(__file__).resolve().parents[1] / 'artifacts' / 'demo'
        directory.mkdir(parents=True, exist_ok=True)
        (directory / 'launcher-fatal.log').write_text(traceback.format_exc(), encoding='utf-8')
        import ctypes
        ctypes.windll.user32.MessageBoxW(None, '시범운영 시작 실패: artifacts/demo/launcher-fatal.log를 확인하세요.',
                                        'DeviceLink 시범운영', 0x10)
        raise
