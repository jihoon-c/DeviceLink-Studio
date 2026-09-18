# DeviceLink Studio

## Overview

DeviceLink Studio is a portfolio project that demonstrates production-style
equipment-control and communication engineering without physical hardware.
The Windows control application communicates with a virtual EO/IR gimbal
implemented inside Unreal Engine, then verifies the same connection,
telemetry, fault, recovery, logging, and automated-test workflows used by
real equipment integration projects.

The system consists of

- MFC Control Console
- Unreal Virtual Equipment
- Unreal virtual machine / virtual equipment
- Shared Protocol Library

Communication is performed using

- TCP

without any physical hardware. Unreal is the simulated equipment side; the
Windows application is the operator-side integration program.

Virtual serial transport is a possible future adapter and is not part of the current implementation.

---

## Main Features

- Device Connection
- Binary Protocol
- CRC
- State Machine
- Telemetry
- Alarm
- Packet Monitor
- SQLite Logging
- Fault Injection
- Record & Replay
- Scenario Runner

---

## Target

Portfolio project.

Quality is more important than feature count.
