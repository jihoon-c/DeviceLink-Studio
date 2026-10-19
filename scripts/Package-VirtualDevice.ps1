param(
    [string]$EngineRoot = 'F:\UE_5.8',
    [string]$Map = '/Game/Maps/DeviceLinkMinimal'
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
$uat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$project = Join-Path $workspace 'UnrealVirtualDevice\UnrealVirtualDevice.uproject'
$output = Join-Path $workspace 'artifacts\package'
if (-not (Test-Path -LiteralPath $uat)) { throw "AutomationTool missing: $uat" }
if ($Map -notmatch '^/Game/[A-Za-z0-9_/]+$') { throw 'Use a /Game/ map package name.' }
$mapFile = Join-Path $workspace ('UnrealVirtualDevice\Content\' + $Map.Substring(6) + '.umap')
if (-not (Test-Path -LiteralPath $mapFile)) { throw "Map missing: $mapFile. Run Create-MinimalDeviceMap.ps1 first." }
New-Item -ItemType Directory -Path $output -Force | Out-Null
# Explicit map only. Existing content and editor plugin settings are preserved.
$cookOptions = '-DisablePlugins=AllToolsets,ModelContextProtocol -NoDefaultMaps'
& $uat BuildCookRun "-project=$project" '-noP4' '-platform=Win64' '-clientconfig=Development' '-build' '-cook' "-map=$Map" '-CookDir=/Game/DeviceLink/Equipment/Articulated' "-AdditionalCookerOptions=$cookOptions" '-stage' '-pak' '-archive' "-archivedirectory=$output" '-utf8output' '-unattended'
if ($LASTEXITCODE -ne 0) { throw 'Packaging failed. See Unreal AutomationTool logs.' }
$executable = Join-Path $output 'Windows\UnrealVirtualDevice.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw "Package executable missing: $executable" }
@{ map = $Map } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path (Split-Path $executable -Parent) 'devicelink-package.json') -Encoding utf8
Write-Output "Package ready: $executable"
