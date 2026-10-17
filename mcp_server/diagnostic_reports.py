"""Versioned JSON records and Markdown exports for repeatable diagnostics."""

from __future__ import annotations

import json
from pathlib import Path
import re
import threading
import uuid
from typing import Any


class DiagnosticReports:
    def __init__(self, directory: Path) -> None:
        self.directory = directory.resolve()
        self._lock = threading.Lock()

    def _path(self, diagnosis_id: str, suffix: str) -> Path:
        if not re.fullmatch(r"[0-9a-f]{32}", diagnosis_id):
            raise ValueError("diagnosis_id must be a 32-character hexadecimal identifier")
        return self.directory / (diagnosis_id + suffix)

    def load(self, diagnosis_id: str) -> dict[str, Any] | None:
        path = self._path(diagnosis_id, ".json")
        with self._lock:
            return self._load(path, diagnosis_id)

    @staticmethod
    def _load(path: Path, diagnosis_id: str) -> dict[str, Any] | None:
        if not path.exists():
            return None
        if path.stat().st_size > 2 * 1024 * 1024:
            raise ValueError("Diagnostic report exceeds 2 MiB limit")
        record = json.loads(path.read_text(encoding="utf-8"))
        if (not isinstance(record, dict) or record.get("schema_version") != 1 or
            not isinstance(record.get("diagnosis"), dict) or
            record["diagnosis"].get("diagnosis_id") != diagnosis_id or
            not isinstance(record.get("verifications"), list)):
            raise ValueError("Invalid diagnostic report")
        baseline = record["diagnosis"]
        if (not isinstance(baseline.get("device_id"), str) or
            not isinstance(baseline.get("observed_at_unix_ms"), int) or
            not isinstance(baseline.get("telemetry"), dict)):
            raise ValueError("Invalid diagnostic baseline")
        return record

    @staticmethod
    def _replace(path: Path, text: str) -> None:
        temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
        try:
            temporary.write_text(text, encoding="utf-8")
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)

    def save(self, result: dict[str, Any], *, verification: bool = False) -> dict[str, Any]:
        diagnosis_id = result["diagnosis_id"]
        path = self._path(diagnosis_id, ".json")
        markdown = self._path(diagnosis_id, ".md")
        with self._lock:
            if verification:
                record = self._load(path, diagnosis_id)
                if record is None:
                    raise ValueError("Diagnostic report is unavailable")
                record["verifications"] = (record["verifications"] + [result])[-50:]
            else:
                record = {"schema_version": 1, "diagnosis": result, "verifications": []}
            serialized = json.dumps(record, ensure_ascii=False, indent=2)
            if len(serialized.encode("utf-8")) > 2 * 1024 * 1024:
                raise ValueError("Diagnostic report exceeds 2 MiB limit")
            self.directory.mkdir(parents=True, exist_ok=True)
            self._replace(path, serialized + "\n")
            # Indented code prevents untrusted model/manual text becoming Markdown markup.
            readable = "# DeviceLink diagnostic report\n\n" + "\n".join(
                "    " + line for line in serialized.splitlines()) + "\n"
            try:
                self._replace(markdown, readable)
            except OSError:
                return {"status": "saved", "json_path": str(path),
                        "markdown_status": "unavailable"}
        return {"status": "saved", "json_path": str(path), "markdown_path": str(markdown)}
