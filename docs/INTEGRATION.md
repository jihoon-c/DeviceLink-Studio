# Application 통합 가이드

## 조립 루트

`DeviceRuntime`은 headless Application 조립 루트다. 다음 객체를 한 수명 범위에서 소유하고
`DeviceManager` 이벤트를 처리 파이프라인에 자동 연결한다.

```text
DeviceManager event
  -> DeviceEventProcessingService
       -> AsyncEventStore -> SqliteEventRepository
       -> CommunicationAlarmService
       -> TelemetryService
       -> TelemetryHeartbeatWatchdog
```

예시는 다음과 같다.

```cpp
DeviceLink::Application::DeviceRuntime runtime(
    std::make_unique<DeviceLink::Infrastructure::SqliteEventRepository>(L"events.sqlite"),
    std::vector<std::uint16_t>{0x7001},
    500,
    [] { return CurrentUnixMilliseconds(); },
    {.warningAfter = std::chrono::milliseconds{1500},
     .faultAfter = std::chrono::milliseconds{3000},
     .pollInterval = std::chrono::milliseconds{100}});

auto& devices = runtime.Devices();
devices.RegisterDevice("simulator-a");
devices.AttachTransport("simulator-a",
    std::make_unique<DeviceLink::Transport::TcpTransport>());
devices.ConnectDevice("simulator-a", "127.0.0.1", 5000);
```

`DeviceRuntime`보다 `DeviceManager` 또는 UI binding이 오래 살아서는 안 된다. Runtime은 파괴 시
Manager를 먼저 종료하고, 그 뒤에 이벤트 처리·저장 worker를 파괴하도록 멤버 수명을 구성한다.

## UI 연결 원칙

MFC 코드는 SQLite나 Winsock을 직접 호출하지 않는다.

1. `runtime.Devices().Events()`를 기존 `WindowEventBridge`에 연결한다.
2. wakeup callback에서는 `PostMessage`만 호출한다.
3. UI thread에서 `HandleMessage()`로 DeviceEventQueue를 drain한다.
4. Packet Monitor는 `EventLogQueryService`의 최근 이벤트 요약을 사용한다.
5. Alarm View는 `runtime.Alarms().ActiveAlarms()`를, Telemetry View는
   `runtime.Telemetry().RecentSamples()`, `runtime.HeartbeatWatchdog().StatusFor(deviceId)`와
   `runtime.TelemetryQuality().MetricsFor(deviceId)`를
   UI thread에서 조회한다.

시나리오 progress는 별도 `ScenarioProgressQueue`에 넣고 같은 방식으로 Windows 메시지 루프에
연결한다.

## 운영 정리

- 종료 전 `runtime.FlushEventLog()`로 비동기 기록을 비운다.
- 오래된 로그는 `runtime.PruneEventLogBefore(timestamp)`로 worker FIFO에서 정리한다.
- TCP 송신 큐 기본 상한은 1 MiB이며 `TcpTransportOptions`로 조정한다.
- `TcpTransport::GetStatistics()`로 송수신 바이트·거부된 전송·큐 상태를 조회한다.

## MFC ↔ Unreal 조립

`MfcHostMain.cpp`이 `DeviceRuntime`, `TcpTransport`, `VirtualGimbalService`를 조립한다.
MFC는 `VirtualGimbalService`의 의미 단위 API만 호출하고 프레임 바이트를 직접 만들지 않는다.
수신된 ACK와 telemetry 역시 서비스의 검증된 decoder를 거쳐 표시한다.

`OperatorWorkflowService`는 ScenarioRunner, FrameReplayRunner, 파일 codec과 progress queue를
묶는다. 작업 worker는 progress를 queue에 넣고 MFC는 별도의 `PostMessage`를 받은 UI thread에서
화면을 갱신한다. Scenario 완료·실패·취소 시에는 `ScenarioReportService`가 동일한 file store를
통해 `<scenario>.report.<완료시각>.md`를 원자적으로 만들고, 이전 결과를 덮어쓰지 않는다. Report
path 역시 progress에 넣어 UI가 표시한다.
Replay 시작 전 비동기 이벤트 저장소를 flush하여 완전한 TX 기록을 읽는다.

Unreal은 `AVirtualGimbalDevice`가 조립 루트이며 TCP worker가 raw byte event만 queue에 넣는다.
Game Thread의 network component가 protocol을 파싱하고 장비 component에 명령을 전달한다.

무인 통합 시험에서는 Unreal에 `-DeviceLinkAutoStart -DeviceLinkPort=<port>`를 전달한다.
`DeviceLinkUnrealSoak`가 동일한 Application 조립 경계를 통해 실제 명령과 응답을 검증하며,
`run-unreal-integration-soak.bat`가 빌드·프로세스 시작·검증·정리를 한 번에 수행한다.

실제 장비 protocol은 `IGimbalProtocolAdapter` 구현으로 추가한다. 서비스에 Adapter를 생성자
주입하고 해당 Adapter가 제공하는 Telemetry message type을 `DeviceRuntime` 구성에 전달하면 된다.
자세한 절차와 안전 확인 항목은 `DEVICE_ADAPTER_GUIDE_KR.md`를 따른다.

Axis VAPIX는 실제 장비 연결 기능이 아니라 Unreal 가상 장비에 추가할 HTTP PTZ 프로파일이다.
`AxisVapixCommandAdapter`는 홈/절대 PTZ/연속 이동/정지/위치 조회의 요청·응답 계약을 검증한다.
후속 Unreal endpoint는 이 계약을 수용해 동일 짐벌의 동작과 상태를 반환하며, Windows 프로그램은
기존과 같이 연결·수신·장애·시험 흐름을 검증한다. 사용자 암호를 요구하거나 로그·SQLite·리포트에
저장하는 실장 인증 기능은 현재 포트폴리오 범위가 아니다.

Heartbeat 임계값은 `runtime.SetHeartbeatOptions()`로 변경한다. MFC 운용 설정은 Warning을
100~600,000 ms, Fault를 Warning보다 큰 값으로 제한하며, `TelemetryHeartbeatWatchdog`는 worker
재생성 없이 새 설정을 반영한다.

Telemetry 품질은 `TelemetryQualityService`가 계산한다. `receivedFrameCount`는 유효 telemetry
event 수, `estimatedMissingFrameCount`는 sequence gap, `deliveryRatePercent`는 수신과 추정 누락의
비율이다. `averageJitterMilliseconds`는 연속 도착 간격 차이의 평균이므로 네트워크/시뮬레이터의
시간 변동을 빠르게 확인할 수 있다. sequence reset 정책이 장비별로 확정되면 해당 reset을 명시적으로
판별하는 Adapter 메타데이터를 후속 추가한다.

## 남은 외부 범위

- 설치 프로그램 또는 portable 배포 패키지
- 포트폴리오용 스크린샷과 짧은 통합 데모 영상
- 제조사 사양서 기반 Adapter와 simulator fault injection 세분화
