# Coding Standard

Language

- C++20

Naming

Class

PascalCase

Member

m_member

Local

camelCase

Constant

kConstant

Interface

IXXXX

---

Never

using namespace

in header files.

---

Prefer

std::unique_ptr

instead of raw pointers.

---

Maximum function length

50 lines.

Split functions when necessary.

---

Header

Forward declaration first.

Avoid unnecessary includes.

---

Every class should have a single responsibility.

---

Avoid giant God Objects.

---

Prefer composition over inheritance.