param([string]$EngineRoot = 'F:\UE_5.8')
$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = Join-Path $workspace 'UnrealVirtualDevice\UnrealVirtualDevice.uproject'
$script = Join-Path $PSScriptRoot 'Create-MinimalDeviceMap.py'
$output = Join-Path $workspace 'artifacts\asset-audit'
New-Item -ItemType Directory -Path $output -Force | Out-Null
& $editor $project '-run=pythonscript' "-script=$script" '-EnablePlugins=PythonScriptPlugin' '-DisablePlugins=AllToolsets,ModelContextProtocol' '-unattended' '-nullrhi' '-nosound' "-abslog=$output\minimal-map.log"
if ($LASTEXITCODE -ne 0) { throw "Minimal map creation failed. See $output\minimal-map.log" }
