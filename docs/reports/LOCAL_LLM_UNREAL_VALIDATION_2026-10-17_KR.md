# 로컬 LLM · Unreal 연결 검증

검증일: 2026-10-17 (한국 시간)

## 검증 구성

- 실제 실행: DeviceLink의 `UnrealVirtualDevice.uproject`, `DeviceLinkLab` 맵, 루프백 TCP 5000.
- Unreal은 `-game -nullrhi -unattended`로 실행했다. 렌더링·카메라 화면 품질은 이 시험 범위가 아니다.
- 호출 경로: 새 MCP stdio 서버 → 네이티브 `DeviceLinkAgentBridge` → Application/Protocol/Transport → Unreal.
- LLM: 이 PC의 Ollama `qwen3:4b`, 루프백 11434, 매뉴얼 절차 목록 선택 및 원문·인용 검증.
- 문서: `docs/agent/fault-codes.md`의 가상장비 샘플. 제조사 문서·물리 장비 시험 결과가 아니다.

## 관측 결과

짐벌 목표 pan=30°, tilt=-10°에 대해 허용 오차 내 pan=29.88°, tilt=-10°를 관측했다. 이후 고장 진단 시에는 pan=30°, tilt=-10°였다.

| 고장 | 진단 시 관측 | 로컬 LLM 소요 | 복구 전 | 복구 후 |
| --- | --- | --- | --- | --- |
| MotorStall | Fault/MotorStall, 온도 29.99°C | 15.17초 | FAIL | PASS |
| OverTemperature | Fault/OverTemperature, 온도 94.88°C | 2.80초 | FAIL | PASS |
| SensorFailure | Fault/SensorFailure, 온도 70.16°C | 2.83초 | FAIL | PASS |

LLM 제안은 한국어 매뉴얼 목록의 원문 문장과 `E1` 인용으로 제한되었다. 최종 PASS는 같은 장비의 더 늦은 Ready/None Telemetry 관측에 대한 판정이며, LLM이 내린 판정이 아니다. 온도 수치 자체가 실제 하드웨어 보호 기준을 의미하지 않는다.

각 사례의 진단과 복구 전·후 검증을 JSON 및 Markdown으로 저장했다. MCP 서버 프로세스를 종료하고 새로 실행한 뒤 세 진단 ID의 보고서와 마지막 PASS를 다시 조회했다. 시험 마지막에 가상장비 전원을 Off 상태로 돌렸다.

실행 결과 원본: `artifacts/diagnostics/unreal/unreal-llm-demo.json`.

## 발견 및 수정

Windows에서 MCP stdout이 기본 로캘 인코딩을 사용해 한국어 검색 결과를 UTF-8로 읽지 못하는 문제가 발견되었다. MCP 서버의 stdin/stdout을 UTF-8로 명시하고 한국어 및 비ASCII 문자가 포함된 보고서의 stdio 회귀 테스트를 추가했다. 새 프로세스로 실행한 매뉴얼 검색과 보고서 조회가 통과했다.

이 채팅에 노출된 도구 목록에는 아직 이전 캐시가 남아 있었고, 기존 `search_manual` 연결은 응답 시간 제한에 도달했다. 따라서 새 MCP stdio 프로세스에서의 전체 통과와 현재 채팅 연결 상태를 구분한다. 현재 채팅의 기존 연결 대신 새 채팅에서 서버를 새로 연결해야 수정 사항을 확인할 수 있다.

## 재현

DeviceLink Unreal 가상장비를 루프백 5000에서 실행한 뒤 프로젝트 루트에서 다음을 실행한다. 다른 프로젝트의 Unreal 프로세스는 시험 대상이 아니다.

```powershell
.\mcp_server\.venv\Scripts\python.exe -m tests.RunUnrealLlmDemo
```

시험은 가상장비의 전원·초기화·짐벌 이동·세 고장 주입 및 해제를 실행한다. 기존 Unreal 화면의 그래픽 상태를 평가하지 않는다.
