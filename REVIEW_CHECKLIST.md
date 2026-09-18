# Review Checklist

Architecture

□ Dependency direction preserved

□ No circular dependency

□ Layer separation maintained

---

Memory

□ RAII

□ Smart Pointer

□ No Leak

---

Thread

□ No race condition

□ Safe shutdown

□ Thread Join

---

Transport

□ No Packet Parsing

□ Raw Bytes only

---

Protocol

□ CRC

□ Invalid Packet

□ Partial Packet

□ Combined Packet

---

UI

□ No worker thread UI access

---

Build

□ Build Success

□ Warning reviewed

---

Tests

□ Unit Test Added

□ Existing Tests Passed