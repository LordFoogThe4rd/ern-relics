[CmdletBinding()]
param(
    [switch]$SkipAnalysis
)
$ErrorActionPreference = 'Stop'
# Native exits are checked explicitly; an unborn dependency has no HEAD yet.
$PSNativeCommandUseErrorActionPreference = $false
$projectRoot = Split-Path -Parent $PSScriptRoot
& uv sync --project $projectRoot --locked
if ($LASTEXITCODE -ne 0) { throw 'Tooling setup failed. See the uv output above.' }
. (Join-Path $PSScriptRoot 'build/toolchain.ps1')
$python = & uv run --project $projectRoot --locked python -c 'import sys; print(sys.executable)'
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the tooling Python.' }
$toolchain = Get-NativeToolchain
if (-not $SkipAnalysis) {
    $null = Get-PinnedClangTidy -Toolchain $toolchain -Python $python
}
$dependencyRoot = Join-Path $projectRoot 'third_party'
New-Item -ItemType Directory -Force -Path $dependencyRoot | Out-Null

$dependencies = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'sources.lock.json') | ConvertFrom-Json

foreach ($dependency in $dependencies) {
    $destination = Join-Path $dependencyRoot $dependency.Name
    if (-not (Test-Path -LiteralPath (Join-Path $destination '.git'))) {
        & git init --quiet $destination
        if ($LASTEXITCODE -ne 0) { throw "Cannot initialize $destination" }
        & git -C $destination remote add origin $dependency.Url
        if ($LASTEXITCODE -ne 0) { throw "Cannot set origin for $destination" }
    }
    $origin = & git -C $destination remote get-url origin
    if ($LASTEXITCODE -ne 0) { throw "Cannot read dependency origin: $destination" }
    if ($origin -ne $dependency.Url) { throw "Unexpected dependency origin in $destination" }
    $current = & git -C $destination rev-parse --verify HEAD 2>$null
    if ($current -ne $dependency.Commit) {
        & git -C $destination fetch --quiet --depth 1 origin $dependency.Commit
        if ($LASTEXITCODE -ne 0) { throw "Cannot download $($dependency.Name)" }
        & git -C $destination checkout --quiet --detach $dependency.Commit
        if ($LASTEXITCODE -ne 0) { throw "Cannot check out $($dependency.Name)" }
    }
    $changes = & git -C $destination status --porcelain
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect dependency changes: $destination" }
    if ($changes) { throw "Dependency has local changes: $destination" }
    Write-Host "$($dependency.Name): $($dependency.Commit)"
}
