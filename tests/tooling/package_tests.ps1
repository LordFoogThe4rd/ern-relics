$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $projectRoot 'tools/build/output.ps1')
$fixture = New-TemporaryDirectory
$source = Join-Path $fixture 'Build [candidate]'
# Hosted Windows runners keep TEMP on C: and the checkout on D:.
$outputRoot = Join-Path $projectRoot 'output'
$dist = Join-Path $outputRoot ('package-tests-' + [guid]::NewGuid().ToString('N'))
$script = Join-Path $projectRoot 'tools/package.ps1'
$files = @('ern-relics.dll', 'ern-relics.ini', 'nightreign-relics.me3', 'LICENSE', 'ThirdPartyNotices.txt')

function Assert-Rejected([scriptblock]$Action, [string]$Message) {
    try { & $Action | Out-Null } catch {
        if ($_.Exception.Message -notlike $Message) { throw }
        return
    }
    throw "Expected rejection: $Message"
}
function Get-Snapshot {
    @(Get-ChildItem -LiteralPath $dist -Recurse -File | Sort-Object FullName | ForEach-Object {
        $_.FullName + ':' + (Get-FileHash -LiteralPath $_.FullName).Hash
    }) -join "`n"
}
try {
    New-Item -ItemType Directory -Path $source | Out-Null
    foreach ($file in $files) {
        [IO.File]::WriteAllBytes((Join-Path $source $file), [byte[]](0, 1, 2, 128, 255))
    }
    $result = & $script -PackageDirectory $source -Version '1.0.0-rc.1' -OutputDirectory $dist
    $zip = [IO.Compression.ZipFile]::OpenRead($result.Archive)
    try {
        if (Compare-Object $files @($zip.Entries.FullName)) { throw 'The ZIP contains unexpected paths.' }
        foreach ($entry in $zip.Entries) {
            $stream = $entry.Open()
            try {
                $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))
            } finally {
                $stream.Dispose()
            }
            if ($hash -cne (Get-FileHash -LiteralPath (Join-Path $source $entry.FullName)).Hash -or
                $hash -cne (Get-FileHash -LiteralPath (Join-Path $result.Package $entry.FullName)).Hash) {
                throw 'The frozen package or ZIP changed a source file.'
            }
        }
    } finally {
        $zip.Dispose()
    }
    $digest = (Get-FileHash -LiteralPath $result.Archive).Hash.ToLowerInvariant()
    if ($result.SHA256 -cne $digest -or
        [IO.File]::ReadAllText($result.Checksum) -cne "$digest  ern-relics-1.0.0-rc.1.zip`n") {
        throw 'The release checksum does not match the ZIP.'
    }
    $before = Get-Snapshot
    [IO.File]::WriteAllText((Join-Path $source 'ern-relics.dll'), 'A later build')
    Assert-Rejected { & $script -PackageDirectory $source -Version '1.0.0-rc.1' -OutputDirectory $dist } '*already exists*'
    if ((Get-Snapshot) -cne $before) { throw 'Repackaging changed a preserved release.' }

    foreach ($path in @('ern-relics-1.0.1.zip', 'ern-relics-1.0.2.zip.sha256')) {
        [IO.File]::WriteAllText((Join-Path $dist $path), 'Preserve partial output')
    }
    foreach ($version in @('1.0.1', '1.0.2')) {
        $before = Get-Snapshot
        Assert-Rejected { & $script -PackageDirectory $source -Version $version -OutputDirectory $dist } '*already exists*'
        if ((Get-Snapshot) -cne $before) { throw 'A collision changed another release artifact.' }
    }
    foreach ($runtime in @('ern-relics.log', 'save.sl2', 'oo2core_9_win64.dll')) {
        $path = Join-Path $source $runtime
        [IO.File]::WriteAllText($path, 'Local runtime data')
        Assert-Rejected { & $script -PackageDirectory $source -Version '1.0.3' -OutputDirectory $dist } '*clean build*'
        Remove-Item -LiteralPath $path
    }
    $presets = Join-Path $source 'presets'
    New-Item -ItemType Directory -Path $presets | Out-Null
    Assert-Rejected { & $script -PackageDirectory $source -Version '1.0.3' -OutputDirectory $dist } '*clean build*'
    Remove-Item -LiteralPath $presets
    Remove-Item -LiteralPath (Join-Path $source 'LICENSE')
    Assert-Rejected { & $script -PackageDirectory $source -Version '1.0.3' -OutputDirectory $dist } '*clean build*'
    if (Test-Path -LiteralPath (Join-Path $dist 'ern-relics-1.0.3')) { throw 'An invalid package published release output.' }
    Write-Host 'PASS: release ZIP bytes and SHA-256 match the frozen package; existing outputs, missing notices, runtime data, and extra directories are rejected.'
} finally {
    Remove-GeneratedDirectory -Root $outputRoot -Path $dist
    Remove-TemporaryDirectory $fixture
}
