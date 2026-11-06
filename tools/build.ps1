[CmdletBinding()]
param(
    [ValidateSet('debug', 'release', 'asan')][string]$Preset = 'release',
    [ValidatePattern('^(?:[a-z0-9]+(?:-[a-z0-9]+)*)?$')][string]$Label = 'dev',
    [string]$BuildDirectory,
    [ValidateRange(0, 1024)][int]$Jobs = 0
)
$ErrorActionPreference = 'Stop'
$Preset = $Preset.ToLowerInvariant()
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'build/output.ps1')
. (Join-Path $PSScriptRoot 'build/toolchain.ps1')
$python = & uv run --project $projectRoot --locked python -c 'import sys; print(sys.executable)'
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the tooling Python. Run tools/bootstrap.ps1 first.' }
$toolchain = Get-NativeToolchain
$temporary = -not $BuildDirectory
$buildDir = if ($temporary) { New-TemporaryDirectory } else {
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
}
$runtimeBuildDir = Join-Path $buildDir 'runtime'
$driver = Join-Path $PSScriptRoot 'build/driver.cmd'
try {
    & $driver $projectRoot $toolchain.Installation $toolchain.CMake $toolchain.Ninja $Preset $python $Jobs $buildDir
    if ($LASTEXITCODE -ne 0) { throw 'Build or core verification failed.' }

    if ($Preset -ne 'asan') {
        $version = Select-String -LiteralPath (Join-Path $buildDir 'CMakeCache.txt') -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(.+)$'
        if (-not $version) { throw 'Cannot read the configured project version.' }
        $name = 'ern-relics-' + $version.Matches.Groups[1].Value
        if ($Preset -ne 'release') { $name += "-$Preset" }
        if ($Label) { $name += "-$Label" }
        $validatePackage = {
            param($StagedPackage)
            & (Join-Path $projectRoot 'tools/verify.ps1') -BuildDirectory $buildDir -PackageDirectory $StagedPackage
        }
        $output = Write-BuildOutput -ProjectRoot $projectRoot -BuildDirectory $runtimeBuildDir -Name $name -ValidatePackage $validatePackage
        Write-Host "Built and tested: $output"
    } else {
        Write-Host 'Built and tested with AddressSanitizer.'
    }
} finally {
    if ($temporary) { Remove-TemporaryDirectory $buildDir }
}
