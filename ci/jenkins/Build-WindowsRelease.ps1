param(
    [Parameter(Mandatory)] [string]$EngineRoot
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$work = Join-Path $root 'ci-artifacts'
$bundleName = 'DeviceLinkStudio-Windows-x64'
$zipName = "$bundleName.zip"
$scratch = Join-Path (Join-Path $root 'artifacts') ('release-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$bundle = Join-Path $scratch $bundleName
$extracted = Join-Path $scratch 'verify'
$zip = Join-Path $scratch $zipName
$python = Join-Path $root 'mcp_server/.venv/Scripts/python.exe'
$package = Join-Path $root 'artifacts/package/Windows'
$environment = @{}
foreach ($name in @('DEVICELINK_UNREAL_EDITOR', 'DEVICELINK_UNREAL_PACKAGE', 'DEVICELINK_AGENT_BRIDGE', 'PYTHONUTF8')) {
    $environment[$name] = [Environment]::GetEnvironmentVariable($name)
}

Push-Location $root
try {
    $revision = & git rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $revision -notmatch '^[a-f0-9]{40}$') { throw 'Could not determine the source revision.' }
    $sourceChanges = & git status --porcelain --untracked-files=all
    if ($LASTEXITCODE -ne 0 -or $sourceChanges) { throw 'Package from a clean Git checkout so the release manifest identifies the built source.' }
    $results = Join-Path $work 'release'
    if (Test-Path -LiteralPath $results) { throw 'Use a clean workspace for each release candidate.' }
    New-Item -ItemType Directory -Path $work,$scratch -Force | Out-Null
    if (-not (Test-Path -LiteralPath $python -PathType Leaf)) { throw 'Run ci/Invoke-WindowsCI.ps1 first.' }
    foreach ($relative in @('DeviceLinkDemo.exe', 'build-demo/DeviceLinkMfcHost.exe', 'build-demo/DeviceLinkAgentBridge.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf)) { throw "Missing Windows build output: $relative" }
    }
    if (Test-Path -LiteralPath $package) { throw 'The Unreal package output already exists. Use a clean workspace.' }

    & ./scripts/Package-VirtualDevice.ps1 -EngineRoot $EngineRoot *>&1 | Tee-Object -FilePath (Join-Path $work 'unreal-package.log')
    $releaseFolders = @('build-demo', 'mcp_server', 'docs', 'artifacts/package/Windows')
    New-Item -ItemType Directory -Path ($releaseFolders | ForEach-Object { Join-Path $bundle $_ }) | Out-Null
    Copy-Item -LiteralPath DeviceLinkDemo.exe -Destination $bundle
    Copy-Item -LiteralPath build-demo/DeviceLinkMfcHost.exe,build-demo/DeviceLinkAgentBridge.exe -Destination (Join-Path $bundle 'build-demo')
    Copy-Item -Path mcp_server/*.py,mcp_server/requirements-diagnostics.txt -Destination (Join-Path $bundle 'mcp_server')
    Copy-Item -LiteralPath docs/agent -Destination (Join-Path $bundle 'docs') -Recurse
    Copy-Item -LiteralPath scenarios -Destination $bundle -Recurse -Exclude '*.report.*.md'
    Copy-Item -LiteralPath ci/jenkins/START_HERE_KR.md -Destination $bundle
    Get-ChildItem -LiteralPath $package -File -Recurse | Where-Object Extension -ne '.pdb' | ForEach-Object {
        $relative = $_.FullName.Substring($package.Length + 1)
        $destination = Join-Path (Join-Path $bundle 'artifacts/package/Windows') $relative
        New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $destination
    }

    $packageManifest = Get-Content -LiteralPath (Join-Path $package 'devicelink-package.json') -Raw | ConvertFrom-Json
    @{ commit = $Revision; map = $packageManifest.map } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $bundle 'release.json') -Encoding utf8
    & tar.exe -a -cf $zip -C $scratch $bundleName
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the release ZIP.' }
    New-Item -ItemType Directory -Path $extracted | Out-Null
    & tar.exe -xf $zip -C $extracted
    if ($LASTEXITCODE -ne 0) { throw 'Could not extract the release ZIP.' }

    $demo = Join-Path $extracted $bundleName
    $logs = Join-Path $demo 'artifacts/demo'
    $demoPython = Join-Path $demo 'mcp_server/.venv/Scripts/python.exe'
    & $python -m venv (Join-Path $demo 'mcp_server/.venv')
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the extracted demo virtual environment.' }
    & $demoPython -m pip install --disable-pip-version-check -r (Join-Path $demo 'mcp_server/requirements-diagnostics.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Extracted demo dependency installation failed.' }
    # A passing probe must exercise the archived executables, even on a machine with an editor.
    $env:DEVICELINK_UNREAL_EDITOR = Join-Path $demo 'no-editor/UnrealEditor.exe'
    $env:DEVICELINK_UNREAL_PACKAGE = Join-Path $demo 'artifacts/package/Windows/UnrealVirtualDevice.exe'
    $env:DEVICELINK_AGENT_BRIDGE = Join-Path $demo 'build-demo/DeviceLinkAgentBridge.exe'
    $env:PYTHONUTF8 = '1'
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        try { $client.Connect('127.0.0.1', 5000) } catch [System.Net.Sockets.SocketException] { }
        if ($client.Connected) { throw 'TCP 5000 is in use. Stop the other demo before running the release smoke test.' }
    }
    finally { $client.Dispose() }

    try {
        $launcher = Start-Process -FilePath (Join-Path $demo 'DeviceLinkDemo.exe') -ArgumentList '--check' -WorkingDirectory $demo -WindowStyle Hidden -Wait -PassThru
        if ($launcher.ExitCode -ne 0) { throw 'Packaged launcher preflight failed.' }
        & $demoPython (Join-Path $demo 'mcp_server/demo_launcher.py') --smoke-basic
        if ($LASTEXITCODE -ne 0) { throw 'Packaged smoke test failed. See the archived smoke logs.' }
        $smoke = Get-Content -LiteralPath (Join-Path $logs 'smoke-basic-result.json') -Raw | ConvertFrom-Json
        $state = Get-Content -LiteralPath (Join-Path $logs 'launcher-state.json') -Raw | ConvertFrom-Json
        if (-not $smoke.ok -or $smoke.fault_demo.before -ne 'FAIL' -or $smoke.fault_demo.after -ne 'PASS' -or
            -not $smoke.startup.owned_processes.unreal -or -not $state.ok -or $state.state -ne 'stopped') {
            throw 'The packaged smoke test did not prove startup, fault recovery and clean shutdown.'
        }
    }
    finally {
        if (Test-Path -LiteralPath $logs) {
            $evidence = New-Item -ItemType Directory -Path (Join-Path $work 'smoke') -Force
            Get-ChildItem -LiteralPath $logs -File | Where-Object Extension -in '.json','.log' | Copy-Item -Destination $evidence.FullName
            $reports = Join-Path $logs 'reports'
            if (Test-Path -LiteralPath $reports) { Copy-Item -LiteralPath $reports -Destination $evidence.FullName -Recurse }
        }
    }

    New-Item -ItemType Directory -Path $results | Out-Null
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    Move-Item -LiteralPath $zip -Destination $results
    "$hash  $zipName" | Set-Content -LiteralPath (Join-Path $results "$zipName.sha256") -Encoding ascii
    Write-Output "Verified release candidate: $Revision $hash"
}
finally {
    foreach ($name in $environment.Keys) { [Environment]::SetEnvironmentVariable($name, $environment[$name]) }
    Pop-Location
}
