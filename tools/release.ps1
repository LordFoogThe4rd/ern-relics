[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist'),
    [string]$BuildDirectory,
    [ValidateRange(0, 1024)][int]$Jobs = 0
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$version = [IO.File]::ReadAllText((Join-Path $projectRoot 'version.txt')).Trim()

& (Join-Path $PSScriptRoot 'check.ps1') -Label dev -Jobs $Jobs -BuildDirectory $BuildDirectory
$package = Join-Path $projectRoot "dist/ern-relics-$version-dev"
$release = & (Join-Path $PSScriptRoot 'package.ps1') -PackageDirectory $package -Version $version -OutputDirectory $OutputDirectory
Write-Host "Ready for Nexus Mods upload or ME3 import: $($release.Archive)"
$release
