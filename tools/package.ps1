[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+(?:-[a-z0-9]+(?:[.-][a-z0-9]+)*)?$')][string]$Version,
    [string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build/output.ps1')
$source = Resolve-OutputPath (Resolve-Path -LiteralPath $PackageDirectory).Path
$dist = Resolve-OutputPath $OutputDirectory
$name = "ern-relics-$Version"
$package = Join-Path $dist $name
$archive = Join-Path $dist "$name.zip"
$checksum = "$archive.sha256"
$expected = @('ern-relics.dll', 'ern-relics.ini', 'nightreign-relics.me3', 'LICENSE', 'ThirdPartyNotices.txt')
$items = @(Get-ChildItem -LiteralPath $source -Force)
if ((Compare-Object $expected @($items.Name)) -or
    ($items | Where-Object { $_.PSIsContainer -or $_.Length -eq 0 -or ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) })) {
    throw 'Package only a clean build containing the expected release files. Keep runtime files outside build output.'
}
foreach ($path in @($package, $archive, $checksum)) {
    if (Test-Path -LiteralPath $path) { throw "Release output already exists; it will not be replaced: $path" }
}
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$stage = Resolve-OutputPath (Join-Path $dist ('.stage-' + [guid]::NewGuid().ToString('N')))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    $stagedPackage = Join-Path $stage $name
    New-Item -ItemType Directory -Path $stagedPackage | Out-Null
    foreach ($fileName in $expected) {
        Copy-Item -LiteralPath (Join-Path $source $fileName) -Destination $stagedPackage
    }
    $stagedArchive = Join-Path $stage "$name.zip"
    [IO.Compression.ZipFile]::CreateFromDirectory($stagedPackage, $stagedArchive)
    $zip = [IO.Compression.ZipFile]::OpenRead($stagedArchive)
    try {
        if (Compare-Object $expected @($zip.Entries.FullName)) { throw 'The ZIP contains unexpected or missing files.' }
        foreach ($entry in $zip.Entries) {
            $stream = $entry.Open()
            try {
                $actual = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))
            } finally {
                $stream.Dispose()
            }
            $original = Get-FileHash -LiteralPath (Join-Path $stagedPackage $entry.FullName) -Algorithm SHA256
            if ($actual -cne $original.Hash) { throw "The ZIP changed $($entry.FullName)." }
        }
    } finally {
        $zip.Dispose()
    }
    $digest = (Get-FileHash -LiteralPath $stagedArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    $stagedChecksum = "$stagedArchive.sha256"
    [IO.File]::WriteAllText($stagedChecksum, "$digest  $name.zip`n", [Text.UTF8Encoding]::new($false))
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    # The moves refuse existing destinations, including collisions after preflight.
    [IO.Directory]::Move($stagedPackage, $package)
    [IO.File]::Move($stagedArchive, $archive)
    [IO.File]::Move($stagedChecksum, $checksum)
    [pscustomobject]@{ Package = $package; Archive = $archive; SHA256 = $digest; Checksum = $checksum }
} finally {
    Remove-GeneratedDirectory -Root $dist -Path $stage
}
