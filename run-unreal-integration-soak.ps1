param(
    [ValidateRange(1, 1000000)]
    [int]$Cycles = 250,
    [ValidateRange(1, 65535)]
    [int]$Port = 5000
)

$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$unrealEditor = 'F:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$uproject = Join-Path $projectDirectory 'UnrealVirtualDevice\UnrealVirtualDevice.uproject'
$buildDirectory = Join-Path $projectDirectory 'build-mfc-host'
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$probe = Join-Path $buildDirectory 'DeviceLinkUnrealSoak.exe'

foreach ($requiredFile in @($unrealEditor, $uproject, $cmake, $vcvars)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required file was not found: $requiredFile"
    }
}

$buildCommand = 'call "{0}" >nul && "{1}" -G "NMake Makefiles" -S "{2}" -B "{3}" -DDEVICELINK_BUILD_MFC_HOST=ON && "{1}" --build "{3}" --target DeviceLinkUnrealSoak' -f $vcvars, $cmake, $projectDirectory, $buildDirectory
& cmd.exe /d /s /c $buildCommand
if ($LASTEXITCODE -ne 0) {
    throw 'Could not build the Unreal integration probe.'
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
    Write-Host "[INFO] Running $Cycles full protocol cycles..."
    & $probe '127.0.0.1' $Port $Cycles
    if ($LASTEXITCODE -ne 0) {
        throw "Integration probe exited with code $LASTEXITCODE."
    }
    Write-Host '[PASS] Unreal integration soak completed successfully.'
}
finally {
    if (-not $unrealProcess.HasExited) {
        Stop-Process -Id $unrealProcess.Id
        $unrealProcess.WaitForExit()
    }
}
