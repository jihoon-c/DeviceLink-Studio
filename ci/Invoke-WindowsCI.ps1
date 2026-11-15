param([string]$Python = 'python')

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$oldBridge = $env:DEVICELINK_AGENT_BRIDGE
$oldUtf8 = $env:PYTHONUTF8
Push-Location $root
try {
    & $Python -c 'import sys, tkinter; sys.exit(sys.version_info < (3, 11))'
    if ($LASTEXITCODE -ne 0) { throw 'Python 3.11 or newer with Tk is required.' }
    & $Python -m venv mcp_server/.venv
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the Python virtual environment.' }
    $venv = Join-Path $root 'mcp_server/.venv/Scripts/python.exe'
    & $venv -m pip install --disable-pip-version-check -r mcp_server/requirements-diagnostics.txt
    if ($LASTEXITCODE -ne 0) { throw 'Python dependency installation failed.' }

    & ./scripts/Build-DemoLauncher.ps1 -RunTests
    $env:DEVICELINK_AGENT_BRIDGE = Join-Path $root 'build-demo/DeviceLinkAgentBridge.exe'
    $env:PYTHONUTF8 = '1'
    & $venv -m unittest tests.SnapshotDiagnosticTests tests.DeviceAgentMcpTests tests.DiagnosticAgentIntegrationTests tests.DemoLauncherTests 2>&1 |
        Tee-Object -FilePath ci-artifacts/python-tests.log
    if ($LASTEXITCODE -ne 0) { throw 'Python regression tests failed.' }
}
finally {
    $env:DEVICELINK_AGENT_BRIDGE = $oldBridge
    $env:PYTHONUTF8 = $oldUtf8
    Pop-Location
}
