[CmdletBinding()]
param(
    [string]$GameDirectory,
    [string]$RegulationPath,
    [Parameter(Mandatory)][string]$BuildDirectory,
    [string]$PackageDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'build/output.ps1')
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
if (-not (Test-Path -LiteralPath (Join-Path $build 'ern-relics-host.exe'))) { throw 'Run tools/build.ps1 first.' }
$runtime = $build
if ($PackageDirectory) {
    if ($GameDirectory -or $RegulationPath) { throw 'Package smoke tests use synthetic data only.' }
    $runtime = (Resolve-Path -LiteralPath $PackageDirectory).Path
}
if ($GameDirectory) {
    $GameDirectory = (Resolve-Path -LiteralPath $GameDirectory).Path
}
if ($RegulationPath) { $RegulationPath = (Resolve-Path -LiteralPath $RegulationPath).Path }
$run = New-TemporaryDirectory
try {
    Copy-Item -LiteralPath (Join-Path $build 'ern-relics-host.exe') -Destination $run
    Copy-Item -LiteralPath (Join-Path $runtime 'ern-relics.dll') -Destination $run
    $hostSymbols = Join-Path $build 'ern-relics-host.pdb'
    if (Test-Path -LiteralPath $hostSymbols) { Copy-Item -LiteralPath $hostSymbols -Destination $run }
    $dllSymbols = Join-Path $runtime 'ern-relics.pdb'
    if (Test-Path -LiteralPath $dllSymbols) { Copy-Item -LiteralPath $dllSymbols -Destination $run }
    $ini = Join-Path $runtime 'ern-relics.ini'
    if ($PackageDirectory -and -not (Test-Path -LiteralPath $ini)) { throw 'The package is missing ern-relics.ini.' }
    if (-not (Test-Path -LiteralPath $ini)) { $ini = Join-Path $projectRoot 'config/ern-relics.ini' }
    Copy-Item -LiteralPath $ini -Destination $run
    Get-ChildItem -LiteralPath $build -Filter 'clang_rt.asan*_dynamic-x86_64.dll' -File |
        Copy-Item -Destination $run
    $previousRegulation = $env:ERN_RELICS_TEST_REGULATION
    $previousGameDirectory = $env:ERN_RELICS_TEST_GAME_DIRECTORY
    try {
        $env:ERN_RELICS_TEST_GAME_DIRECTORY = if ($GameDirectory) { $GameDirectory } else { $null }
        $env:ERN_RELICS_TEST_REGULATION = if ($RegulationPath) { $RegulationPath } else { $null }
        $mode = if ($PackageDirectory) { '--smoke-test' } else { '--self-test' }
        $process = Start-Process -FilePath (Join-Path $run 'ern-relics-host.exe') -ArgumentList $mode -WorkingDirectory $run -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'host-stdout.txt') -RedirectStandardError (Join-Path $run 'host-stderr.txt')
    } finally {
        $env:ERN_RELICS_TEST_REGULATION = $previousRegulation
        $env:ERN_RELICS_TEST_GAME_DIRECTORY = $previousGameDirectory
    }
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
        throw 'The DX12 integration test timed out.'
    }
    $process.Refresh()
    Get-Content -LiteralPath (Join-Path $run 'host-test.txt')
    if ($process.ExitCode -ne 0) { throw "DX12 integration test failed: exit $($process.ExitCode)" }
} catch {
    Get-Content -LiteralPath (Join-Path $run 'host-stderr.txt') -Tail 80 -ErrorAction SilentlyContinue
    Get-Content -LiteralPath (Join-Path $run 'ern-relics.log') -Tail 40 -ErrorAction SilentlyContinue
    throw
} finally {
    Remove-TemporaryDirectory $run
}
if ($GameDirectory) {
    $probe = Join-Path $build 'ern_relics_rules_probe.exe'
    $activeRegulation = if ($RegulationPath) { $RegulationPath } else { Join-Path $GameDirectory 'regulation.bin' }
    Push-Location -LiteralPath $projectRoot
    try {
        & uv run --project $projectRoot --locked --group assets python -m tools.assets.verify_regulation --game-dir $GameDirectory --regulation $activeRegulation --probe $probe
        if ($LASTEXITCODE -ne 0) { throw 'Installed-game relic rule audit failed.' }
    } finally {
        Pop-Location
    }
}
