$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $projectRoot 'tools/build/output.ps1')
$fixture = New-TemporaryDirectory
$caller = Join-Path $fixture 'Caller directory'
$game = Join-Path $caller 'Game install'
$regulation = Join-Path $fixture 'Mods/custom regulation.bin'
$build = Join-Path $caller 'Local build'
$package = Join-Path $caller 'Package [smoke]'
$capture = Join-Path $caller 'audit-call.json'
$previousGameDirectory = $env:ERN_RELICS_TEST_GAME_DIRECTORY
$previousRegulation = $env:ERN_RELICS_TEST_REGULATION
$hostState = @{ Calls = 0; ExitCode = 0 }

# The host is unrelated to argument resolution. Capture its inputs without DX12 or game files.
function Start-Process {
    param($FilePath, $ArgumentList, $WorkingDirectory, $WindowStyle, [switch]$PassThru, $RedirectStandardOutput, $RedirectStandardError)
    if ($env:ERN_RELICS_TEST_GAME_DIRECTORY -ne $expectedHostGame -or $env:ERN_RELICS_TEST_REGULATION -ne $expectedHostRegulation) {
        throw 'The host received incorrect paths.'
    }
    $hostState.Calls++
    $expectedMode = if ($expectedPackage) { '--smoke-test' } else { '--self-test' }
    if ($ArgumentList -cne $expectedMode) { throw 'Verification selected the wrong host test mode.' }
    $runtime = if ($expectedPackage) { $package } else { $build }
    if ([IO.File]::ReadAllText((Join-Path $WorkingDirectory 'ern-relics.dll')) -cne
        [IO.File]::ReadAllText((Join-Path $runtime 'ern-relics.dll'))) {
        throw 'Verification did not stage the selected DLL unchanged.'
    }
    $expectedIni = if ($expectedPackage) { Join-Path $package 'ern-relics.ini' } else { Join-Path $projectRoot 'config/ern-relics.ini' }
    if ([IO.File]::ReadAllText((Join-Path $WorkingDirectory 'ern-relics.ini')) -cne
        [IO.File]::ReadAllText($expectedIni)) {
        throw 'Verification did not stage the selected configuration unchanged.'
    }
    $files = @(Get-ChildItem -LiteralPath $WorkingDirectory -File | ForEach-Object { $_.Name })
    $expectedFiles = @('ern-relics-host.exe','ern-relics.dll','ern-relics.ini','ern-relics-host.pdb','clang_rt.asan_dynamic-x86_64.dll')
    $expectedDllSymbols = Join-Path $runtime 'ern-relics.pdb'
    if (Test-Path -LiteralPath $expectedDllSymbols) {
        $expectedFiles += 'ern-relics.pdb'
        if ([IO.File]::ReadAllText((Join-Path $WorkingDirectory 'ern-relics.pdb')) -cne
            [IO.File]::ReadAllText($expectedDllSymbols)) { throw 'Verification selected DLL symbols from another build.' }
    }
    if (Compare-Object $expectedFiles $files) { throw 'Verification must stage only its runtime files, symbols, configuration, and sanitizer runtime.' }
    if ($RedirectStandardOutput -ne (Join-Path $WorkingDirectory 'host-stdout.txt') -or
        $RedirectStandardError -ne (Join-Path $WorkingDirectory 'host-stderr.txt')) { throw 'Host output must be captured for sanitizer diagnostics.' }
    [IO.File]::WriteAllText((Join-Path $WorkingDirectory 'host-test.txt'), 'Synthetic host passed.')
    $process = [pscustomobject]@{ ExitCode = $hostState.ExitCode }
    $process | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value { param($Timeout) return $true }
    $process | Add-Member -MemberType ScriptMethod -Name Refresh -Value {}
    return $process
}

function Assert-Rejected([scriptblock]$Action, [string]$Message) {
    try { & $Action | Out-Null } catch {
        if ($Message -and $_.Exception.Message -notlike $Message) { throw }
        return
    }
    throw "Expected rejection: $Message"
}

# Capture the real audit command without downloading tools or opening game files.
function uv {
    [IO.File]::WriteAllText($capture, (@{
        arguments = @($args)
        directory = (Get-Location).Path
    } | ConvertTo-Json))
    $global:LASTEXITCODE = 0
}

try {
    foreach ($directory in @($game, (Split-Path -Parent $regulation), $build, $package)) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }
    foreach ($file in @((Join-Path $game 'regulation.bin'), $regulation, (Join-Path $build 'ern-relics-host.exe'),
        (Join-Path $build 'ern-relics.dll'), (Join-Path $build 'ern-relics.pdb'), (Join-Path $build 'ern-relics-host.pdb'), (Join-Path $build 'clang_rt.asan_dynamic-x86_64.dll'),
        (Join-Path $build 'unrelated.obj'))) {
        [IO.File]::WriteAllText($file, 'Synthetic path fixture')
    }
    [IO.File]::WriteAllText((Join-Path $package 'ern-relics.dll'), 'Packaged production DLL fixture')
    [IO.File]::WriteAllText((Join-Path $package 'ern-relics.ini'), 'Packaged configuration fixture')
    $env:ERN_RELICS_TEST_GAME_DIRECTORY = 'Previous game directory'
    $env:ERN_RELICS_TEST_REGULATION = 'Previous regulation'

    Push-Location -LiteralPath $caller
    try {
        $cases = @(
            @{ GameDirectory = '.\Game install'; RegulationPath = '..\Mods\custom regulation.bin'; BuildDirectory = $build },
            @{ GameDirectory = '.\Game install'; BuildDirectory = '.\Local build' },
            @{ GameDirectory = $game; RegulationPath = $regulation; BuildDirectory = $build },
            @{ BuildDirectory = '.\Local build' },
            @{ BuildDirectory = '.\Local build'; PackageDirectory = '.\Package [smoke]' },
            @{ BuildDirectory = $build; PackageDirectory = $package }
        )
        foreach ($case in $cases) {
            $expectedPackage = [bool]$case.PackageDirectory
            $expectedHostGame = if ($case.GameDirectory) { $game } else { $null }
            $expectedHostRegulation = if ($case.RegulationPath) { $regulation } else { $null }
            $expectedAuditRegulation = if ($case.RegulationPath) { $regulation } else { Join-Path $game 'regulation.bin' }
            & (Join-Path $projectRoot 'tools/verify.ps1') @case | Out-Null
            if ($case.GameDirectory) {
                $actual = Get-Content -LiteralPath $capture -Raw | ConvertFrom-Json
                $expected = @('run', '--project', $projectRoot, '--locked', '--group', 'assets', 'python',
                    '-m', 'tools.assets.verify_regulation', '--game-dir', $game,
                    '--regulation', $expectedAuditRegulation, '--probe', (Join-Path $build 'ern_relics_rules_probe.exe'))
                if ($actual.directory -ne $projectRoot -or ($actual.arguments -join "`n") -cne ($expected -join "`n")) {
                    throw "The audit received incorrect paths or working directory: $($actual | ConvertTo-Json -Compress)"
                }
                Remove-Item -LiteralPath $capture
            } elseif (Test-Path -LiteralPath $capture) {
                throw 'Host-only verification ran the optional asset audit.'
            }
            if ((Get-Location).Path -ne $caller) { throw 'Verification changed the caller directory.' }
            if ($env:ERN_RELICS_TEST_GAME_DIRECTORY -ne 'Previous game directory' -or $env:ERN_RELICS_TEST_REGULATION -ne 'Previous regulation') {
                throw 'Verification changed the caller environment.'
            }
        }
        $expectedPackage = $true
        $expectedHostGame = $expectedHostRegulation = $null
        [IO.File]::WriteAllText((Join-Path $package 'ern-relics.pdb'), 'Packaged production symbols fixture')
        & (Join-Path $projectRoot 'tools/verify.ps1') -BuildDirectory $build -PackageDirectory $package | Out-Null
        $callsBeforeFailure = $hostState.Calls
        Assert-Rejected { & (Join-Path $projectRoot 'tools/verify.ps1') -BuildDirectory $build -PackageDirectory '.\Missing package' } '*does not exist*'
        Assert-Rejected { & (Join-Path $projectRoot 'tools/verify.ps1') -BuildDirectory $build -PackageDirectory $package -GameDirectory $game } 'Package smoke tests use synthetic data only.'
        Remove-Item -LiteralPath (Join-Path $package 'ern-relics.dll')
        Assert-Rejected { & (Join-Path $projectRoot 'tools/verify.ps1') -BuildDirectory $build -PackageDirectory $package } '*does not exist*'
        [IO.File]::WriteAllText((Join-Path $package 'ern-relics.dll'), 'Packaged production DLL fixture')
        Remove-Item -LiteralPath (Join-Path $package 'ern-relics.ini')
        Assert-Rejected { & (Join-Path $projectRoot 'tools/verify.ps1') -BuildDirectory $build -PackageDirectory $package } 'The package is missing ern-relics.ini.'
        [IO.File]::WriteAllText((Join-Path $package 'ern-relics.ini'), 'Packaged configuration fixture')
        if ($hostState.Calls -ne $callsBeforeFailure) { throw 'An incomplete package or invalid smoke request launched the host.' }
        $hostState.ExitCode = 7
        Assert-Rejected { & (Join-Path $projectRoot 'tools/verify.ps1') -BuildDirectory $build -PackageDirectory $package } 'DX12 integration test failed: exit 7'
        if ($hostState.Calls -ne $callsBeforeFailure + 1) { throw 'The failed smoke host did not run.' }
        if ((Get-Location).Path -ne $caller -or $env:ERN_RELICS_TEST_GAME_DIRECTORY -ne 'Previous game directory' -or
            $env:ERN_RELICS_TEST_REGULATION -ne 'Previous regulation') { throw 'Failed package verification changed the caller state.' }
        if (Test-Path -LiteralPath $capture) { throw 'Package smoke verification ran the optional game audit.' }
    } finally {
        Pop-Location
    }
    Write-Host 'PASS: caller-relative game, regulation, build, and package paths; exact packaged DLL and configuration; isolated smoke mode; missing package inputs and host failures rejected; caller directory and environment preserved.'
} finally {
    $env:ERN_RELICS_TEST_GAME_DIRECTORY = $previousGameDirectory
    $env:ERN_RELICS_TEST_REGULATION = $previousRegulation
    Remove-TemporaryDirectory $fixture
}
