"""Check local diagnostic dependencies; --live explicitly runs one model request."""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys

if __package__:
    from . import server
    from .llm_diagnostics import settings
else:
    import server
    from llm_diagnostics import settings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--live", action="store_true", help="Run one request with synthetic telemetry and the sample manual")
    arguments = parser.parse_args()
    try:
        config = settings()
    except (OSError, ValueError, TypeError):
        print(json.dumps({"ok": False, "error_code": "invalid_configuration"}))
        return 1
    dependencies = {name: importlib.util.find_spec(name) is not None
                    for name in ("pypdf", "langchain_openai")}
    evidence = server._search_manual("MotorStall", 3)
    result = {"ok": all(dependencies.values()) and bool(config["model"]) and evidence["ok"],
              "provider": config["provider"], "model": config["model"],
              "dependencies": dependencies, "manual_matches": len(evidence["matches"]),
              "manual_errors": evidence["errors"], "request_sent": False}
    try:
        result["bridge_found"] = server.Bridge._find_executable().is_file()
    except FileNotFoundError:
        result["bridge_found"] = False
        result["ok"] = False
    if arguments.live:
        # These inputs are explicitly synthetic; this checks integration, not diagnostic quality.
        explanation = server._langchain_explanation(
            {"state": "Fault", "fault": "MotorStall", "synthetic_test": True}, [], evidence["matches"])
        result["request_sent"] = explanation["status"] in {"generated", "unavailable"}
        result["llm_explanation"] = explanation
        result["validation_scope"] = "synthetic_sample_schema_and_citations_only"
        result["ok"] = result["ok"] and explanation["status"] == "generated"
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    raise SystemExit(main())
