param(
    [Parameter(Mandatory)] [string]$Tag,
    [Parameter(Mandatory)] [string]$Repository
)

$ErrorActionPreference = 'Stop'
if ($Tag -cnotmatch '^v[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9]+([.-][a-zA-Z0-9]+)*)?$') { throw 'Expected a version tag such as v1.0.0 or v1.0.0-rc.1.' }
if ($Repository -notmatch '^[a-zA-Z0-9-]+/[a-zA-Z0-9_.-]+$') { throw 'Expected the GitHub repository as owner/name.' }
$head = & git rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Could not determine the source revision.' }
$tagCommit = & gh api --method GET "repos/$Repository/commits/$Tag" --jq '.sha'
if ($LASTEXITCODE -ne 0 -or $tagCommit -cne $head) { throw 'The remote version tag must point to the tested commit.' }
$zip = 'ci-artifacts/release/DeviceLinkStudio-Windows-x64.zip'
$checksum = "$zip.sha256"
$expected = ((Get-Content -LiteralPath $checksum -Raw).Trim() -split '\s+')[0]
if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $expected) { throw 'The ZIP checksum changed after verification.' }

$releaseOptions = @('--repo', $Repository, '--verify-tag', '--draft', '--generate-notes')
if ($Tag.Contains('-')) { $releaseOptions += '--prerelease' }
& gh release create $Tag $zip $checksum @releaseOptions
if ($LASTEXITCODE -ne 0) { throw 'GitHub release draft creation failed. Check whether a draft already exists before retrying.' }
