"""Local manual lookup shared by MCP and snapshot diagnostics."""
from __future__ import annotations

import hashlib
from pathlib import Path
import re
from typing import Any

def search_manual(query: str, limit: int, directory: Path) -> dict[str, Any]:
    terms = {term.lower() for term in re.findall(r"[A-Za-z0-9가-힣]+", query) if len(term) > 1}
    matches: list[tuple[int, dict[str, Any]]] = []
    errors: list[str] = []
    for path in sorted(directory.glob("*")):
        if path.suffix.lower() not in {".md", ".pdf"} or not path.is_file():
            continue
        if path.stat().st_size > 20 * 1024 * 1024:
            errors.append(f"{path.name}: manual exceeds 20 MiB limit")
            continue
        try:
            content = path.read_bytes()
        except OSError as exception:
            errors.append(f"{path.name}: {type(exception).__name__}")
            continue
        version = hashlib.sha256(content).hexdigest()[:16]
        uri = f"devicelink://manual/{path.name}"
        chunks: list[tuple[str, int | None, str]] = []
        if path.suffix.lower() == ".md":
            try:
                sections = re.split(r"(?=^## )", content.decode("utf-8"), flags=re.MULTILINE)
            except UnicodeDecodeError:
                errors.append(f"{path.name}: Markdown must be UTF-8")
                continue
            for section in sections:
                section = section.strip()
                if section:
                    chunks.append((section.splitlines()[0].removeprefix("## "), None, section))
        else:
            try:
                from io import BytesIO
                from pypdf import PdfReader
                pages = PdfReader(BytesIO(content)).pages
                if len(pages) > 200:
                    errors.append(f"{path.name}: PDF exceeds 200-page limit")
                    continue
                for page_number, page in enumerate(pages, start=1):
                    page_text = page.extract_text() or ""
                    if page_text.strip():
                        chunks.append((f"Page {page_number}", page_number, page_text.strip()))
                if not chunks:
                    errors.append(f"{path.name}: no extractable text; OCR is required")
            except Exception as exception:
                errors.append(f"{path.name}: {exception}")
                continue
        for section_name, page_number, chunk in chunks:
            lowered = chunk.lower()
            score = sum(lowered.count(term) for term in terms)
            if score:
                first_match = min(lowered.find(term) for term in terms if term in lowered)
                excerpt_start = max(0, first_match - 150)
                matches.append((score, {"source": uri, "document_version": version,
                                        "section": section_name, "page": page_number,
                                        "excerpt": chunk[excerpt_start:excerpt_start + 900]}))
    matches.sort(key=lambda item: (-item[0], item[1]["source"], item[1]["page"] or 0))
    selected = [item[1] for item in matches[:limit]]
    for index, item in enumerate(selected, start=1):
        item["citation_id"] = f"E{index}"
    return {
        "ok": bool(matches),
        "message": "Manual evidence found" if matches else "No matching manual section found",
        "matches": selected,
        "errors": errors,
    }
