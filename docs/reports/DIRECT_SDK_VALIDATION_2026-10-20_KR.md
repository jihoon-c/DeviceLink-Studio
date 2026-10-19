# 직접 SDK 전환 검증

## 변경

LangChain은 구조화 응답 호출만 담당하고 있었으며 문서 검색·진단 조정에는 사용하지 않았다. 해당 호출을 `openai.OpenAI.chat.completions.create`로 교체하고 필수 의존성을 `pypdf`, `openai`로 축소했다. 설치된 기존 가상환경 패키지를 강제로 삭제하지 않았으므로 디스크 절감량은 측정하지 않았다.

엄격한 JSON Schema, 원문 절차·인용 검증, 모델의 장비 도구 미제공, 로컬 더미 키·프록시 우회, 자동 재시도 0회를 유지했다. 응답 거절·잘림·빈 내용·형식 오류·크기 초과를 거부하고 모든 종료 경로에서 SDK/HTTP 클라이언트를 닫는다.

## 자동 회귀

Python 49/49 통과. 실제 SDK → 루프백 HTTP → 네이티브 브리지/가상장비 통합 시험에서 `langchain_openai`, `langchain_core`, `langsmith` 모듈을 사용할 수 없게 차단했다. 인용 보고서·FAIL→PASS·재시작 후 보고서 조회가 통과했다. 거절·잘린 JSON·유효하지 않은 JSON·빈 응답·배열 응답·크기 초과·시간 초과·429 미재시도도 확인했다.

C++ 코드는 변경하지 않았다. 기존 C++ 6/6 결과는 [패키지 검증 기록](PACKAGED_DEVICE_VALIDATION_2026-10-20_KR.md)에 있다.

## 실제 로컬 모델과 최소 패키지

`python mcp_server/demo_launcher.py --smoke`를 실행했다. 실제 `qwen3:4b`와 최소 패키지 가상장비에서 MotorStall 관측 → 원문 절차·인용 설명 생성 → 보고서 저장 → 복구 전 FAIL → 명시적 해제 후 PASS가 통과했다. 종료 후 런처 소유 프로세스를 정리했다. 근거는 Git 제외 `artifacts/demo/smoke-result.json`, `launcher-state.json`, `llm.process.log`다.

## MFC와 동일한 읽기 전용 helper

화면 조작 없이 새 최소 패키지·숨김 MFC를 실행하고, 네이티브 브리지로 받은 실제 관측값·이벤트를 `snapshot_diagnostics.py --request ... --output ...` 프로세스에 전달했다.

| 항목 | 결과 |
| --- | --- |
| 각도 명령 | 목표 Pan 30° / Tilt -10° |
| 도달 관측 | Pan 29.64° / Tilt -10° (허용 오차 0.5° 이내) |
| helper 설명 | generated / qwen3:4b |
| 근거 검증 | verbatim_manual_procedures |
| helper 처리 시간 | 14.047초 (모델 준비·보고서 포함, 해당 PC 1회) |
| 장비 변경 actions_taken | 빈 목록 |
| 같은 진단의 검증 | FAIL → PASS |
| 모델 서버 TCP 11434 | 실행 전 닫힘, 처리 후 닫힘 |
| 종료 | 런처 소유 가상장비·MFC 정리 |

검증 코드·원시 결과는 Git 제외 `artifacts/demo/live-snapshot-sdk.py`, `artifacts/demo/live-snapshot-sdk/result.json`에, 인용·제안·검증 이력은 `artifacts/demo/mfc-diagnostics/reports/`에 기록했다. 모델에 장비 제어 기능을 주지 않았으며 helper는 이미 수집된 관측값만 처리했다.

## 남은 범위

이 결과는 MFC 실화면의 AI 응답 표시·보고서 버튼 조작 검증을 대신하지 않는다. 실제 3D·카메라·고장 표시, DPI·키보드 탐색과 통신 장애 시나리오가 남아 있다. 샘플 매뉴얼 기반이며 물리 장비의 고장 원인이나 수리 성공을 입증하지 않는다.
