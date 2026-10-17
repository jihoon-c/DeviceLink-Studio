# 로컬 LLM 진단 연동

## 현재 구성

Ollama 휴대용 런타임과 `qwen3:4b`를 사용한다. 모델은 이 PC에서 실행되고 진단 요청은 `http://127.0.0.1:11434/v1`로 전송된다. 로컬 모드에서는 외부 API 키 대신 고정된 무인증용 값 `local-llm`을 사용하고 HTTP 프록시를 거치지 않는다. Ollama는 `OLLAMA_NO_CLOUD=1`로 실행한다.

- 런타임·모델: `artifacts/local-llm/` (Git 제외)
- 로컬 설정: `mcp_server/llm.local.json` (Git 제외, 비밀 정보 없음)
- 진단 JSON·Markdown: `artifacts/diagnostics/` (Git 제외)
- LLM 어댑터: `mcp_server/llm_diagnostics.py`
- 영속 보고서: `mcp_server/diagnostic_reports.py`

`llm.local.json`은 매 진단 시 읽으므로 모델명 변경에는 MCP 재시작이 필요 없다. Python 코드나 MCP 도구 목록을 변경한 뒤에는 Codex를 재시작한다.

## 준비 및 실행

프로젝트 루트의 PowerShell에서 실행한다. 최초 실행은 공식 Ollama 배포 ZIP과 로컬 모델을 다운로드한다. ZIP은 GitHub 공식 릴리스의 SHA-256과 비교한 후 압축을 해제한다. 관리자 설치나 Windows 자동 시작 등록은 수행하지 않는다.

```powershell
.\scripts\Setup-LocalDiagnosticLlm.ps1
```

PC 재부팅 등으로 Ollama가 종료된 경우 이미 다운로드한 모델을 사용해 다시 시작한다.

```powershell
.\scripts\Setup-LocalDiagnosticLlm.ps1 -SkipPull
```

설정 예시:

```json
{
  "provider": "local",
  "model": "qwen3:4b",
  "base_url": "http://127.0.0.1:11434/v1",
  "timeout_seconds": 20
}
```

환경 변수 `DEVICELINK_LLM_PROVIDER`, `DEVICELINK_LLM_MODEL`, `DEVICELINK_LLM_BASE_URL`, `DEVICELINK_LLM_TIMEOUT_SECONDS`를 지정하면 파일보다 우선한다. 로컬 모드의 주소는 루프백 `/v1` 주소만 허용한다. 다른 제공사로 자동 전환하지 않는다. LLM 시간 제한은 MCP 도구 시간 제한보다 작게 설정한다.

## 매뉴얼에 근거한 제안

샘플 문서는 `docs/agent/fault-codes.md`이며 실제 제조사 문서가 아니다. `MotorStall`, `OverTemperature`, `SensorFailure` 절마다 다음 소제목과 목록을 둔다.

```markdown
### 원인 가설
- 매뉴얼이 뒷받침하는 원인 가설 또는 미확인 사항.

### 확인 절차
- 근거가 있는 확인 절차.

### 복구 제안
- 사용자가 요청했을 때 제안할 수 있는 복구 절차.
```

로컬 모델은 검색된 발췌문의 목록에서 제안 문장을 선택한다. JSON Schema의 선택지를 제한하고 응답을 다시 검증해, 목록 밖 문장과 잘못된 인용을 거부한다. 결과의 `source_validation=verbatim_manual_procedures`가 이 범위를 표시한다. 이는 자유롭게 생성한 설명 전체의 사실성을 검증한다는 뜻은 아니다. 절차 목록이 없는 문서는 검색 근거로 표시하지만 로컬 설명 생성은 `skipped_no_procedure_catalog`로 처리한다.

첫 실제 모델 시험에서는 매뉴얼에 없는 `40°C`, `0–10V`, 센서 교체 제안이 생성되었다. 이를 수용하지 않도록 절차 선택 검증을 추가했으며, 같은 유형의 임의 온도 기준을 거부하는 회귀 테스트를 둔다. 샘플의 온도 보호 고장 코드는 실제 과열이나 특정 임계값 초과를 증명하지 않는다.

현재 검색은 Markdown 절·텍스트 PDF 페이지의 키워드 검색이다. 긴 절에서는 검색어 주변을 발췌한다. 텍스트가 추출되지 않는 PDF는 OCR 필요 상태를 표시한다. 실제 제조사 PDF/OCR·임베딩 검색 품질은 아직 검증 범위가 아니다.

## 진단 및 보고서

1. `diagnose_device`는 네이티브 Application API의 새 Telemetry와 SQLite 이벤트를 읽는다.
2. 관련 매뉴얼을 검색하고 로컬 LLM이 인용과 절차를 선택한다. LLM에는 장비 제어 도구를 제공하지 않는다.
3. 고장 진단은 JSON과 Markdown으로 자동 저장된다. 저장 실패는 관측 결과와 구분해 `report.status=unavailable`로 표시한다.
4. 사용자가 복구를 실행한 후 `verify_diagnosis(diagnosis_id=...)`가 같은 장비의 더 늦은 Ready/None 관측을 확인한다.
5. `get_diagnosis_report(diagnosis_id=...)`로 저장된 원래 문서 발췌·해시·LLM 제안·검증 이력을 조회한다. 서버 재시작 후에도 동일한 보고서 폴더를 사용하면 진단 ID를 조회·재검증할 수 있다.

JSON이 정본이며 Markdown은 JSON 내용을 문자 그대로 보여주는 내보내기다. 한 진단에 최근 검증 50건까지 저장한다. PASS는 관측한 상태 전환에만 적용하며 특정 조치가 복구의 원인이었다고 확정하지 않는다.

## 검증 명령

네트워크 요청 없이 의존성·설정을 확인한다.

```powershell
.\mcp_server\.venv\Scripts\python.exe mcp_server/check_diagnostics.py
```

실제 로컬 모델을 한 번 호출한다. 합성 Telemetry와 샘플 매뉴얼에 대한 연결·형식·인용 검사다.

```powershell
.\mcp_server\.venv\Scripts\python.exe mcp_server/check_diagnostics.py --live
```

SDK·네이티브 브리지·루프백 통신·오류 처리·PDF·보고서 회귀 테스트:

```powershell
.\mcp_server\.venv\Scripts\python.exe -m unittest tests.DeviceAgentMcpTests tests.DiagnosticAgentIntegrationTests -v
```

설치된 실제 로컬 모델과 C++ 브리지를 TCP 시험용 가상장비에 연결해 세 고장의 FAIL→PASS를 검증하고 결과를 저장한다.

```powershell
.\mcp_server\.venv\Scripts\python.exe -m tests.RunLocalLlmDemo
```

결과는 `artifacts/diagnostics/local-llm-demo.json`에 저장한다. 이 시험은 Unreal이나 물리 장비를 실행한 시험이 아니다.

실제 DeviceLink Unreal 가상장비를 루프백 5000에서 실행한 경우 다음 MCP stdio 통합 시험을 사용한다.

```powershell
.\mcp_server\.venv\Scripts\python.exe -m tests.RunUnrealLlmDemo
```

이 시험의 결과는 `artifacts/diagnostics/unreal/unreal-llm-demo.json`에 저장한다. [2026-10-17 실제 Unreal 검증 기록](reports/LOCAL_LLM_UNREAL_VALIDATION_2026-10-17_KR.md)을 참고한다. Windows의 MCP stdin/stdout은 UTF-8로 명시한다. 이전 프로세스나 도구 목록이 현재 채팅에 남아 있으면 새 채팅에서 연결을 확인한다.

## 공식 참고 자료

- [Ollama Windows 휴대용 배포](https://docs.ollama.com/windows)
- [Ollama OpenAI 호환 API](https://docs.ollama.com/api/openai-compatibility)
- [Qwen3 4B 모델](https://ollama.com/library/qwen3:4b)
- [OpenAI 구조화 출력](https://developers.openai.com/api/docs/guides/structured-outputs?api-mode=chat)
