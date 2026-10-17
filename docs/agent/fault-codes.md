# DeviceLink Virtual EO/IR Fault Codes

## MotorStall

MotorStall represents a simulated gimbal motor that cannot follow its commanded
motion. The equipment reports state Fault and fault MotorStall. Reproduce it with
`inject_fault` using `MotorStall`. Clear it with `clear_fault`, then verify state
Ready and fault None through telemetry.

### 원인 가설
- MotorStall은 명령된 짐벌 움직임을 따르지 못하는 가상 고장이다. 실제 모터의 물리적 고장 원인은 이 샘플만으로 확인할 수 없다.

### 확인 절차
- 새 Telemetry에서 state=Fault와 fault=MotorStall이 함께 관측되는지 확인한다.
- 장비 ID와 관측 시각, 고장 전후 이벤트를 비교한다. ACK만으로 정상 여부를 판단하지 않는다.

### 복구 제안
- 사용자가 가상 고장 해제를 요청한 경우 clear_fault를 제안한다.
- 고장 해제 후 새 Telemetry에서 state=Ready와 fault=None을 확인한다.

## OverTemperature

OverTemperature represents equipment temperature protection. The simulator
reports state Fault and fault OverTemperature until the injected fault is
cleared.

### 원인 가설
- OverTemperature는 주입된 가상 온도 보호 고장이다. 고장 코드만으로 실제 과열이나 보호 임계값 초과를 확정할 수 없다.

### 확인 절차
- 새 Telemetry의 fault=OverTemperature와 temperature_celsius를 함께 확인한다. 이 샘플에는 실제 과열 임계값이 정의되어 있지 않다.
- 장비 ID와 관측 시각, 고장 전후 이벤트를 비교한다. ACK만으로 정상 여부를 판단하지 않는다.

### 복구 제안
- 사용자가 가상 고장 해제를 요청한 경우 clear_fault를 제안한다.
- 고장 해제 후 새 Telemetry에서 state=Ready와 fault=None을 확인한다.

## SensorFailure

SensorFailure represents loss or invalid output from an EO/IR sensor path. The
simulator reports state Fault and fault SensorFailure until the injected fault
is cleared.

### 원인 가설
- SensorFailure는 EO/IR 센서 경로의 손실 또는 잘못된 출력을 나타내는 가상 고장이다. 실제 센서 하드웨어의 고장 원인은 미확인이다.

### 확인 절차
- 새 Telemetry에서 state=Fault와 fault=SensorFailure가 함께 관측되는지 확인한다.
- 장비 ID와 관측 시각, 고장 전후 이벤트를 비교한다. 실제 센서 교체 여부를 판단할 근거는 이 샘플에 없다.

### 복구 제안
- 사용자가 가상 고장 해제를 요청한 경우 clear_fault를 제안한다.
- 고장 해제 후 새 Telemetry에서 state=Ready와 fault=None을 확인한다.

## Fault code None

None means that no simulated fault is active. It is used by `clear_fault` and is
expected together with the Ready state after successful recovery.
