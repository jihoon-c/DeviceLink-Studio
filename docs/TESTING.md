# DeviceLink Studio 테스트 가이드

## 전체 빌드와 회귀 테스트

Visual Studio C++ 도구 집합과 CMake가 설치된 PowerShell에서 다음을 실행한다.

```powershell
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --clean-first && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build --output-on-failure'
```

테스트 안정성을 반복 확인할 때는 다음을 사용한다.

```powershell
ctest --test-dir build --output-on-failure --repeat until-fail:10
```

## 테스트 범위

| 대상 | 검증 내용 |
| --- | --- |
| `TcpTransportTests` | Winsock 연결, 송신/수신, peer 종료 감지와 재접속, 대기열 용량 제한 |
| `ProtocolTests` | 직렬화, CRC, 스트림 분할, 손상 프레임 복구 |
| `ApplicationTests` | 상태 머신, TCP 통합, 다중 장비, 반복 재연결, 시나리오, SQLite 재생 |
| `InfrastructureTests` | 비동기 로깅/저장, SQLite 이벤트, 원자적 텍스트 파일 교체 |

Transport 장애 복구를 길게 반복하려면 `run-transport-soak.bat`를 실행한다. 기본 1,000회이며
첫 번째 인자로 반복 횟수를 지정할 수 있다. 일반 CTest에는 빠른 5회 회귀 시험만 포함된다.

## 콘솔 시뮬레이터 수동 시험

```powershell
.\build\DeviceLinkSimulator.exe 5000 echo
```

두 번째 인수로 `echo`, `delay`, `disconnect`, `corrupt-crc`, `corrupt-then-echo`,
`fragmented`, `telemetry`를 선택할 수 있다.

## 시나리오 파일 형식

각 행은 `지연시간(ms) 명령 인수...` 형식이다. 빈 행과 `#`로 시작하는 주석 행은 무시한다.

```text
10 CONNECT device-a 127.0.0.1 5000
20 SEND device-a 33024 33 ABCD
0 DISCONNECT device-a
```

- `CONNECT`: 장비 ID, 호스트, 1~65535 포트
- `SEND`: 장비 ID, 16비트 message type, 32비트 sequence, 짝수 길이 hexadecimal payload (`-`는 빈 payload)
- `DISCONNECT`: 장비 ID

`ScenarioFileService`는 저장 시 원자적 파일 교체를 사용하고, 로드 실패 시 파일 오류 또는
파싱 오류 행 번호를 호출자에게 반환한다.

## UI 스레드 안전성

통신 수신 worker와 시나리오 worker는 UI 컨트롤을 직접 갱신하지 않는다. 각각 `DeviceEventQueue`와
`ScenarioProgressQueue`에 이벤트를 넣는다. Windows UI는 wakeup callback에서 `PostMessage`만 호출하고,
UI 스레드에서 큐를 drain해야 한다.
