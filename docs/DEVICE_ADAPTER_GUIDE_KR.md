# 실제 장비 Protocol Adapter 추가 가이드

## 목적

`IGimbalProtocolAdapter`는 장비의 의미 단위 명령과 제조사별 wire protocol을 분리한다.
UI, 재접속 정책, 장비 세션, TCP worker는 Adapter를 알 필요가 없으며 실제 장비 사양이 바뀌어도
해당 구현만 교체할 수 있다.

## 구현할 계약

새 Adapter는 다음 책임만 가진다.

- `GimbalCommand`를 `PacketFrame`으로 인코딩
- 장비 ACK payload를 `VirtualGimbalAcknowledgement`로 변환
- 장비 상태 payload를 `VirtualGimbalTelemetry`로 변환
- ACK 및 Telemetry message type 제공
- 길이, 범위, enum 값과 endian 검증

소켓 연결, thread 생성, 자동 재접속, UI 갱신은 Adapter 책임이 아니다. Transport에서 packet을
파싱하거나 Adapter에서 MFC control을 접근해서는 안 된다.

## 조립 방법

실제 장비 구현이 `VendorGimbalProtocolAdapter`라면 조립 루트에서 다음처럼 주입한다.

```cpp
auto adapter = std::make_unique<VendorGimbalProtocolAdapter>(configuration);
const auto telemetryType = adapter->TelemetryMessageType();

DeviceRuntime runtime(repository, {telemetryType}, 500, clock);
runtime.Devices().RegisterDevice("vendor-gimbal-01");
runtime.Devices().AttachTransport(
    "vendor-gimbal-01", std::make_unique<TcpTransport>());

VirtualGimbalService service(
    runtime.Devices(),
    "vendor-gimbal-01",
    ReconnectPolicy{},
    std::move(adapter));
```

현재 `DeviceLinkGimbalProtocolAdapter`가 완전한 기본 구현 예제다. Application 테스트의
`TestGimbalProtocolAdapter`는 가짜 message type을 주입해 서비스가 기본 protocol 상수에
결합되지 않았음을 검증한다.

## 실제 장비 투입 전 확인 항목

1. 제조사 사양서의 byte order, signed 범위, scaling 단위를 명시한다.
2. 정상 ACK뿐 아니라 NACK, 잘못된 길이, 알 수 없는 enum을 단위 테스트한다.
3. 캡처한 golden packet을 fixture로 두고 양방향 변환을 검증한다.
4. 실제 장비 없이 재현할 수 있는 simulator 응답 세트를 만든다.
5. 실제 장비 연결 시험에서는 비상 정지·limit·timeout 정책을 별도로 승인받는다.

특정 제조사 사양서가 제공되기 전에는 명령 코드나 안전 동작을 추정해서 구현하지 않는다.
