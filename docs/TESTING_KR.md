# DeviceLink Studio 테스트 방법

## 한 번에 전체 검증

프로젝트 최상단의 `run-all-tests.bat`를 더블클릭한다.

스크립트가 MFC 호스트를 포함해 빌드한 뒤 CTest를 실행한다. 성공하면
`[PASS] All automated tests passed`를 표시하고 `DeviceLink Studio` 창을 연다.

## 자동 테스트 범위

| 테스트 | 확인 기능 |
| --- | --- |
| `TcpTransportTests` | Winsock 연결, connect timeout, raw byte 송수신, 상대 강제 종료 감지·재접속, 안전 종료, 송신 대기열 한도 |
| `ProtocolTests` | 프레임 직렬화·역직렬화, CRC, 분할 수신, 손상 프레임 복구 |
| `ApplicationTests` | 장비 세션 상태 전이, TCP 연동, 이벤트 처리, DashboardBinding, 시나리오, SQLite 이벤트 저장 |
| `InfrastructureTests` | 비동기 로그·이벤트 저장, SQLite 조회·정리, 원자적 텍스트 파일 저장 |

`ApplicationTests`에는 Unreal 짐벌 명령 7종의 프레임/sequence/빅엔디언 페이로드와
ACK 및 telemetry 디코딩, 송신 명령 Replay 영속화, Replay 기록 증폭 방지,
자동화 작업 중복 실행 차단과 취소 진행 이벤트, 자동 재접속 성공·비활성화·최대 횟수 제한
및 운영 설정 round-trip·손상 파일 거부 검증도 포함된다. 가짜 장비 Adapter를 생성자 주입해
명령 encoding, response decoding과 message type이 기본 DeviceLink 규격과 독립적으로 교체되는지도
확인한다.

## Transport Soak 테스트

프로젝트 최상단의 `run-transport-soak.bat`를 더블클릭하면 Transport 테스트만 빌드한 뒤
loopback 서버의 강제 연결 종료, client 감지, 같은 Transport 인스턴스 재접속, 송수신을
기본 1,000회 반복한다. 완료 시 누적 송수신 바이트, 빈 송신 큐, 종료 알림 횟수를 검증한다.

반복 횟수는 명령 프롬프트에서 지정할 수 있다.

```bat
run-transport-soak.bat 5000
```

일반 CTest에는 5회 장애 복구 회귀 시험만 포함해 빌드 검증 시간을 짧게 유지한다.

## Unreal 전체 통합 Soak 테스트

`run-unreal-integration-soak.bat`를 더블클릭하면 다음 작업을 자동 수행한다.

1. C++20 `DeviceLinkUnrealSoak` Probe를 빌드한다.
2. Unreal 게임을 `-DeviceLinkAutoStart -DeviceLinkPort=5000` 무인 모드로 실행한다.
3. 서버가 준비될 때까지 최대 60초 기다린다.
4. 전원 ON, Pan/Tilt 변경, 상태 요청의 ACK 성공과 유효한 Telemetry 범위를 검증한다.
5. 25사이클마다 연결을 정상 해제하고 같은 Unreal 서버로 다시 연결한다.
6. 전원 OFF 및 안전 해제 후 스크립트가 시작한 Unreal 프로세스만 종료한다.

기본값은 250사이클, Port 5000이다. 사이클과 포트를 바꾸려면 다음처럼 실행한다.

```bat
run-unreal-integration-soak.bat 1000 5510
```

실패 시 누락된 ACK, Telemetry timeout, 범위를 벗어난 각도·전압, 연결 오류 중 원인을 출력하고
0이 아닌 종료 코드를 반환한다.

## MFC ↔ Unreal 전체 통합 확인

빠른 데모는 `run-integration-demo.bat` 하나로 두 프로그램을 실행할 수 있다. 코드와 레벨을
편집하면서 검증할 때는 아래처럼 각각 실행한다.

1. `run-unreal-virtual-device.bat`를 더블클릭한다.
2. Unreal Editor에서 **Play**를 누르고 시작 안내의 Port를 `5000`으로 둔 채
   **시뮬레이션 시작**을 누른다.
3. `run-mfc-host.bat`를 더블클릭한다.
4. MFC의 Host `127.0.0.1`, Port `5000`을 확인하고 **연결**을 누른다.
5. 연결 상태가 `연결됨`으로 바뀌고 `TELEMETRY` 항목이 0.5초마다 추가되는지 확인한다.
6. 다음 표의 순서로 기능을 확인한다.

### 두 PC LAN 통합 시험

1. 데스크톱과 노트북을 같은 개인 네트워크에 연결한다.
2. 데스크톱에서 `setup-lan-firewall.bat`를 한 번 실행한다.
3. 데스크톱에서 `run-unreal-lan-device.bat`를 실행하고 표시된 IPv4 주소를 기록한다.
4. 노트북에서 MFC를 실행해 Host에 데스크톱 IPv4, Port에 `5000`을 입력한다.
5. 연결 후 ACK와 0.5초 Telemetry 수신을 확인한다.
6. 조이스틱을 움직여 노트북의 목표 각도, TX/RX 목록, 데스크톱 Unreal 짐벌 움직임을 비교한다.
7. 데스크톱 Unreal을 종료해 MFC의 통신 오류·Heartbeat Fault·자동 재접속을 확인한다.
8. Unreal을 같은 명령으로 다시 실행해 연결 복구를 확인한다.

LAN 실행은 `-DeviceLinkAllowLan`이 지정된 경우에만 `0.0.0.0`으로 바인딩된다. 옵션이 없으면
`127.0.0.1` 로컬 전용이므로 기존 단일 PC 시험의 보안 범위가 유지된다. Windows 방화벽 규칙도
Public이 아닌 Private 프로필에만 적용한다.

| 조작 | MFC 확인 | Unreal 확인 |
| --- | --- | --- |
| 전원 ON | `0x1001` ACK SUCCESS | 상태등이 Ready 색으로 변경 |
| 초기화 | `0x1002` ACK SUCCESS | 상태가 Initializing을 거쳐 Ready |
| Pan 45 / Tilt 10, 각도 적용 | `0x1003` ACK SUCCESS, 현재/목표 각도 갱신 | 짐벌이 목표 각도로 이동 |
| 스캔 시작/정지 | `0x1004`/`0x1005` ACK SUCCESS | 좌우 스캔 시작/정지 |
| 상태 요청 | `0x1006` ACK와 즉시 telemetry | 현재 상태 전송 |
| Motor Stall 적용/해제 | Motor Stall/None telemetry | Fault 상태등과 장비 상태 변경 |
| Over Temperature 적용 | 온도 약 95°C와 Fault telemetry | 과열 Fault 상태 표시 |
| Sensor Failure 적용 | Sensor Failure telemetry | 이동 명령 거부와 Fault 상태 표시 |
| 응답 지연 1000 ms | ACK/Telemetry가 약 1초 뒤 표시 | 연결은 유지되고 응답만 지연 |
| 무응답 → 정상 응답 | ACK/Telemetry 중단 후 다시 수신 | 복구 설정 명령은 즉시 처리 |
| 드론 관제 모드 → 이륙 | `0x1009`, `0x1010` ACK SUCCESS | 큐브 기반 드론 기체가 10 m까지 상승 |
| X=20 / Y=-10, 좌표 이동 | `0x1012` ACK SUCCESS | 드론이 현장 기준 X=20 m, Y=-10 m로 이동 |
| 귀환 → 착륙 | `0x1013`, `0x1011` ACK SUCCESS | 드론이 홈 상공으로 귀환 후 지상에 착륙 |

연결 직후 실시간 장비 상태의 `Quality` 행에서 `RX`가 0.5초 telemetry마다 증가하고, `Delivery`가
정상 흐름에서는 100.0%인지 확인한다. 지연 응답을 적용하면 최근/평균 Interval과 Jitter가 커지며,
순서 역전 또는 sequence gap을 재현하는 장비에서는 Missing 또는 Out-of-order가 별도 증가한다.
이 지표는 장비 sequence 계약을 전제로 한 운용 관측값이며 packet protocol 자체를 해석하지 않는다.

## 장비·통신 장애 시나리오 확인

1. 연결 후 장비 고장 목록에서 `Over Temperature`를 선택하고 **고장 주입**을 누른다.
2. 상태가 Fault, 고장이 Over Temperature, 온도가 약 95°C로 표시되는지 확인한다.
3. `Sensor Failure`를 적용하고 Pan/Tilt 명령이 실패하는지 확인한 뒤 **고장 해제**를 누른다.
4. 응답 목록에서 `응답 지연`, 지연값 `1000`을 선택해 **응답 설정**을 누른다.
5. 상태 요청 ACK와 Telemetry가 약 1초 늦게 도착하는지 확인한다.
6. `무응답`을 적용해 ACK와 Telemetry가 중단되는지 확인한다.
7. 약 1.5초 후 Heartbeat가 `Warning`, 약 3초 후 `Fault`가 되고 CRITICAL 알람이 생기는지 확인한다.
8. `정상 응답`을 적용해 다음 Telemetry 수신 시 `Healthy`로 복구되고 heartbeat 알람이 자동
   해제되는지 확인한다.
9. 이벤트 종류에서 `Telemetry Heartbeat`를 선택해 Waiting/Warning/Fault/Healthy 전이가
   SQLite 이력에 기록됐는지 확인한다.
10. 전체 자동 시연은 `scenarios\unreal-gimbal-fault-demo.dls`를 실행한다.

## Heartbeat 임계값 설정 확인

1. 오른쪽 하단 `Warning ms`, `Fault ms`에 각각 `500`, `1200`을 입력하고 **Heartbeat 적용**을 누른다.
2. 무응답을 적용했을 때 약 0.5초 후 Warning, 약 1.2초 후 Fault가 되는지 확인한다.
3. Fault에 Warning 이하 값 또는 Warning에 `100` 미만 값을 입력하면 설정이 거부되는지 확인한다.
4. 프로그램을 종료하고 다시 실행해 적용한 값이 유지되는지 확인한다.

장애 설정은 안전을 위해 운영 설정 파일에 저장하지 않으며 Unreal을 다시 실행하면 정상 응답과
고장 없음 상태에서 시작한다.

## 시나리오·Replay·알람 확인

1. 연결된 상태에서 기본 경로 `..\scenarios\unreal-gimbal-demo.dls`를 확인하고
   **시나리오 실행**을 누른다.
2. 단계 표시가 `0 / 6`부터 `6 / 6` 완료까지 갱신되고 Unreal 짐벌이 순서대로 동작하는지 본다.
3. 완료 상태 아래에 `[리포트]` 경로가 표시되고, 시나리오 폴더에
   `unreal-gimbal-demo.report.<완료시각>.md`가 생성되는지 확인한다.
4. 리포트에서 최종 PASS, 실행 시간, 6개 단계의 PASS 결과와 명령 설명이 기록됐는지 확인한다.
5. 실행 중 **작업 중지**를 누르면 Cancelled 상태와 `NOT RUN` 단계가 리포트에 남는지 확인한다.
6. 수동 명령 또는 시나리오를 한 번 실행한 뒤 **송신 기록 Replay**를 누른다.
7. Replay가 TX 명령만 재전송하고 ACK/Telemetry RX 프레임은 장비 명령으로 보내지 않는지 확인한다.
8. Unreal Play를 중지해 통신 알람을 만든 뒤 오류 목록의 `ALARM` 행을 선택하고
   **선택 알람 확인**을 눌러 활성 알람에서 제거되는지 확인한다.

## SQLite 이벤트 이력 확인

1. 수동 명령과 시나리오를 실행한 뒤 하단의 Device ID를 `unreal-gimbal-01`로 둔다.
2. 이벤트 종류를 `전체 종류`로 선택하고 **이력 새로고침**을 누른다.
3. 연결 상태, TX, RX, Replay, 오류 행이 시간순으로 표시되는지 확인한다.
4. `송신 프레임`을 선택하면 `frame-sent`만 남는지 확인한다.
5. 검색어에 `type=4097` 또는 `connected`를 입력해 대소문자 구분 없이 필터되는지 확인한다.
6. 장비 ID를 비우면 모든 장비가, `unreal-gimbal-01`을 입력하면 해당 장비만 표시되는지 확인한다.

조회 버튼은 먼저 비동기 이벤트 저장 큐를 flush한 뒤 별도 Application 서비스를 통해
SQLite를 읽는다. MFC UI는 SQLite API를 직접 호출하지 않는다.

## 운영 설정 복원 확인

1. Host/Port, 자동 재접속, 시나리오 경로와 이력 필터를 기본값과 다르게 변경한다.
2. MFC를 `Alt + F4`로 정상 종료한다.
3. 다시 실행해 모든 입력값이 복원되는지 확인한다.
4. `DeviceLinkStudio.settings`가 실행 파일 작업 폴더에 생성되는지 확인한다.
5. 설정 파일 일부를 손상시키고 실행했을 때 assertion 없이 기본값으로 시작하는지 확인한다.

설정은 임시 파일 작성 후 원자적 교체 방식으로 저장되며 비정상적으로 중간까지 기록된 파일을
정상 설정으로 받아들이지 않는다.

## DPI 및 창 크기 확인

1. MFC 창의 가장자리를 드래그해 최소 크기 제한과 최대화 동작을 확인한다.
2. 창을 세로로 늘렸을 때 패킷·오류 목록과 SQLite 이력 목록이 겹치지 않고 확장되는지 확인한다.
3. Windows 디스플레이 배율을 100%, 125%, 150%로 바꾸거나 배율이 다른 모니터 사이로 창을 옮긴다.
4. 제목, 버튼, 입력창, 목록의 글자가 선명하며 잘리거나 서로 겹치지 않는지 확인한다.
5. DPI 전환 후에도 연결과 명령 버튼이 정상 동작하는지 확인한다.

UI 배치는 운영체제 메시지에 의존하므로 자동 테스트 대신 MFC 호스트 전체 빌드와 위 수동
검증으로 확인한다. 통신·프로토콜·Application 동작은 기존 CTest가 회귀 검증한다.

## 안전 종료 및 복구 확인

1. 통신 중 MFC를 `Alt + F4`로 닫아 assertion 없이 종료되는지 확인한다.
2. 다시 MFC를 열어 재연결되는지 확인한다.
3. 통신 중 Unreal의 Play를 중지하면 MFC가 `통신 오류`와 alarm을 표시하는지 확인한다.
4. MFC에서 **연결 해제**를 누른 뒤 Unreal Play를 다시 시작하고 재연결한다.

## 자동 재접속 확인

1. MFC의 **자동 재접속**을 체크하고 Unreal을 실행하지 않은 상태에서 연결한다.
2. 최초 실패 뒤 `재접속 n / 5` 표시가 증가하는지 확인한다.
3. 5회가 되기 전에 Unreal 시뮬레이션을 시작하면 자동으로 `연결됨`이 되는지 확인한다.
4. 연결된 상태에서 Unreal Play를 중지했다가 다시 시작해 원격 종료 복구를 확인한다.
5. 자동 재접속을 끈 뒤 Unreal을 중지하면 추가 연결 시도가 없는지 확인한다.
6. **연결 해제**를 누르면 예약된 재접속이 취소되는지 확인한다.

정상 종료 시 `DeviceLinkStudio.sqlite`에 이벤트 로그가 flush된다. 자동 테스트는 사용자 DB를
건드리지 않고 loopback TCP와 임시 SQLite 파일만 사용한다.
