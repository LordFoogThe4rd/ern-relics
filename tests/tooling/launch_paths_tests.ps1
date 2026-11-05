$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $projectRoot 'tools/build/output.ps1')
$fixture = New-TemporaryDirectory
$caller = Join-Path $fixture 'Caller directory'
$me3 = Join-Path $caller 'Me3 [local]/me3.exe'
$game = Join-Path $caller 'Game local/nightreign.exe'
$package = Join-Path $fixture 'dist/ern-relics-1.0.0-some-new-feature'
$capture = @{ Call = $null }

function Get-Process { return @() }
function Start-Process {
    param($FilePath, $ArgumentList, $WorkingDirectory, $WindowStyle, $RedirectStandardOutput, $RedirectStandardError, [switch]$PassThru)
    $capture.Call = [pscustomobject]@{
        FilePath = $FilePath
        Arguments = $ArgumentList
        WorkingDirectory = $WorkingDirectory
        WindowStyle = $WindowStyle
    }
    return [pscustomobject]@{ Id = 0 }
}

try {
    foreach ($directory in @($package, (Split-Path -Parent $me3), (Split-Path -Parent $game))) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }
    foreach ($path in @($me3, $game, (Join-Path $package 'ern-relics.dll'))) {
        [IO.File]::WriteAllText($path, 'Synthetic fixture, not executable.')
    }
    $sentinel = Join-Path $package 'keep.txt'
    [IO.File]::WriteAllText($sentinel, 'Preserve existing package files.')
    $launch = Join-Path $projectRoot 'tools/game/launch-test.ps1'
    $profile = Join-Path $package 'nightreign-relics-test.me3'
    Push-Location -LiteralPath $caller
    try {
        foreach ($case in @(
            @{ PackageDirectory = '../dist/ern-relics-1.0.0-some-new-feature'; Me3Exe = './Me3 [local]/me3.exe'; GameExe = './Game local/nightreign.exe' },
            @{ PackageDirectory = $package; Me3Exe = $me3; GameExe = $game }
        )) {
            $capture.Call = $null
            & $launch @case -Check | Out-Null
            if ($capture.Call -or -not (Test-Path -LiteralPath $profile)) { throw 'Check mode must write a profile without launching.' }
            $content = Get-Content -LiteralPath $profile -Raw
            $nativeToml = ConvertTo-Json -InputObject (Join-Path $package 'ern-relics.dll').Replace('\', '/') -Compress
            if (-not $content.Contains("path = $nativeToml") -or
                -not $content.Contains('savefile = "ern-relics-test.sl2"') -or
                -not $content.Contains('start_online = false')) { throw 'The profile must select this named package and the offline test save.' }
            & $launch @case | Out-Null
            $actual = $capture.Call
            $expectedArguments = @('launch', '--profile', ('"{0}"' -f $profile), '--exe', ('"{0}"' -f $game))
            if ($actual.FilePath -cne $me3 -or $actual.WorkingDirectory -cne $package -or
                $actual.WindowStyle -ne 'Hidden' -or
                ($actual.Arguments -join "`n") -cne ($expectedArguments -join "`n")) {
                throw "Launch received incorrect paths: $($actual | ConvertTo-Json -Compress)"
            }
            if ((Get-Location).Path -cne $caller -or -not (Test-Path -LiteralPath $sentinel)) { throw 'Launch changed the caller directory or removed package files.' }
        }
    } finally {
        Pop-Location
    }
    Write-Host 'PASS: explicit named packages, caller-relative paths, literal brackets, quoting, offline profile, check mode, and preserved package files. No process launched.'
} finally {
    Remove-TemporaryDirectory $fixture
}
