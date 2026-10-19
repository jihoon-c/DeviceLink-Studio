param(
    [string]$EngineRoot = 'F:\UE_5.8',
    [string]$Map = '/Game/Maps/DeviceLinkLab'
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = Join-Path $workspace 'UnrealVirtualDevice\UnrealVirtualDevice.uproject'
$script = Join-Path $PSScriptRoot 'Audit-UnrealAssets.py'
if (-not (Test-Path -LiteralPath $editor)) { throw "Editor missing: $editor" }
if ($Map -notmatch '^/Game/[A-Za-z0-9_/]+$') { throw 'Use a /Game/ map package name.' }
$output = Join-Path $workspace ('artifacts\asset-audit\' + ($Map.Substring(6).Replace('/', '_')))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$previousMap = $env:DEVICELINK_ASSET_AUDIT_MAP
$previousOutput = $env:DEVICELINK_ASSET_AUDIT_OUTPUT
try {
    $env:DEVICELINK_ASSET_AUDIT_MAP = $Map
    $env:DEVICELINK_ASSET_AUDIT_OUTPUT = $output
    & $editor $project '-run=pythonscript' "-script=$script" '-EnablePlugins=PythonScriptPlugin' '-DisablePlugins=AllToolsets,ModelContextProtocol' '-unattended' '-nullrhi' '-nosound' "-abslog=$output\audit.log"
    if ($LASTEXITCODE -ne 0) { throw "Asset audit failed. See $output\audit.log" }
    Write-Output "Audit: $output\asset-references.md"
} finally {
    $env:DEVICELINK_ASSET_AUDIT_MAP = $previousMap
    $env:DEVICELINK_ASSET_AUDIT_OUTPUT = $previousOutput
}
