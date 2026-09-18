# DeviceLink Studio

Windows C++20 기반의 프로토콜 독립형 장비 통신 및 SILS 스타일 테스트 환경입니다.
TCP 바이트 전송, 프레임 검증, 장비 상태 관리, 이벤트 기반 UI 바인딩, 콘솔 시뮬레이터와
자동화 시나리오 실행을 분리된 계층으로 제공합니다.

> **포트폴리오 핵심 목적**: 실제 하드웨어를 판매·제어하는 제품이 아니라, Unreal Engine 내부의
> EO/IR 가상 장비와 Windows MFC 연결 프로그램을 결합해 실무 장비통신 개발 역량을 증명하는
> SILS형 포트폴리오입니다. 즉 Unreal은 장비/현장/고장 조건을 재현하고, DeviceLink Studio는
> 연결·명령·수신·상태관리·알람·기록·자동시험을 담당합니다.

## 현재 구현 범위

- Winsock2 TCP transport: 연결, 송신 큐, 수신 worker, 안전 종료
- 제한 시간 기반 non-blocking TCP connect와 최대 횟수 자동 재접속
- loopback 강제 종료·재접속 회귀 테스트와 선택 실행형 1,000회 Transport Soak 테스트
- 프레임 프로토콜: 헤더 직렬화, CRC16, 스트림 파싱 및 손상 프레임 복구
- Application: 장비 등록, 세션 관리, 상태 전이, 이벤트 큐
- 교체형 장비 Adapter: 의미 단위 짐벌 명령과 제조사별 frame/ACK/Telemetry 변환 분리
- MFC 제어 콘솔: 시작 운용 모드 선택(고정카메라/드론), Host/Port 연결, 전원, 초기화,
  Pan/Tilt 또는 드론 이륙·착륙·좌표 이동·귀환, 장비·통신 장애 주입
- 실시간 모니터: ACK, telemetry, 장비 상태, 통신 오류 및 알람
- Telemetry heartbeat 감시: 연결 후 1.5초 Warning, 3초 Fault, 정상 수신 시 자동 복구
- Telemetry 품질 지표: 수신 수, sequence gap 기반 누락 추정, 전달률, 수신 간격 및 지터 표시
- 시험 자동화: `.dls` 시나리오 선택·실행·중지·단계별 진행 상태
- 시험 결과 리포트: 완료·실패·취소 시 단계별 PASS/FAIL/NOT RUN Markdown 보고서 자동 생성
- Record & Replay: 실제 송신 명령 영속화 및 재전송, replay 증폭 방지
- 알람 운용: 통신 알람 목록과 사용자 확인 처리
- 이벤트 이력: SQLite 기록을 장비 ID·종류·검색어로 필터링해 최근 200건 조회
- 운영 설정: endpoint, 자동 재접속, 시나리오 경로와 이력 필터 자동 저장·복원
- DPI 대응 UI: Per-Monitor V2, 고해상도 글꼴 재생성, 창 크기에 따른 목록 영역 확장
- 콘솔 시뮬레이터: echo, 지연, 연결 종료, CRC 손상, telemetry 모드
- ScenarioRunner: 연결, 프레임 전송, 해제, 지연, 취소
- Infrastructure: 파일 로거 및 비동기 로거
- Frame replay: 검증 완료 프레임을 지정한 간격으로 재전송
- SQLite: event repository, 단일 worker 기반 비동기 저장, 장비 세션 이벤트 영속화
- Unreal 가상장비: EO/IR 짐벌 상태 머신, 물리 동작, TCP 서버, 현장 blockout 레벨
- Unreal 통합 Soak: 무인 서버 기동, 실제 명령/ACK/Telemetry 검증, 주기적 TCP 재연결
- Fault simulation: Motor Stall, Over Temperature, Sensor Failure, 응답 지연, 무응답 및 정상 복구
- Axis VAPIX 가상 장비 프로파일: PTZ 절대 이동·홈·연속 이동/정지·위치 조회 HTTP 계약 검증

제조사 사양서 기반 실장 Adapter와 설치 패키지는 후속 외부 입력·배포 범위입니다.

## Axis PTZ 가상 장비 프로파일

`AxisVapixCommandAdapter`는 Axis VAPIX PTZ CGI 계약에 맞춰 홈 이동, 절대 Pan/Tilt, 연속 Pan
이동/정지, 위치 조회 요청을 만듭니다. 이는 실제 Axis 카메라 구매를 전제하지 않고, 이후 Unreal
가상 짐벌이 해당 HTTP 계약을 흉내 내도록 하기 위한 비교 대상 프로파일입니다. 현재 포트폴리오의
검증 대상은 항상 **Unreal 가상 장비 ↔ Windows 연결 프로그램**입니다.

## 운용 방법 (한국어)

### 사전 조건

- Windows와 Visual Studio C++ 데스크톱 개발 도구, MFC, CMake가 설치되어 있어야 합니다.
- Unreal Engine 5.8은 기본 경로 `F:\UE_5.8`에 설치되어 있어야 합니다. 다른 경로를 사용하면
  `run-unreal-virtual-device.bat`와 `run-integration-demo.bat`의 `UNREAL_EDITOR`를 수정합니다.
- 기본 통신은 동일 PC의 `127.0.0.1:5000` TCP입니다. 방화벽 경고가 나오면 개인 네트워크에서
  Unreal 실행 파일의 통신을 허용합니다.

### 빠른 실행

`run-integration-demo.bat`를 더블클릭하면 Unreal 게임 창과 MFC 콘솔을 함께 실행합니다.
개발용으로 따로 실행하려면 다음 순서를 사용합니다.

1. `run-unreal-virtual-device.bat`를 실행하고 Unreal Editor에서 **Play**를 누릅니다.
2. 시작 안내에서 Port `5000`으로 **시뮬레이션 시작**을 누릅니다.
3. `run-mfc-host.bat`를 실행합니다.
4. MFC 화면의 `127.0.0.1`, `5000`을 확인하고 **연결**을 누릅니다.
5. 상단 **운용 모드**를 선택한 뒤 **연결**을 누릅니다. 현재 선택한 모드는 연결 직후 Unreal
   가상장비로 적용됩니다.
6. 아래 모드별 절차를 수행하고 MFC의 ACK/Telemetry/State와 Unreal의 움직임을 함께 확인합니다.

### 고정카메라 관제 모드

1. **고정카메라 관제 모드**를 선택합니다.
2. **전원 ON** → **초기화**를 누릅니다.
3. `Pan`, `Tilt`에 각각 Pan `-170~170°`, Tilt `-45~80°` 범위의 값을 입력하고 **각도 적용**을 누릅니다.
4. 필요하면 **스캔 시작/스캔 정지**로 왕복 감시 동작을 확인합니다.
5. **상태 요청**으로 즉시 telemetry를 수신합니다.

### 드론 관제 모드

1. **드론 관제 모드**를 선택하고 **연결**합니다. Unreal의 EO/IR 장비가 큐브 기반 드론 기체에
   부착된 가상장비로 전환됩니다.
2. **이륙**을 누릅니다. 드론은 홈 위치의 고도 10 m까지 상승합니다.
3. `X 좌표 m`, `Y 좌표 m`에 목표 좌표를 입력하고 **좌표 이동**을 누릅니다. 현재 UI의 이동 고도는
   10 m로 고정되어 있습니다.
4. **귀환**은 홈 위치 상공(10 m)으로 이동시키고, **착륙**은 현재 위치에서 지상으로 내립니다.
5. 모드를 바꾸면 Unreal 가상 드론은 홈 위치/지상 상태로 안전하게 초기화됩니다.

### 모니터링·장애·시험 운용

- `수신 패킷`에서 TX/RX 프레임과 ACK 성공 여부를 확인합니다.
- `실시간 장비 상태`에서 상태, 고장, 현재/목표 각도, 온도·전압, telemetry 품질을 확인합니다.
- `장비 / 통신 장애 시뮬레이션`에서 Motor Stall, Over Temperature, Sensor Failure, 응답 지연,
  무응답을 주입하고 정상 응답으로 복구할 수 있습니다.
- 우측의 기본 `.dls` 시나리오를 실행하면 고정카메라 자동 시연을 수행하고 Markdown 시험 보고서를
  생성합니다. 드론은 현재 수동 관제 절차로 검증합니다.
- 연결이 끊기면 자동 재접속을 사용하거나 **연결 해제** 후 다시 연결합니다. 연결 해제는 TCP worker와
  socket을 안전하게 종료합니다.

MFC 오른쪽의 기본 시나리오 `..\scenarios\unreal-gimbal-demo.dls`를 실행하면 전원,
초기화, 각도 이동, 스캔, 상태 요청을 순차 수행합니다. 이후 **송신 기록 Replay**로 저장된
TX 명령을 다시 실행할 수 있습니다.

시나리오가 끝나면 시나리오 파일과 같은 폴더에 `파일이름.report.완료시각.md`가 자동 생성됩니다.
기존 결과를 덮어쓰지 않고 실행마다 별도 보관합니다. 이 파일은
최종 판정, 실행 시간, 실패 원인, 각 단계의 명령·지연·결과를 남기므로 포트폴리오 및 시험 증적에
바로 사용할 수 있습니다.

`scenarios\unreal-gimbal-fault-demo.dls`는 과열, 센서 고장, 1초 응답 지연, 무응답과
정상 복구를 순서대로 시연합니다. 같은 기능은 MFC의 **장비 / 통신 장애 시뮬레이션** 영역에서
개별 선택할 수 있습니다. 무응답 상태에서도 복구용 응답 설정 명령은 즉시 처리됩니다.

화면 하단의 이벤트 이력에서는 `connection-state`, `frame-sent`, `frame-replayed`,
`frame-received`, `transport-error`, `telemetry-heartbeat`를 구분해 조회할 수 있습니다.

TCP 연결은 기본 3초 timeout을 사용합니다. MFC의 **자동 재접속**이 켜져 있으면 최초 연결 실패나
원격 종료 후 1.5초 간격으로 최초 시도를 포함해 최대 5회 연결하며, 사용자가 연결 해제를 누르면
재접속 의도를 취소하고 worker를 안전하게 종료합니다.

MFC를 정상 종료하면 현재 Host/Port, 자동 재접속 여부, 시나리오 경로와 이력 검색 조건을
`DeviceLinkStudio.settings`에 원자적으로 저장하고 다음 실행에 복원합니다. 설정 파일이 없거나
손상된 경우에는 안전한 기본값을 사용합니다.

MFC 창은 최소 운용 크기 아래로 줄어들지 않으며 최대화할 수 있습니다. 창 높이가 늘어나면
패킷·오류·이력 목록이 자동으로 확장되고, 배율이 다른 모니터로 이동하면 글꼴과 컨트롤을
해당 모니터 DPI에 맞춰 다시 배치합니다.

오른쪽 하단의 **Heartbeat 적용**에서 Warning/Fault 시간을 ms 단위로 바꿀 수 있습니다. Warning은
100~600,000 ms, Fault는 Warning보다 큰 값만 허용하며 적용값은 다음 실행에도 복원됩니다.

실시간 장비 상태 영역의 `Quality` 행은 telemetry sequence를 기준으로 수신 수와 누락 추정치를,
도착 시간을 기준으로 최근/평균 수신 간격 및 지터(연속 간격 변화의 평균)를 표시합니다. 전달률은
`수신 / (수신 + 누락 추정)`이며, 순서가 뒤바뀐 frame은 누락으로 계산하지 않고 별도 표시합니다.

자동 검증과 장애·재연결 테스트 절차는 `docs/TESTING_KR.md`에 정리되어 있습니다.

장시간 Transport 안정성 검증은 `run-transport-soak.bat`를 실행합니다. 기본값은 1,000회이며
예를 들어 `run-transport-soak.bat 5000`으로 반복 횟수를 지정할 수 있습니다.

Unreal까지 포함한 전체 통합 검증은 `run-unreal-integration-soak.bat`를 실행합니다. 스크립트가
Unreal 가상장비를 자동 시작하고 전원, 각도, 상태 요청, ACK, Telemetry와 25사이클마다의 재연결을
검증한 다음 자신이 시작한 Unreal 프로세스를 종료합니다. 기본 250사이클이며 첫 번째 인자로
사이클 수, 두 번째 인자로 포트를 지정할 수 있습니다.

## Operating Guide (English)

### Prerequisites

- Install Windows desktop C++ development tools, MFC, and CMake with Visual Studio.
- Unreal Engine 5.8 is expected at `F:\UE_5.8`. If it is installed elsewhere, update the
  `UNREAL_EDITOR` value in `run-unreal-virtual-device.bat` and `run-integration-demo.bat`.
- The default local integration endpoint is `127.0.0.1:5000` over TCP. Allow the Unreal
  executable through the private-network firewall prompt if Windows displays one.

### Start the integrated demo

1. Double-click `run-unreal-virtual-device.bat`, open the Unreal Editor, and press **Play**.
2. In the startup guide, retain port `5000` and press **Start Simulation**.
3. Double-click `run-mfc-host.bat` to build and open DeviceLink Studio.
4. Confirm host `127.0.0.1` and port `5000`.
5. Select an operating mode, then press **Connect**. The selected mode is sent to the
   Unreal virtual device immediately after the TCP connection is established.

### Fixed-camera monitoring mode

1. Select **Fixed Camera Monitoring Mode**.
2. Send **Power On**, then **Initialize**.
3. Enter a Pan target from `-170` to `170°` and a Tilt target from `-45` to `80°`, then send
   **Apply Angle**.
4. Use **Start/Stop Scan** for the sweep demonstration and **Request Status** for immediate telemetry.

### Drone monitoring mode

1. Select **Drone Monitoring Mode** and connect. Unreal changes the EO/IR device to a
   cube-based virtual drone payload.
2. Send **Take Off**. The drone climbs to 10 m above its home location.
3. Enter target `X` and `Y` coordinates in metres, then send **Move To**. The current control
   UI intentionally uses a fixed flight altitude of 10 m.
4. **Return Home** flies to the home location at 10 m; **Land** descends at the current location.
5. Switching modes resets the virtual drone safely to its home ground state.

### Monitoring, faults, and test evidence

- Inspect transmitted/received frames and acknowledgement results in **Received Packets**.
- Inspect device state, fault, current/target orientation, temperature, voltage, and telemetry
  quality in **Real-time Device Status**.
- The fault panel simulates Motor Stall, Over Temperature, Sensor Failure, delayed responses,
  and no response. Return to normal response to recover.
- The default `.dls` scenario automates the fixed-camera workflow and writes a Markdown test
  report. The drone workflow is currently verified through the manual operating procedure.
- Automatic reconnect can recover a dropped endpoint. **Disconnect** intentionally shuts down
  the TCP worker and socket safely.

## 빌드와 자동 테스트

Visual Studio C++ 도구와 CMake가 설치된 PowerShell에서 실행합니다.

~~~powershell
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build --output-on-failure'
~~~

테스트만 다시 실행할 때:

~~~powershell
ctest --test-dir build --output-on-failure
~~~

## 시뮬레이터

~~~powershell
.\build\DeviceLinkSimulator.exe 5000 echo
~~~

두 번째 인자로 다음 모드를 선택할 수 있습니다.

| 모드 | 동작 |
| --- | --- |
| echo | 수신한 유효 프레임을 그대로 반환 |
| delay | 응답을 지연 |
| disconnect | 프레임 수신 뒤 연결 종료 |
| corrupt-crc | CRC가 손상된 응답 프레임을 전송 |
| corrupt-then-echo | CRC가 손상된 프레임 뒤 정상 응답 전송 |
| fragmented | 정상 응답을 두 TCP 조각으로 나눠 전송 |
| telemetry | 연결 뒤 telemetry 프레임을 전송 |

## 계층 구조

~~~
UI
  ↓
Application
  ↓
Core
  ↓
Protocol
  ↓
Transport
  ↓
Infrastructure
~~~

자세한 책임과 worker-to-UI 이벤트 흐름은 docs/ARCHITECTURE.md를 참고하세요.
