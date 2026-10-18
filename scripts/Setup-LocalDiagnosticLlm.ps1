param(
    [string]$Model = 'qwen3:4b',
    [string]$Version = 'v0.40.5',
    [switch]$SkipPull
)

$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
$runtime = Join-Path $workspace 'artifacts\local-llm'
$binaryDirectory = Join-Path $runtime "ollama-$Version"
$executable = Join-Path $binaryDirectory 'ollama.exe'
$modelDirectory = Join-Path $runtime 'models'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null

if (-not (Test-Path -LiteralPath $executable)) {
    $release = Invoke-RestMethod "https://api.github.com/repos/ollama/ollama/releases/tags/$Version"
    $asset = $release.assets | Where-Object { $_.name -eq 'ollama-windows-amd64.zip' }
    if (-not $asset -or -not $asset.digest.StartsWith('sha256:')) {
        throw 'The official release did not provide a SHA-256 digest.'
    }
    $archive = Join-Path $runtime "ollama-$Version.zip"
    Write-Output "Downloading official Ollama $Version ($([math]::Round($asset.size / 1GB, 2)) GiB)..."
    & curl.exe --fail --location --retry 2 --continue-at - --silent --show-error --output $archive $asset.browser_download_url
    if ($LASTEXITCODE -ne 0) { throw 'Ollama download failed.' }
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $asset.digest.Substring(7)) { throw 'Ollama archive checksum mismatch.' }
    Write-Output 'Checksum verified. Extracting portable runtime...'
    [System.IO.Compression.ZipFile]::ExtractToDirectory($archive, $binaryDirectory, $true)
}

# These process-scoped settings apply only to this project runtime.
$env:OLLAMA_HOST = '127.0.0.1:11434'
$env:OLLAMA_MODELS = $modelDirectory
$env:OLLAMA_NO_CLOUD = '1'
$env:OLLAMA_CONTEXT_LENGTH = '8192'
$env:OLLAMA_MAX_LOADED_MODELS = '1'
$env:OLLAMA_NUM_PARALLEL = '1'
$env:NO_PROXY = 'localhost,127.0.0.1,::1'

$running = $false
try {
    $null = Invoke-RestMethod 'http://127.0.0.1:11434/api/version' -NoProxy -TimeoutSec 2
    $running = $true
} catch { }
if (-not $running) {
    $server = Start-Process -FilePath $executable -ArgumentList 'serve' -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $runtime 'server.stdout.log') `
        -RedirectStandardError (Join-Path $runtime 'server.stderr.log')
    $server.Id | Set-Content (Join-Path $runtime 'server.pid')
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        try {
            $null = Invoke-RestMethod 'http://127.0.0.1:11434/api/version' -NoProxy -TimeoutSec 2
            $running = $true
            break
        } catch { Start-Sleep -Seconds 1 }
    }
}
if (-not $running) { throw 'Local Ollama server did not start; inspect artifacts/local-llm/server.stderr.log.' }
if (-not $SkipPull) {
    Write-Output "Downloading local model $Model (first run only)..."
    $pull = @{model=$Model; stream=$false} | ConvertTo-Json
    $null = Invoke-RestMethod 'http://127.0.0.1:11434/api/pull' -Method Post -Body $pull `
        -ContentType 'application/json' -NoProxy -TimeoutSec 3600
}
$models = Invoke-RestMethod 'http://127.0.0.1:11434/api/tags' -NoProxy -TimeoutSec 5
if ($Model -notin $models.models.name) { throw "Local model $Model is not available." }
Write-Output 'Warming the local model...'
$warmup = @{model=$Model; prompt=''; keep_alive='10m'; options=@{num_ctx=8192}} | ConvertTo-Json
$null = Invoke-RestMethod 'http://127.0.0.1:11434/api/generate' -Method Post -Body $warmup `
    -ContentType 'application/json' -NoProxy -TimeoutSec 120

$configuration = @{provider='local'; model=$Model; base_url='http://127.0.0.1:11434/v1'; timeout_seconds=120}
$configuration | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $workspace 'mcp_server\llm.local.json') -Encoding utf8
Write-Output "Local LLM configured: $Model at http://127.0.0.1:11434/v1"
Write-Output 'No cloud key is used. Run mcp_server/.venv/Scripts/python.exe mcp_server/check_diagnostics.py --live.'
