# Architecture

## 의존성 규칙

각 계층은 아래 방향으로만 의존합니다. 하위 계층은 상위 계층의 타입이나 UI를 알지 못합니다.

~~~
UI → Application → Core → Protocol → Transport → Infrastructure
~~~

## 포트폴리오 시스템 경계

이 프로젝트의 제품 경계는 아래와 같다. 실제 장비는 요구하지 않으며, Unreal 가상 장비를 통해
실장 통합에서 필요한 통신·동시성·장애·시험 자동화 문제를 재현한다.

~~~
Windows MFC 연결 프로그램
  → Application / Core / Protocol / Transport
  → TCP
  → Unreal Engine 가상 EO/IR 장비 + 현장 레벨 + 고장 시나리오
~~~

따라서 제조사 API 프로파일(Axis VAPIX 등)은 실제 제품 연동 주장이 아니라, 가상 장비가 수용할 수
있는 실무형 통신 계약의 예시다. UI와 통신 계층의 설계·검증 대상은 계속 Unreal endpoint다.

| 계층 | 책임 | 대표 구성요소 |
| --- | --- | --- |
| UI | 화면 표시와 UI 스레드 갱신 | MfcDashboardFrame, DashboardModel, DashboardBinding |
| Application | 장비 관리와 의미 단위 작업 조율 | DeviceManager, VirtualGimbalService, ScenarioRunner |
| Core | 연결 상태와 세션 동작 | DeviceSession, ConnectionStateMachine |
| Protocol | 프레이밍, CRC, 검증, 파싱 | PacketFrame, FrameStreamParser |
| Transport | 원시 바이트 전송 | ITransport, TcpTransport |
| Infrastructure | 외부 저장/로그 구현 | TextFileLogger, AsyncLogger, SqliteEventRepository |

## 수신 이벤트 흐름

~~~
TcpTransport receive worker
  → DeviceSession (raw bytes를 프레임으로 변환)
  → DeviceManager / DeviceEventQueue
  → WindowEventBridge (PostMessage)
  → UI thread
  → DashboardBinding / DashboardModel
~~~

worker는 MFC control이나 Dashboard model을 직접 갱신하지 않습니다. WindowEventBridge가
Windows 메시지를 게시하고, UI 스레드가 해당 메시지를 처리할 때 큐를 drain합니다.

송신 성공도 `FrameSent` 이벤트로 발행되어 UI에는 TX로 표시되고 SQLite에는 `frame-sent`로
저장됩니다. Replay 중 송신은 `frame-replayed` 감사 이벤트만 남기고 Replay 원본에는 다시
포함하지 않아 반복 실행 시 기록이 증폭되지 않습니다.

## TCP 수명 주기

- Winsock 초기화와 socket handle은 각각 RAII 객체가 소유합니다.
- Connect는 socket을 non-blocking 모드로 연결하고 `select`로 설정된 timeout까지만 기다린 뒤,
  blocking 모드로 복구하고 수신/송신 worker를 시작합니다.
- Send는 호출자 스레드에서 송신 큐에 바이트를 추가합니다.
- Disconnect는 새 작업을 중단하고 socket shutdown으로 block된 I/O를 깨운 뒤 worker를 join합니다.
- 원격 peer 종료는 transport error로 전달되고, Core 상태 전이 규칙에 따라 Faulted 상태가 됩니다.

`VirtualGimbalService`는 연결 의도, endpoint, 재시도 간격과 최대 횟수를 소유합니다. 별도
`std::jthread`는 Faulted 세션을 정상 Disconnect 상태 전이로 되돌린 다음 재연결합니다. 명시적
Disconnect는 연결 의도를 먼저 제거하고 condition variable을 깨우므로 종료 이후 재연결 경합이
발생하지 않습니다. 서비스 소멸자는 stop 요청과 join 후 마지막 연결을 정리합니다.

Transport는 장비 상태, telemetry 의미, 알람, packet 의미를 해석하지 않습니다.

## ScenarioRunner

ScenarioRunner는 Application 계층의 one-shot 실행기입니다. 다음 step을 순차 실행합니다.

- ConnectScenarioAction
- SendFrameScenarioAction
- DisconnectScenarioAction

각 step에는 실행 전 지연 시간을 줄 수 있습니다. 결과는 future ScenarioResult로 반환하며,
Stop 또는 소멸자는 stop token을 요청하고 worker를 join합니다. 따라서 지연 중 취소도
안전하게 완료됩니다.

FrameReplayRunner는 검증된 PacketFrame과 각 프레임의 지연 시간을 send step으로 바꿔
ScenarioRunner에 위임합니다. 원시 로그 파일의 파싱이나 CRC 검증은 수행하지 않으며, 이후
SQLite repository가 replay source를 제공할 수 있습니다.

## SQLite 이벤트 저장

SqliteEventRepository는 event_log 테이블에 시간, 장비 ID, 분류, 상세 문자열을 저장합니다.
AsyncEventStore는 이 repository를 소유하고 하나의 worker에서 append 요청을 순서대로 처리합니다.
Flush는 대기 중인 저장과 진행 중인 저장이 모두 끝날 때까지 대기하고, Shutdown은 새 요청을
차단한 뒤 남은 요청을 기록하고 worker를 join합니다.

SQLite API는 Infrastructure에만 있으며 UI는 데이터베이스를 직접 호출하지 않습니다.
Application의 DeviceEventPersistence는 DeviceManager observer로 받은 연결 상태, 수신 frame
메타데이터, transport error를 AsyncEventStore에 전달합니다.

`EventLogQueryService`는 UI가 전달한 장비 ID, category, 검색어 조건을 적용하고
`EventLogSummary`만 반환합니다. 조회 전 주입된 prepare callback이 `AsyncEventStore::Flush`를
수행하므로 저장 worker와 조회 시점 사이의 일관성을 확보합니다. 검색 결과는 최근 항목 기준
최대 개수를 유지하며 payload 원문 대신 크기만 UI에 제공합니다.

`OperatorSettingsService`는 MFC 입력값을 독립적인 `OperatorSettings` 모델로 받아
`ITextFileStore`를 통해 저장합니다. 문자열은 percent encoding하여 구분자와 줄바꿈을 보존하고,
version 및 모든 필수 필드의 범위를 확인한 뒤에만 UI에 반환합니다. 실제 파일 교체는
Infrastructure의 `AtomicTextFileStore`가 담당하므로 UI는 파일 API를 직접 호출하지 않습니다.

MFC 호스트는 프로세스 시작 시 Per-Monitor V2 DPI 인식을 활성화합니다. `MfcDashboardFrame`은
96 DPI 기준 논리 좌표를 현재 모니터 배율로 변환하고 `WM_SIZE`에서 컨트롤을 다시 배치합니다.
가로 공간은 두 작업 열에 분배하고 추가 세로 공간은 실시간 목록과 이력 목록에 나누어 주며,
`WM_DPICHANGED`에서는 글꼴을 RAII `CFont` 객체로 재생성한 뒤 운영체제가 제안한 창 영역을
적용합니다. 이는 표시 책임에만 해당하므로 Application 이하 계층에는 DPI 의존성이 없습니다.

`DeviceLinkUnrealSoak`는 UI 없는 외부 통합 Probe입니다. `DeviceManager`, `VirtualGimbalService`,
`TcpTransport`의 정상 Application 경계를 사용해 Unreal 가상장비에 명령을 보내고 ACK와 Telemetry를
검증합니다. Unreal의 `ADeviceLinkPlayerController`는 `-DeviceLinkAutoStart`일 때 시작 위젯을
생성하지 않고 지정 포트에 endpoint를 적용하므로 CI나 장시간 시험에서 사람의 입력이 필요 없습니다.

`IGimbalProtocolAdapter`는 의미 단위 `GimbalCommand`를 장비별 `PacketFrame`으로 바꾸고 ACK 및
Telemetry를 공통 Application 모델로 변환하는 Strategy 경계입니다. 기본 구현인
`DeviceLinkGimbalProtocolAdapter`는 Unreal 가상장비 계약을 담당합니다. `VirtualGimbalService`는
Adapter를 `std::unique_ptr`로 소유하고 생성자 주입받으므로 전역 registry나 singleton 없이 실제
장비 구현으로 교체할 수 있습니다. UI도 서비스의 instance decoder를 사용해 주입된 Adapter를
따르며 Transport는 계속 raw byte만 처리합니다.

장비 고장과 통신 응답 장애는 별도 모델입니다. `InjectFault`는 짐벌 상태 머신의 Motor Stall,
Over Temperature, Sensor Failure를 제어하고 `ConfigureResponse`는 Unreal 응답 adapter의 Normal,
Delayed, NoResponse 정책을 제어합니다. 지연 응답은 Unreal Game Thread의 timer로 예약하므로 TCP
worker를 block하지 않습니다. NoResponse 상태에서도 관리 명령 `ConfigureResponse`는 즉시 ACK하여
운영자가 재시작 없이 안전하게 정상 모드로 복귀할 수 있습니다.

`TelemetryHeartbeatWatchdog`는 Application 계층의 독립 감시 서비스입니다. 연결 직후에는
Waiting, telemetry 수신 시 Healthy, 기본 1.5초 미수신 시 Warning, 3초 미수신 시 Fault로
전이합니다. C++20 `std::jthread`와 stop token으로 주기 평가와 안전 종료를 구현하며, 생성된
상태 이벤트는 기존 처리 파이프라인으로 되돌아가 SQLite 기록·알람·UI event queue를 동일하게
통과합니다. 정상 telemetry 재수신 또는 연결 해제 시 heartbeat 알람은 자동 해제됩니다.
`TelemetryHeartbeatWatchdog::SetOptions`는 mutex 보호 하에 임계값을 교체하고 worker를 깨우므로,
운영 중에도 감시 스레드를 재생성하지 않고 안전하게 적용할 수 있습니다.

`TelemetryQualityService`는 같은 Application 이벤트 처리 경로에서 telemetry frame만 관찰하는
읽기 모델이다. 장비별로 sequence와 도착 시각을 보관해 수신 수, sequence gap 기반 누락 추정,
순서 역전 수, 최근/평균 수신 간격 및 지터를 계산한다. Transport나 Protocol에 telemetry 의미를
추가하지 않으며 UI는 UI thread에서 `DeviceRuntime::TelemetryQuality()`를 조회만 한다. 32-bit
sequence가 크게 뒤로 이동하면 counter reset 또는 out-of-order로 보고 거대한 누락 수를 추정하지
않는다.

`ScenarioReportService`는 ScenarioRunner의 완료·실패·취소 progress를 받아 같은 시나리오 폴더에
Markdown 시험 결과를 원자적으로 저장합니다. 보고서에는 최종 판정, 수행 시간, 실패 단계와 원인,
각 시나리오 명령의 PASS/FAIL/NOT RUN 결과가 들어갑니다. 생성은 worker에서 수행하지만 결과 경로는
기존 `ScenarioProgressQueue`를 통해 UI thread에 전달되므로 MFC controls에 대한 worker 직접 접근은
발생하지 않습니다.

## 검증 전략

- Transport: loopback TCP 서버로 연결, 송신, 수신, 원격 종료 감지와 같은 인스턴스의 재접속을
  검증합니다. 별도 Soak 모드는 이 세션 수명주기를 지정 횟수만큼 반복한 뒤 누적 통계와 큐 정리를
  확인하므로 일반 회귀 테스트 시간과 장시간 안정성 검증을 분리합니다.
- Protocol: CRC, serialize/parse, 스트림 분할, 손상 프레임 뒤 복구를 검증합니다.
- Application: 상태 전이, event FIFO, UI message bridge, TCP 통합, scenario 완료/취소를 검증합니다.
- Infrastructure: 파일 출력과 비동기 로그의 flush/shutdown을 검증합니다.

## 가상 짐벌 제어 경계

`VirtualGimbalService`는 전원, 초기화, 각도, 스캔, 상태 요청, 고장이라는 Application 명령을
DeviceLink frame으로 변환한다. MFC는 protocol 상수나 endian 규칙을 알지 않는다. 반대로
Transport는 짐벌이나 telemetry 의미를 알지 않는다. ACK와 telemetry payload 해석도 이 서비스가
담당하므로 UI는 이미 검증된 장비 상태만 표시한다.

## 다음 단계

- 제조사 사양서 기반 실제 Adapter
- 시험 결과 리포트
- 설치 패키지와 포트폴리오 시연 자료
