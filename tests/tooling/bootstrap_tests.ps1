$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $projectRoot 'tools/build/output.ps1')
$temporaryRoot = New-TemporaryDirectory
$savedGitConfigGlobal = $env:GIT_CONFIG_GLOBAL
$savedGitConfigNoSystem = $env:GIT_CONFIG_NOSYSTEM

# Only Python/toolchain setup is stubbed. Dependency operations use real local Git.
function uv {
    if ($args -notcontains '--locked') { throw 'Bootstrap must keep Python tooling locked.' }
    $global:LASTEXITCODE = 0
    if ($args[0] -eq 'run') { 'unused-python' }
}
function Invoke-FixtureGit([string]$Directory, [string[]]$GitArguments) {
    & git -C $Directory -c user.name=Fixture -c user.email=fixture@example.invalid -c commit.gpgsign=false @GitArguments
    if ($LASTEXITCODE -ne 0) { throw "Fixture Git command failed: $GitArguments" }
}
function New-BootstrapFixture([string]$Name) {
    $root = Join-Path $temporaryRoot $Name
    New-Item -ItemType Directory -Path (Join-Path $root 'tools/build') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot 'tools/bootstrap.ps1') -Destination (Join-Path $root 'tools/bootstrap.ps1')
    [IO.File]::WriteAllText((Join-Path $root 'tools/build/toolchain.ps1'), @'
function Get-NativeToolchain { [pscustomobject]@{ LLVM = 'unused' } }
function Get-PinnedClangTidy { param($Toolchain, $Python) 'unused-clang-tidy' }
'@)
    $pins = foreach ($dependencyName in @('first','second')) {
        [pscustomobject]@{ Name = $dependencyName; Url = $origin; Commit = $revision }
    }
    [IO.File]::WriteAllText((Join-Path $root 'sources.lock.json'), ($pins | ConvertTo-Json))
    return $root
}
function Assert-PinnedDependencies([string]$Root) {
    foreach ($name in @('first','second')) {
        $directory = Join-Path $Root "third_party/$name"
        $head = Invoke-FixtureGit $directory @('rev-parse','HEAD')
        if ($head -ne $revision -or [IO.File]::ReadAllText((Join-Path $directory 'fixture.txt')) -ne 'Pinned dependency') {
            throw 'Bootstrap did not fetch and check out the locked revision.'
        }
        $branch = Invoke-FixtureGit $directory @('rev-parse','--abbrev-ref','HEAD')
        if ($branch -ne 'HEAD') { throw 'Bootstrap must leave dependencies detached at their pins.' }
        $changes = Invoke-FixtureGit $directory @('status','--porcelain')
        if ($changes) { throw 'Bootstrap left a dirty dependency.' }
    }
}
function Assert-BootstrapRejected([string]$Root, [string]$Message) {
    try { & (Join-Path $Root 'tools/bootstrap.ps1') 2>$null | Out-Null } catch {
        if ($_.Exception.Message -notlike $Message) { throw }
        return
    }
    throw "Expected bootstrap rejection: $Message"
}

try {
    # Ignore machine-specific Git configuration; no remotes leave this fixture.
    $env:GIT_CONFIG_GLOBAL = Join-Path $temporaryRoot 'empty.gitconfig'
    $env:GIT_CONFIG_NOSYSTEM = '1'
    [IO.File]::WriteAllText($env:GIT_CONFIG_GLOBAL, '')
    $origin = Join-Path $temporaryRoot 'local origin'
    New-Item -ItemType Directory -Path $origin | Out-Null
    Invoke-FixtureGit $origin @('init','--quiet','--template=')
    [IO.File]::WriteAllText((Join-Path $origin 'fixture.txt'), 'Pinned dependency')
    Invoke-FixtureGit $origin @('add','fixture.txt')
    Invoke-FixtureGit $origin @('commit','--quiet','-m','test: pinned dependency')
    $revision = Invoke-FixtureGit $origin @('rev-parse','HEAD')
    [IO.File]::WriteAllText((Join-Path $origin 'fixture.txt'), 'Unpinned newer dependency')
    Invoke-FixtureGit $origin @('commit','--quiet','-am','test: newer dependency')

    foreach ($nativeErrors in @($true,$false)) {
        $root = New-BootstrapFixture "native-errors-$nativeErrors"
        $PSNativeCommandUseErrorActionPreference = $nativeErrors
        # Missing checkouts exercise init and the expected unborn HEAD failure.
        & (Join-Path $root 'tools/bootstrap.ps1') | Out-Null
        if ($PSNativeCommandUseErrorActionPreference -ne $nativeErrors) {
            throw 'Bootstrap changed the caller native-command preference.'
        }
        Assert-PinnedDependencies $root
        # A second invocation must also accept existing clean pinned checkouts.
        & (Join-Path $root 'tools/bootstrap.ps1') | Out-Null
        Assert-PinnedDependencies $root
    }

    $first = Join-Path $root 'third_party/first'
    $second = Join-Path $root 'third_party/second'
    $index = Join-Path $first '.git/index'
    $indexBytes = [IO.File]::ReadAllBytes($index)
    [IO.File]::WriteAllBytes($index, [byte[]]@(0))
    $changes = @(& git -C $first status --porcelain 2>$null)
    if ($LASTEXITCODE -eq 0 -or $changes.Count -ne 0) { throw 'Fixture must fail Git status without porcelain output.' }
    # A later clean dependency used to erase the first dependency's exit code.
    Invoke-FixtureGit $second @('status','--porcelain') | Out-Null
    Assert-BootstrapRejected $root '*Cannot inspect dependency changes:*'
    if ($LASTEXITCODE -eq 0) { throw 'A later command erased the failed dependency status.' }
    [IO.File]::WriteAllBytes($index, $indexBytes)

    Invoke-FixtureGit $first @('remote','remove','origin')
    Assert-BootstrapRejected $root '*Cannot read dependency origin:*'
    if ($LASTEXITCODE -eq 0) { throw 'Bootstrap lost the failed origin read.' }
    Invoke-FixtureGit $first @('remote','add','origin',$origin)

    $localChange = Join-Path $first 'fixture.txt'
    [IO.File]::WriteAllText($localChange, 'Local changes must survive')
    Assert-BootstrapRejected $root '*Dependency has local changes:*'
    if ([IO.File]::ReadAllText($localChange) -ne 'Local changes must survive') {
        throw 'Bootstrap changed a dirty dependency.'
    }
} finally {
    $env:GIT_CONFIG_GLOBAL = $savedGitConfigGlobal
    $env:GIT_CONFIG_NOSYSTEM = $savedGitConfigNoSystem
    Remove-TemporaryDirectory $temporaryRoot
}
Write-Host 'PASS: bootstrap checks Git failures, preserves local changes, and initializes locked dependencies offline.'
