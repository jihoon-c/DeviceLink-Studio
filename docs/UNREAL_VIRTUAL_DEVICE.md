# Unreal Virtual Gimbal Device

`UnrealVirtualDevice` is a UE 5.8 project that hosts a simulated EO/IR gimbal on
`127.0.0.1:5000`. It uses the same DeviceLink frame envelope as the Windows host.

## Design

- `AVirtualGimbalDevice` is the composition root and visual adapter.
- `UGimbalDeviceComponent` owns the equipment state machine and motion model.
- `UVirtualDeviceNetworkComponent` bridges the game thread to the TCP worker and
  owns the receive framing buffer.
- `FVirtualDeviceTcpServer` owns non-blocking sockets on an `FRunnable` worker.
- `VirtualDeviceProtocol` owns framing, big-endian conversion, and CRC validation.
- Producer/consumer queues isolate the worker thread from Unreal objects.

The TCP worker never calls an Actor, Component, or protocol parser. It queues raw
connection/data events only. `UVirtualDeviceNetworkComponent::TickComponent`
drains those bytes, invokes `VirtualDeviceProtocol`, and broadcasts complete
messages on the game thread.

## Message contract

All frames use magic `0xD15C`, version `1`, a 14-byte big-endian header, payload,
and CRC16-CCITT (`0xFFFF` initial value).

| Type | Name | Payload |
| --- | --- | --- |
| `0x1001` | Power | `uint8` (`0` off, `1` on) |
| `0x1002` | Initialize | empty |
| `0x1003` | Set Pan/Tilt | `int16 pan`, `int16 tilt`, centidegrees, big-endian |
| `0x1004` | Start Scan | empty |
| `0x1005` | Stop Scan | empty |
| `0x1006` | Get Status | empty |
| `0x1007` | Inject Fault | `uint8` fault (`0` clears) |
| `0x1008` | Configure Response | `uint8 mode`, `uint16 delay ms`; mode `0` normal, `1` delayed, `2` no response |
| `0x1009` | Select Equipment Mode | `uint8` (`0` fixed-camera, `1` drone) |
| `0x1010` | Drone Take Off | empty |
| `0x1011` | Drone Land | empty |
| `0x1012` | Drone Move To | `int16 x`, `int16 y`, `int16 altitude`, decimeters, big-endian |
| `0x1013` | Drone Return Home | empty |
| `0x6001` | Acknowledge | `uint16 command type`, `uint8 result` |
| `0x7001` | Telemetry | state, fault, position, target, temperature, voltage |

Telemetry is published twice per second while a DeviceLink client is connected.
Over-temperature raises the simulated sensor value to 95°C. Sensor failure and
motor stall enter the Fault state and reject motion commands. Delayed response
applies to ACK and telemetry; no-response suppresses both. Configure Response is
always acknowledged immediately so an operator can recover from no-response mode.

## Level

`/Game/Maps/DeviceLinkLab` is the editor and game startup map. It contains one
`AVirtualGimbalDevice` named `VirtualGimbal_01` under the
`DeviceLink/VirtualDevices` outliner folder. The actor is non-spatially loaded so
its TCP endpoint remains alive in World Partition maps without a player streaming
source.

## Operating modes

The Windows console presents the operator with two modes before sending commands.

- **Fixed-camera monitoring** keeps the existing EO/IR gimbal workflow: power,
  initialize, pan/tilt target, scan, and status request.
- **Drone monitoring** switches the same virtual endpoint to a simple quadcopter
  visual. After take-off it accepts X/Y coordinates in metres, moves at a constant
  speed, supports landing, and returns to the home position at 10 m altitude.

This is a virtual equipment scenario only. The control program still talks solely
to the Unreal simulation through the DeviceLink TCP contract.

The lab also contains a lightweight blockout environment made only from Unreal
Engine basic-shape assets. Actors use the `Site_` prefix and are grouped below
`DeviceLink/Site`:

- `Ground`: 30 m yard surface and central gimbal pad
- `Structures`: control building, roof, door, and utility container
- `Safety`: perimeter barriers with an opening on the east side
- `Utilities`: two light-marker poles and a control-building antenna mast
- `Targets`: three differently sized targets along the gimbal's forward axis

## Run and verify

1. Run `run-unreal-virtual-device.bat` from the repository root.
2. Press **Play** in Unreal Editor. The startup guide opens before game input is enabled.
3. Confirm the local binding address, enter a TCP port, and press **시뮬레이션 시작**.
4. Connect the DeviceLink host to the displayed address and port.
5. In the Windows console choose either fixed-camera mode and send Power On,
   Set Pan/Tilt, Get Status; or drone mode and send Take Off, X/Y Move To,
   Return Home/Land.
6. Verify successful `0x6001` acknowledgements, `0x7001` telemetry, gimbal motion,
   and the status-light color.

The automated tests are `DeviceLink.VirtualDevice.Protocol.RoundTrip` and
`DeviceLink.VirtualDevice.Startup.PortValidation`. The runtime integration check
must also close and reconnect a TCP client to verify safe connection teardown and
re-accept behavior.

For unattended integration testing, launch the game with
`-DeviceLinkAutoStart -DeviceLinkPort=5000`. This bypasses the startup widget while
preserving the same endpoint-configuration path. `run-unreal-integration-soak.bat`
builds the headless C++ probe, launches Unreal with these options, validates real
commands, acknowledgements, telemetry, and periodic reconnects, then stops only
the Unreal process that it started.
