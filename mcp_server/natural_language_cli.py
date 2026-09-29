#!/usr/bin/env python3
"""Standalone natural-language console for DeviceLink MCP operations."""

from __future__ import annotations

import argparse
import json
import sys

from server import Bridge, call_tool


def _print_result(result: dict[str, object]) -> None:
    print(json.dumps(result, ensure_ascii=False, indent=2))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Control the DeviceLink EO/IR simulator with Korean or English instructions.")
    parser.add_argument("instruction", nargs="*", help="instruction to execute once")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--no-auto-connect", action="store_true")
    parser.add_argument("--no-auto-prepare", action="store_true")
    args = parser.parse_args()

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    if hasattr(sys.stdin, "reconfigure"):
        sys.stdin.reconfigure(encoding="utf-8")

    bridge = Bridge()
    common = {
        "host": args.host,
        "port": args.port,
        "auto_connect": not args.no_auto_connect,
        "auto_prepare": not args.no_auto_prepare,
    }
    try:
        if args.instruction:
            try:
                result = call_tool(
                    bridge, "execute_instruction",
                    {**common, "instruction": " ".join(args.instruction)},
                )
            except (ValueError, FileNotFoundError, RuntimeError, json.JSONDecodeError) as exception:
                result = {"ok": False, "message": str(exception)}
            _print_result(result)
            return 0 if result.get("ok") else 1

        print("DeviceLink 자연어 콘솔입니다. 종료하려면 exit 또는 quit를 입력하세요.")
        while True:
            try:
                instruction = input("DeviceLink> ").strip()
            except EOFError:
                break
            if not instruction:
                continue
            if instruction.lower() in {"exit", "quit", "종료"}:
                break
            try:
                _print_result(call_tool(
                    bridge, "execute_instruction", {**common, "instruction": instruction}))
            except (ValueError, FileNotFoundError, RuntimeError, json.JSONDecodeError) as exception:
                _print_result({"ok": False, "message": str(exception)})
    finally:
        bridge.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
