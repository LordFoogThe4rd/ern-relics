[CmdletBinding()]
param(
    [ValidateSet('debug', 'release', 'asan')][string]$Preset = 'release',
    [ValidatePattern('^(?:[a-z0-9]+(?:-[a-z0-9]+)*)?$')][string]$Label = 'dev',
    [string]$BuildDirectory,
    [ValidateRange(0, 1024)][int]$Jobs = 0
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'build/output.ps1')
& uv run --project $projectRoot --locked ruff check --no-cache $projectRoot
if ($LASTEXITCODE -ne 0) { throw 'Python lint failed.' }
& (Join-Path $PSScriptRoot 'format.ps1')
$temporary = -not $BuildDirectory
$build = if ($temporary) { New-TemporaryDirectory } else {
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
}
try {
    & (Join-Path $PSScriptRoot 'build.ps1') -Preset $Preset -Jobs $Jobs -Label $Label -BuildDirectory $build
    $analysis = @{ BuildDirectory = $build }
    if ($Jobs) { $analysis.Jobs = $Jobs }
    & (Join-Path $PSScriptRoot 'analyze.ps1') @analysis
} finally {
    if ($temporary) { Remove-TemporaryDirectory $build }
}
Write-Host 'All checks passed: Python lint, Python/C++ formatting, compiler warnings, static analysis, CTest and synthetic asset regressions, and the actual-DLL DX12 host.'
