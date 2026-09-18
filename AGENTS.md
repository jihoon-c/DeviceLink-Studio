# DeviceLink Studio - AI Development Guide

## Project

DeviceLink Studio is a Windows C++20 application that provides
a protocol-agnostic equipment communication and SILS-like testing
environment.

The project is NOT a simple monitoring application.

The primary goal is to demonstrate architecture,
communication design, multithreading and test automation.

Target portfolio:

- MFC
- C++
- Simulator
- Defense
- Equipment Control
- Industrial Software

---

# Architecture

Always keep this dependency direction.

UI

↓

Application

↓

Core

↓

Protocol

↓

Transport

↓

Infrastructure

No reverse dependency is allowed.

---

# Core Principles

- Single Responsibility
- Interface First
- RAII
- Smart Pointer
- Thread Safety
- Testability

---

# Forbidden

Never

- call Winsock directly from UI
- call SQLite directly from UI
- parse packets inside Transport
- update UI from worker threads
- use global variables
- use raw new/delete
- use singleton unless explicitly required

---

# Transport

Transport only transfers raw bytes.

Transport does NOT know

- Device State
- Packet Meaning
- Alarm
- Telemetry

---

# Protocol

Protocol is responsible for

- framing
- parsing
- serialization
- CRC
- validation

---

# UI

UI only displays data.

Business logic belongs to Application layer.

---

# Thread

Worker Thread

↓

Event Queue

↓

PostMessage

↓

UI Thread

Never access MFC Controls directly from worker threads.

---

# Testing

Every important feature should be testable.

Protocol should have unit tests.

State machine should have unit tests.

Transport should be independently testable.

---

# Commit Philosophy

Small changes.

One purpose per commit.

Always keep build green.