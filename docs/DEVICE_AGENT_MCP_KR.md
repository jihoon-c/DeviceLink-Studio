# DeviceLink AI Agent · MCP 운용 가이드

## 전체 실행 구조

```text
사용자 자연어
  → Codex Agent
  → DeviceLink MCP Server (Python, stdio)
  → DeviceLinkAgentBridge (C++ Application adapter)
  → DeviceManager / VirtualGimbalService
  → Protocol → TcpTransport
  → Unreal Virtual EO/IR (TCP 5000)
  → ACK / Telemetry
  → 결정적 판정 및 자연어 요약
```

MCP 서버는 TCP 패킷을 직접 생성하지 않습니다. 모든 장비 명령은 C++ Application 계층을 거쳐 기존 Protocol과 Transport로 전달됩니다.

## 프로세스별 역할과 실행 주체

| 프로세스 | 역할 | 실행 방법 |
| --- | --- | --- |
| Unreal Virtual Device | EO/IR 동작, ACK·Telemetry, Fault 시뮬레이션 | 사용자가 `run-unreal-virtual-device.bat` 실행 |
| DeviceLink MCP Server | MCP 초기화, 도구 스키마, RAG 문서 검색, Tool Call 변환 | Codex가 STDIO 서버로 자동 실행 |
| DeviceLinkAgentBridge | Application API 호출, TCP 세션 유지, ACK·Telemetry 판정 | MCP 서버가 최초 장비 도구 호출 때 자동 실행 |
| Codex Agent | 자연어 해석, 도구 순서 결정, 결과 요약 | 사용자가 Codex 작업에서 자연어 입력 |

평상시에는 Unreal만 직접 실행합니다. MCP 서버와 C++ 브리지는 Codex가 필요할 때 시작합니다.

## 1. 최초 빌드

Visual Studio Developer PowerShell 또는 Developer Command Prompt에서 실행합니다.

```powershell
cd F:\GitHubRep\DeviceLinkStudio
cmake -S . -B build-agent-diag -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-agent-diag --target DeviceLinkAgentBridge
```

빌드 결과:

```text
F:\GitHubRep\DeviceLinkStudio\build-agent-diag\DeviceLinkAgentBridge.exe
```

전체 테스트:

```powershell
cmake --build build-agent-diag
ctest --test-dir build-agent-diag --output-on-failure
$env:DEVICELINK_AGENT_BRIDGE = "F:\GitHubRep\DeviceLinkStudio\build-agent-diag\DeviceLinkAgentBridge.exe"
.\mcp_server\.venv\Scripts\python.exe -m unittest tests.DeviceAgentMcpTests -v
```

## 2. Codex에 MCP 서버 등록

Codex 데스크톱에서 다음 순서로 등록합니다.

1. **Settings → MCP servers → Add server**
2. 이름: `devicelink`
3. 타입: `STDIO`
4. Command: Python 3 실행 파일 경로
5. Arguments: `F:\GitHubRep\DeviceLinkStudio\mcp_server\server.py`
6. Working directory: `F:\GitHubRep\DeviceLinkStudio`
7. 환경 변수 추가:

```text
DEVICELINK_AGENT_BRIDGE=F:\GitHubRep\DeviceLinkStudio\build-agent-diag\DeviceLinkAgentBridge.exe
```

8. 저장 후 Codex를 재시작합니다.
9. 새 작업에서 `/mcp`를 입력해 `devicelink` 연결을 확인합니다.

이 PC의 현재 Codex 설정은 `mcp_server/.venv/Scripts/python.exe`와 `build-agent-diag/DeviceLinkAgentBridge.exe`를 사용한다. 설정 파일을 바꾼 뒤 이미 실행 중인 MCP 프로세스는 이전 도구 목록을 유지할 수 있으므로 Codex를 재시작하고 `diagnose_device`, `get_event_history`, `verify_diagnosis`가 목록에 나타나는지 확인한다.

동일 설정을 `config.toml`에 직접 추가하려면 다음 예시를 사용합니다.

```toml
[mcp_servers.devicelink]
command = "F:\\GitHubRep\\DeviceLinkStudio\\mcp_server\\.venv\\Scripts\\python.exe"
args = ["F:\\GitHubRep\\DeviceLinkStudio\\mcp_server\\server.py"]
cwd = "F:\\GitHubRep\\DeviceLinkStudio"
startup_timeout_sec = 10
tool_timeout_sec = 45
env = { DEVICELINK_AGENT_BRIDGE = "F:\\GitHubRep\\DeviceLinkStudio\\build-agent-diag\\DeviceLinkAgentBridge.exe" }
```

## 3. Unreal 가상장비 실행

프로젝트 루트에서 실행합니다.

```powershell
.\run-unreal-virtual-device.bat
```

스크립트는 Unreal 프로젝트를 빌드하고 `DeviceLinkLab` 맵을 게임 모드로 실행합니다. 기본 DeviceLink TCP endpoint는 다음과 같습니다.

```text
127.0.0.1:5000
```

Unreal 화면과 장비 상태 UI가 나타난 뒤 Agent 명령을 실행합니다.

## 4. 자연어 장비 운용

Codex의 새 작업에서 다음과 같이 요청합니다.

```text
127.0.0.1:5000의 EO/IR에 연결하고 Pan 30도, Tilt -10도로 이동한 다음 Telemetry로 실제 도달 여부를 확인해줘.
```

예상 Tool Call:

```text
connect_device(host="127.0.0.1", port=5000)
set_gimbal_angle(pan_degrees=30, tilt_degrees=-10)
```

`set_gimbal_angle`은 명령 ACK만으로 성공 처리하지 않습니다. 제한 시간 안에 실제 Telemetry 각도가 요청값의 허용 오차에 들어와야 성공합니다.

현재 상태만 확인하려면:

```text
현재 EO/IR Telemetry 확인해줘.
```

호출 도구:

```text
get_telemetry()
```

## 5. Fault 주입과 복구

개별 제어:

```text
Motor Stall을 주입하고 Fault 상태가 확인되는지 검사해줘.
```

```text
Fault를 해제하고 Ready 상태로 복구됐는지 확인해줘.
```

예상 Tool Call:

```text
inject_fault(fault="MotorStall")
clear_fault()
```

지원 Fault:

- `MotorStall`
- `OverTemperature`
- `SensorFailure`

## 6. 자동 시험 시나리오

```text
motor_stall_recovery 시험을 실행하고 단계별 결과와 최종 판정을 알려줘.
```

호출 도구:

```text
run_test_scenario(scenario="motor_stall_recovery")
```

시험 단계:

1. MotorStall 주입
2. Telemetry에서 `state=Fault`, `fault=MotorStall` 확인
3. Fault 해제
4. Telemetry에서 `state=Ready`, `fault=None` 확인
5. 관측 결과를 근거로 PASS/FAIL 반환

## 7. 매뉴얼 RAG

현재 `search_manual`은 `docs/agent`의 Markdown 절과 PDF 페이지를 단어 빈도로 검색한다. 결과에는 문서 내용 해시, 절 또는 페이지, 인용 ID가 포함된다. 이미지로만 구성된 PDF의 OCR, 임베딩·벡터 검색은 [AI 진단 에이전트 기획](AI_DIAGNOSTIC_AGENT_PLAN_KR.md)의 후속 범위다.

현재 사용할 수 있는 읽기 전용 진단 도구는 `get_recent_events`(현 브리지 세션 이벤트), `get_event_history`(SQLite의 장비 ID·시간 범위 이력), `diagnose_device`(새 Telemetry·영속 이벤트·매뉴얼 근거 묶음), `verify_recovery`(새 Telemetry의 Ready/None 조건 확인)다. `verify_recovery`의 PASS는 조회한 현재 상태에만 적용된다. 진단 도구는 fault 해제나 장비 제어를 자동 실행하지 않는다.

PDF 검색과 선택적 LLM 설명을 사용하려면 MCP 서버 Python 환경에 `python -m pip install -r mcp_server/requirements-diagnostics.txt`를 실행한다. 현재 로컬 구성은 Ollama + `qwen3:4b`이며 `mcp_server/llm.local.json`에서 모델과 루프백 주소를 읽는다. 외부 API 키를 사용하지 않는다. 최초 준비와 재부팅 후 실행은 [로컬 LLM 안내](LOCAL_LLM_SETUP_KR.md)를 따른다. 모델을 설정하지 않으면 관측·검색 결과만 반환한다. `DEVICELINK_AGENT_DB`를 지정하면 브리지 SQLite 경로를 바꿀 수 있고, 기본값은 작업 디렉터리의 `DeviceLinkStudio.sqlite`다. 모델 설명은 제안으로만 기록되며 판정값을 바꾸지 않는다.

고장 진단 시 `diagnose_device`가 `diagnosis_id`를 반환한다. 사용자가 복구 명령을 실행한 후 `verify_diagnosis(diagnosis_id=...)`를 호출하면 최초 Fault 관측과 나중의 Ready/None Telemetry, 그 사이 영속 이벤트를 함께 반환한다. PASS는 같은 장비에서 더 늦은 정상 상태가 **관측되었다**는 뜻이며 특정 복구 명령이 원인이었다고 단정하지 않는다. 진단과 검증은 `artifacts/diagnostics`에 JSON·Markdown으로 저장한다. `get_diagnosis_report`로 저장된 근거와 결과를 조회할 수 있고, 같은 보고서 폴더를 사용하면 서버 재시작 후에도 진단 ID로 재검증할 수 있다.

질문:

```text
Motor Stall은 어떤 상황이고 어떻게 복구해?
```

Agent는 `search_manual`로 다음 로컬 문서를 검색합니다.

- `docs/agent/eo-ir-manual.md`
- `docs/agent/fault-codes.md`

후속 요청:

```text
그 Fault를 실제로 재현하고 정상 복구되는지 확인해줘.
```

Agent는 검색 결과에서 Fault 종류를 결정한 뒤 장비 도구 또는 자동 시험 시나리오를 호출합니다.

## 8. Codex 없이 자연어 명령 사용

MCP 서버는 `execute_instruction` 도구를 제공하며, 한국어 또는 영어 복합 명령을 한 번에
해석합니다. 각도 이동 명령은 필요할 때 자동으로 연결, 전원 ON, 초기화를 선행합니다.

MCP 클라이언트에서 직접 호출하는 예:

```json
{
  "name": "execute_instruction",
  "arguments": {
    "instruction": "127.0.0.1:5000에 연결하고 Pan 30도, Tilt -10도로 이동해줘"
  }
}
```

Codex나 별도 MCP 클라이언트 없이 Windows 콘솔에서 바로 입력하려면:

```powershell
.\run-natural-language-console.bat
```

대화형 프롬프트에 다음처럼 입력합니다.

```text
DeviceLink> Pan 30도, Tilt -10도로 이동해줘
DeviceLink> 현재 Telemetry 상태 확인해줘
DeviceLink> MotorStall을 주입하고 복구 시험해줘
```

한 번만 실행할 수도 있습니다.

```powershell
.\run-natural-language-console.bat "Pan 30도, Tilt -10도로 이동해줘"
```

자연어 해석은 외부 LLM이 아니라 서버 내부의 허용 목록 기반 결정적 파서입니다. 지원하지
않거나 모호한 문장은 장비 명령을 보내지 않고 오류를 반환합니다.

## 9. 프로세스 종료와 재시작

- Unreal 종료: 게임 창을 닫습니다.
- MCP 서버 종료: Codex 작업 또는 앱 종료 시 STDIO 프로세스가 종료됩니다.
- C++ 브리지 종료: MCP 서버가 종료될 때 `QUIT` 명령으로 정리합니다.
- 새 Codex 작업이나 MCP 재시작 후에는 `connect_device`를 다시 호출합니다.
- 같은 Codex 작업 안에서는 브리지가 TCP 연결과 최신 Telemetry 상태를 유지합니다.

## 10. 문제 해결

### MCP 목록에 devicelink가 없음

- Codex 저장 후 재시작 여부를 확인합니다.
- `/mcp`에서 서버 상태를 확인합니다.
- Python과 `mcp_server/server.py` 경로를 확인합니다.

### DeviceLinkAgentBridge.exe를 찾을 수 없음

```powershell
cmake --build build-agent-diag --target DeviceLinkAgentBridge
```

`DEVICELINK_AGENT_BRIDGE`가 실제 exe 절대 경로인지 확인합니다.

### connect_device 실패

- Unreal 가상장비가 실행 중인지 확인합니다.
- Host `127.0.0.1`, Port `5000`을 확인합니다.
- 다른 프로그램이 5000 포트를 사용 중인지 확인합니다.
- LAN 운용 시 Windows 방화벽과 Private 네트워크 설정을 확인합니다.

### ACK는 왔지만 각도 시험 실패

ACK는 명령 수락을 의미합니다. Telemetry의 실제 Pan/Tilt가 제한 시간 안에 목표 허용 오차에 들어왔는지 확인합니다.
