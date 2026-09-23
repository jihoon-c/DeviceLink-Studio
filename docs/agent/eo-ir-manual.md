# DeviceLink EO/IR Virtual Equipment Manual

## Connection and initialization

The Unreal virtual EO/IR listens for DeviceLink TCP frames on its configured
port. Connect before sending any equipment command. A successful TCP connection
only establishes the transport path; command ACK and telemetry provide equipment
level evidence.

## Gimbal angle operation

Absolute pan accepts -170 to 170 degrees. Absolute tilt accepts -45 to 80
degrees. An ACK means that the command was accepted. Movement succeeds only when
the measured telemetry angles enter the requested tolerance before the timeout.

## Telemetry

Telemetry contains equipment state, active fault, measured pan and tilt, target
pan and tilt, temperature, and supply voltage. Test verdicts should cite observed
telemetry rather than infer success from an ACK alone.

## Recovery testing

Fault recovery tests inject one supported simulated fault, confirm the Fault
state and matching fault code, clear the fault, and confirm the Ready state with
fault None. Each observation has a bounded timeout.
