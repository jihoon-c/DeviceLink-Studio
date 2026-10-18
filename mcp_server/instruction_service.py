"""Validated request planning and execution, independent of MCP and UI."""
from __future__ import annotations

import math
import re
from typing import Any, Callable

def _contains_any(text: str, values: tuple[str, ...]) -> bool:
    return any(value in text for value in values)


def _extract_angle(instruction: str, names: tuple[str, ...]) -> float | None:
    joined = "|".join(re.escape(name) for name in names)
    matches = re.findall(
        rf"(?:{joined})\s*(?:각도)?\s*(?:은|는|을|를|=|:)?\s*(-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)(?![\d.eE])",
        instruction,
        re.IGNORECASE,
    )
    if len(matches) > 1:
        raise ValueError('Specify each angle only once')
    value = float(matches[0]) if matches else None
    if value is not None and not math.isfinite(value):
        raise ValueError('Angle must be finite')
    return value


def plan_instruction(arguments: dict[str, Any]) -> dict[str, Any]:
    instruction = arguments.get("instruction")
    if not isinstance(instruction, str) or len(instruction.strip()) < 2:
        raise ValueError("instruction must contain at least two characters")
    text = instruction.strip()
    lowered = text.lower()
    if re.search(r"하지|말고|말아|지\s*마|않|금지|안\s*(?:해|하|켜|끄|움직|이동|주입|복구)|\b(?:no|not|never|without)\b|don['’]t", lowered):
        return {'ok': False, 'message': 'Negation or exclusion requires clarification; no action was planned.'}
    if _contains_any(lowered, ('연결 해제', '연결해제', 'disconnect')):
        return {'ok': False, 'message': 'Use the Disconnect button; this instruction is not supported.'}
    diagnostic = _contains_any(lowered, ('진단', 'diagnos', '고장 원인', '복구 방법', '복구 절차', '고장 설명'))
    if diagnostic:
        if _contains_any(lowered, ('이동', '주입', '초기화', '전원', 'inject', 'initialize', 'power')):
            return {'ok': False, 'message': 'Separate diagnostic questions from device-changing actions.'}
        return {'ok': True, 'kind': 'diagnosis', 'instruction': text, 'commands': []}

    host = arguments.get("host", "127.0.0.1")
    if not isinstance(host, str) or not host or any(character.isspace() for character in host):
        raise ValueError("host must be a non-empty host name or IP address")
    port = arguments.get("port", 5000)
    if not isinstance(port, int) or isinstance(port, bool):
        raise ValueError("port must be an integer")
    if not 1 <= port <= 65535:
        raise ValueError("port must be between 1 and 65535")
    auto_connect = arguments.get("auto_connect", True)
    auto_prepare = arguments.get("auto_prepare", True)
    if not isinstance(auto_connect, bool) or not isinstance(auto_prepare, bool):
        raise ValueError("auto_connect and auto_prepare must be booleans")

    endpoint = re.search(r"((?:\d{1,3}\.){3}\d{1,3}|localhost)\s*:\s*(\d{1,5})", text, re.IGNORECASE)
    if endpoint:
        host, port = endpoint.group(1), int(endpoint.group(2))
        if not 1 <= port <= 65535:
            raise ValueError("port must be between 1 and 65535")

    pan = _extract_angle(text, ("pan", "팬"))
    tilt = _extract_angle(text, ("tilt", "틸트"))
    has_any_angle = pan is not None or tilt is not None
    if has_any_angle and (pan is None or tilt is None):
        raise ValueError("Both Pan and Tilt angles are required")
    if pan is not None and not -170 <= pan <= 170:
        raise ValueError("Pan must be between -170 and 170 degrees")
    if tilt is not None and not -45 <= tilt <= 80:
        raise ValueError("Tilt must be between -45 and 80 degrees")

    wants_connect = _contains_any(lowered, ("연결", "connect")) or endpoint is not None
    wants_power_off = _contains_any(lowered, ("전원 끄", "전원 off", "power off"))
    wants_power_on = _contains_any(lowered, ("전원 켜", "전원 on", "power on"))
    wants_initialize = _contains_any(lowered, ("초기화", "initialize", "home"))
    wants_telemetry = _contains_any(lowered, ("텔레메트리", "telemetry", "상태 확인", "status"))
    wants_clear_fault = _contains_any(lowered, ("fault 해제", "고장 해제", "장애 해제", "clear fault", "복구"))
    fault_names = [name for name in ("MotorStall", "OverTemperature", "SensorFailure")
                   if name.lower() in lowered.replace(" ", "")]
    if len(fault_names) > 1:
        return {'ok': False, 'message': 'Specify only one fault per request.'}
    fault_name = fault_names[0] if fault_names else None
    wants_inject_fault = fault_name is not None and _contains_any(
        lowered, ("주입", "발생", "inject", "simulate"))
    wants_scenario = _contains_any(lowered, ("motor_stall_recovery", "복구 시험", "recovery test"))

    device_action = any((wants_power_off, wants_power_on, wants_initialize, has_any_angle,
                         wants_telemetry, wants_clear_fault, wants_inject_fault, wants_scenario))
    if not wants_connect and not device_action:
        return {
            "ok": False,
            "message": "Unsupported instruction. Include connect, power, initialize, Pan/Tilt, telemetry, fault, or recovery test.",
        }
    if wants_power_off and (has_any_angle or wants_power_on or wants_initialize):
        return {"ok": False, "message": "Power-off cannot be combined with power-on, initialize, or movement."}

    if wants_inject_fault and (wants_clear_fault or wants_scenario):
        return {'ok': False, 'message': 'Fault injection cannot be combined with recovery.'}
    if wants_power_off and (wants_inject_fault or wants_clear_fault or wants_scenario):
        return {'ok': False, 'message': 'Power-off cannot be combined with fault or recovery actions.'}
    commands = []
    if wants_power_off:
        commands.append('POWER_OFF 3000')
    else:
        if wants_power_on or (auto_prepare and (has_any_angle or wants_initialize)):
            commands.append('POWER_ON 3000')
        if wants_initialize or (has_any_angle and auto_prepare):
            commands.append('INITIALIZE 5000')
        if wants_scenario:
            commands.append('RUN_SCENARIO motor_stall_recovery 5000')
        elif wants_inject_fault:
            commands.append(f'INJECT_FAULT {fault_name} 3000')
        elif wants_clear_fault:
            commands.append('CLEAR_FAULT 3000')
        if has_any_angle:
            commands.append(f'SET_ANGLE {pan:g} {tilt:g} 5000 0.5')
        if wants_telemetry and not has_any_angle:
            commands.append('GET_TELEMETRY 2000')
    return {'ok': True, 'kind': 'device_commands', 'instruction': text,
            'host': host, 'port': port, 'auto_connect': auto_connect,
            'wants_connect': wants_connect, 'commands': commands}


def execute_instruction(bridge: Any, arguments: dict[str, Any], *, diagnose: Callable) -> dict[str, Any]:
    preview = arguments.get('preview_only', False)
    if not isinstance(preview, bool):
        raise ValueError('preview_only must be a boolean')
    plan = plan_instruction(arguments)
    if not plan['ok'] or preview:
        return {**plan, 'preview_only': preview, 'actions_taken': []}
    if plan['kind'] == 'diagnosis':
        return diagnose()
    steps = []
    host, port = plan['host'], plan['port']
    auto_connect, wants_connect = plan['auto_connect'], plan['wants_connect']
    if bridge.connected_endpoint not in {None, (host, port)}:
        if not auto_connect and not wants_connect:
            return {'ok': False, 'message': 'Device is connected to a different endpoint', 'steps': []}
        bridge.close()
    if bridge.connected_endpoint is None:
        if not auto_connect and not wants_connect:
            return {'ok': False, 'message': 'Device is not connected and auto_connect is disabled', 'steps': []}
        commands = [f'CONNECT {host} {port}', *plan['commands']]
    else:
        commands = plan['commands']
    for command in commands:
        result = bridge.call(command)
        steps.append({'command': command, 'result': result})
        if not result.get('ok'):
            return {'ok': False, 'message': 'Instruction stopped after failed command', 'steps': steps}
    response = {'ok': True, 'message': 'Natural-language instruction completed',
                'instruction': plan['instruction'], 'steps': steps, 'plan': plan}
    if steps and 'telemetry' in steps[-1]['result']:
        response['telemetry'] = steps[-1]['result']['telemetry']
    return response
