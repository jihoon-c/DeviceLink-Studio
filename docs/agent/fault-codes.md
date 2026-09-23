# DeviceLink Virtual EO/IR Fault Codes

## MotorStall

MotorStall represents a simulated gimbal motor that cannot follow its commanded
motion. The equipment reports state Fault and fault MotorStall. Reproduce it with
`inject_fault` using `MotorStall`. Clear it with `clear_fault`, then verify state
Ready and fault None through telemetry.

## OverTemperature

OverTemperature represents equipment temperature protection. The simulator
reports state Fault and fault OverTemperature until the injected fault is
cleared.

## SensorFailure

SensorFailure represents loss or invalid output from an EO/IR sensor path. The
simulator reports state Fault and fault SensorFailure until the injected fault
is cleared.

## Fault code None

None means that no simulated fault is active. It is used by `clear_fault` and is
expected together with the Ready state after successful recovery.
