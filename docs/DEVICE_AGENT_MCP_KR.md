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
cmake -S . -B build-agent -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-agent --target DeviceLinkAgentBridge
```

빌드 결과:

```text
F:\GitHubRep\DeviceLinkStudio\build-agent\DeviceLinkAgentBridge.exe
```

전체 테스트:

```powershell
cmake --build build-agent
ctest --test-dir build-agent --output-on-failure
python -m unittest tests\DeviceAgentMcpTests.py -v
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
DEVICELINK_AGENT_BRIDGE=F:\GitHubRep\DeviceLinkStudio\build-agent\DeviceLinkAgentBridge.exe
```

8. 저장 후 Codex를 재시작합니다.
9. 새 작업에서 `/mcp`를 입력해 `devicelink` 연결을 확인합니다.

동일 설정을 `config.toml`에 직접 추가하려면 다음 예시를 사용합니다. Python 경로는 `where python` 결과에 맞게 바꿉니다.

```toml
[mcp_servers.devicelink]
command = "python"
args = ["F:\\GitHubRep\\DeviceLinkStudio\\mcp_server\\server.py"]
cwd = "F:\\GitHubRep\\DeviceLinkStudio"
startup_timeout_sec = 10
tool_timeout_sec = 45
env = { DEVICELINK_AGENT_BRIDGE = "F:\\GitHubRep\\DeviceLinkStudio\\build-agent\\DeviceLinkAgentBridge.exe" }
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

## 8. 프로세스 종료와 재시작

- Unreal 종료: 게임 창을 닫습니다.
- MCP 서버 종료: Codex 작업 또는 앱 종료 시 STDIO 프로세스가 종료됩니다.
- C++ 브리지 종료: MCP 서버가 종료될 때 `QUIT` 명령으로 정리합니다.
- 새 Codex 작업이나 MCP 재시작 후에는 `connect_device`를 다시 호출합니다.
- 같은 Codex 작업 안에서는 브리지가 TCP 연결과 최신 Telemetry 상태를 유지합니다.

## 9. 문제 해결

### MCP 목록에 devicelink가 없음

- Codex 저장 후 재시작 여부를 확인합니다.
- `/mcp`에서 서버 상태를 확인합니다.
- Python과 `mcp_server/server.py` 경로를 확인합니다.

### DeviceLinkAgentBridge.exe를 찾을 수 없음

```powershell
cmake --build build-agent --target DeviceLinkAgentBridge
```

`DEVICELINK_AGENT_BRIDGE`가 실제 exe 절대 경로인지 확인합니다.

### connect_device 실패

- Unreal 가상장비가 실행 중인지 확인합니다.
- Host `127.0.0.1`, Port `5000`을 확인합니다.
- 다른 프로그램이 5000 포트를 사용 중인지 확인합니다.
- LAN 운용 시 Windows 방화벽과 Private 네트워크 설정을 확인합니다.

### ACK는 왔지만 각도 시험 실패

ACK는 명령 수락을 의미합니다. Telemetry의 실제 Pan/Tilt가 제한 시간 안에 목표 허용 오차에 들어왔는지 확인합니다.