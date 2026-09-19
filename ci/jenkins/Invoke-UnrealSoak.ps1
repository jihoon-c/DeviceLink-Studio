param(
    [Parameter(Mandatory)]
    [string]$UnrealRoot,
    [Parameter(Mandatory)]
    [string]$ProjectRoot,
    [Parameter(Mandatory)]
    [string]$BuildDirectory,
    [ValidateRange(1, 1000000)]
    [int]$Cycles = 25,
    [ValidateRange(1, 65535)]
    [int]$Port = 5510
)

$ErrorActionPreference = 'Stop'
$unrealEditor = Join-Path $UnrealRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$uproject = Join-Path $ProjectRoot 'UnrealVirtualDevice\UnrealVirtualDevice.uproject'
$probe = Join-Path $BuildDirectory 'Release\DeviceLinkUnrealSoak.exe'

foreach ($requiredFile in @($unrealEditor, $uproject, $probe)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required file was not found: $requiredFile"
    }
}

$arguments = @(
    $uproject,
    '/Game/Maps/DeviceLinkLab',
    '-game',
    '-unattended',
    '-nosplash',
    '-NoSound',
    '-windowed',
    '-ResX=960',
    '-ResY=540',
    '-DeviceLinkAutoStart',
    "-DeviceLinkPort=$Port"
)

Write-Host "[INFO] Starting Unreal virtual device on port $Port..."
$unrealProcess = Start-Process -FilePath $unrealEditor -ArgumentList $arguments -PassThru
try {
    Start-Sleep -Seconds 5
    Write-Host "[INFO] Running $Cycles integration cycles..."
    & $probe '127.0.0.1' $Port $Cycles
    if ($LASTEXITCODE -ne 0) {
        throw "Integration probe exited with code $LASTEXITCODE."
    }
}
finally {
    if (-not $unrealProcess.HasExited) {
        Stop-Process -Id $unrealProcess.Id
        $unrealProcess.WaitForExit()
    }
}
