param(
    [switch]$RunTests,
    [string]$TestResultsDirectory = 'ci-artifacts'
)

$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
$buildDirectory = Join-Path $workspace 'build-demo'
$installation = $null
$cmake = $null
$ninja = $null
foreach ($version in @('18', '2022')) {
    foreach ($edition in @('Community', 'Professional', 'Enterprise', 'BuildTools')) {
        $candidate = Join-Path $env:ProgramFiles "Microsoft Visual Studio\$version\$edition"
        $candidateCmake = Join-Path $candidate 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        $candidateNinja = Join-Path $candidate 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
        if (-not $cmake -and (Test-Path $candidateCmake)) { $cmake = $candidateCmake }
        if (-not $ninja -and (Test-Path $candidateNinja)) { $ninja = $candidateNinja }
        $mfc = @(Get-ChildItem (Join-Path $candidate 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
            Where-Object { Test-Path (Join-Path $_.FullName 'atlmfc\include\afxwin.h') })
        if (-not $installation -and $mfc.Count -gt 0 -and (Test-Path (Join-Path $candidate 'VC\Auxiliary\Build\vcvars64.bat'))) {
            $installation = $candidate
        }
    }
}
if (-not $installation) { throw 'Visual Studio C++ and MFC build tools are required.' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
if (-not $cmake -or -not $ninja) { throw 'Visual Studio CMake and Ninja tools are required.' }
New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
$batch = Join-Path $buildDirectory 'build-demo.cmd'
$buildTargets = if ($RunTests) { '--parallel' } else { '--target DeviceLinkDemo DeviceLinkMfcHost DeviceLinkAgentBridge' }
$commands = @(
    '@echo off',
    'chcp 65001 >nul',
    'set VSLANG=1033',
    ('call "{0}" >nul' -f $vcvars),
    'if errorlevel 1 exit /b 1',
    ('"{0}" --fresh -S "{1}" -B "{2}" -G Ninja -DCMAKE_MAKE_PROGRAM="{3}" -DCMAKE_BUILD_TYPE=Release -DDEVICELINK_BUILD_MFC_HOST=ON -DPython3_EXECUTABLE="{1}\mcp_server\.venv\Scripts\python.exe"' -f $cmake, $workspace, $buildDirectory, $ninja),
    'if errorlevel 1 exit /b 1',
    ('"{0}" --build "{1}" {2}' -f $cmake, $buildDirectory, $buildTargets),
    'exit /b %errorlevel%'
)
$commands -join "`r`n" | Set-Content -LiteralPath $batch -Encoding OEM
$buildLog = Join-Path $buildDirectory 'build-demo.log'
Write-Output "Building launcher, MFC and bridge. Log: $buildLog"
& cmd.exe /d /c $batch > $buildLog 2>&1
if ($LASTEXITCODE -ne 0) {
    Select-String -Path $buildLog -Pattern 'error|fatal|FAILED' | Select-Object -Last 15 | ForEach-Object { $_.Line }
    throw "Demo launcher build failed. See $buildLog"
}
Write-Output "Ready: $(Join-Path $workspace 'DeviceLinkDemo.exe')"

if ($RunTests) {
    $results = New-Item -ItemType Directory -Path $TestResultsDirectory -Force
    $ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
    & $ctest --test-dir $buildDirectory --output-on-failure --no-tests=error --output-junit (Join-Path $results.FullName 'ctest.xml')
    if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
}
