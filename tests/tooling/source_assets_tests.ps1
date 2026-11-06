$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $projectRoot 'tools/build/output.ps1')
$temporaryRoot = New-TemporaryDirectory
$fakeAsset = Join-Path $temporaryRoot 'relic-icons.png'
[IO.File]::WriteAllBytes($fakeAsset, [byte[]]@(0))
$rejected = $false
try {
    & (Join-Path $projectRoot 'tools/checks/source-assets.ps1') -Root $temporaryRoot -AllFiles
} catch {
    if ($_.Exception.Message -notlike '*assets are not allowed*') { throw }
    $rejected = $true
}
if (-not $rejected) { throw 'The source guard accepted an image asset.' }
Remove-Item -LiteralPath $fakeAsset

# Documentation screenshots are allowed only directly inside docs/screenshots.
$screenshotDirectory = Join-Path $temporaryRoot 'docs/screenshots'
New-Item -ItemType Directory -Path $screenshotDirectory -Force | Out-Null
[IO.File]::WriteAllBytes((Join-Path $screenshotDirectory 'editor.webp'), [byte[]]@(0))
& (Join-Path $projectRoot 'tools/checks/source-assets.ps1') -Root $temporaryRoot -AllFiles
foreach ($relativePath in @('docs/relic-icons.webp', 'docs/screenshots/data/relic-icons.webp', 'docs/screenshots/relic-icons.dds', 'docs/screenshots/original.png')) {
    $asset = Join-Path $temporaryRoot $relativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $asset) -Force | Out-Null
    [IO.File]::WriteAllBytes($asset, [byte[]]@(0))
    $rejected = $false
    try {
        & (Join-Path $projectRoot 'tools/checks/source-assets.ps1') -Root $temporaryRoot -AllFiles
    } catch {
        if ($_.Exception.Message -notlike '*assets are not allowed*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "The source guard accepted an asset outside the screenshot exception: $relativePath" }
    Remove-Item -LiteralPath $asset
}

# Source code may live in data groups; exported catalogues may not.
$sourceDirectory = Join-Path $temporaryRoot 'src/data'
New-Item -ItemType Directory -Path $sourceDirectory -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $sourceDirectory 'decoder.cpp'), '// Synthetic source fixture')
& (Join-Path $projectRoot 'tools/checks/source-assets.ps1') -Root $temporaryRoot -AllFiles
$catalogue = Join-Path $sourceDirectory 'catalogue.json'
[IO.File]::WriteAllText($catalogue, '{}')
$rejected = $false
try {
    & (Join-Path $projectRoot 'tools/checks/source-assets.ps1') -Root $temporaryRoot -AllFiles
} catch {
    if ($_.Exception.Message -notlike '*assets are not allowed*') { throw }
    $rejected = $true
}
if (-not $rejected) { throw 'The source guard accepted an exported catalogue in a data group.' }
Remove-Item -LiteralPath $catalogue

# A downloaded source ZIP has no .git. Bootstrap still fetches dependency fixtures.
$dependencyDirectory = Join-Path $temporaryRoot 'third_party/bcdec/test_images'
New-Item -ItemType Directory -Path $dependencyDirectory -Force | Out-Null
$dependencyAsset = Join-Path $dependencyDirectory 'dice_bc7.dds'
[IO.File]::WriteAllBytes($dependencyAsset, [byte[]]@(0))
foreach ($name in @('dist', 'output', '.venv')) {
    $localDirectory = Join-Path $temporaryRoot $name
    New-Item -ItemType Directory -Path $localDirectory | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $localDirectory 'local.bin'), [byte[]]@(0))
}
& (Join-Path $projectRoot 'tools/checks/source-assets.ps1') -Root $temporaryRoot
$rejected = $false
try {
    & (Join-Path $projectRoot 'tools/checks/source-assets.ps1') -Root $temporaryRoot -AllFiles
} catch {
    if ($_.Exception.Message -notlike '*assets are not allowed*') { throw }
    $rejected = $true
}
if (-not $rejected) { throw 'The strict archive guard ignored a bundled dependency asset.' }
Remove-Item -LiteralPath $dependencyAsset
foreach ($name in @('dist', 'output', '.venv')) {
    Remove-Item -LiteralPath (Join-Path $temporaryRoot "$name/local.bin")
}

function Invoke-FixtureGit([string[]]$GitArguments) {
    & git -C $temporaryRoot -c user.name=Fixture -c user.email=fixture@example.invalid -c commit.gpgsign=false -c core.autocrlf=false -c "core.hooksPath=$temporaryRoot/no-hooks" @GitArguments
    if ($LASTEXITCODE -ne 0) { throw "Fixture Git command failed: $GitArguments" }
}
function Assert-HistoryRejected {
    $rejected = $false
    try {
        & (Join-Path $projectRoot 'tools/checks/source-assets.ps1') -Root $temporaryRoot -History
    } catch {
        if ($_.Exception.Message -notlike '*assets are not allowed in Git history*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'The history guard accepted a retained asset.' }
}

Invoke-FixtureGit -GitArguments @('init', '--quiet', '--initial-branch=clean', '--template=')
[IO.File]::WriteAllText((Join-Path $temporaryRoot '.gitignore'), "/third_party/`n")
[IO.File]::WriteAllBytes($dependencyAsset, [byte[]]@(0))
& (Join-Path $projectRoot 'tools/checks/source-assets.ps1') -Root $temporaryRoot
Remove-Item -LiteralPath $dependencyAsset
[IO.File]::WriteAllText((Join-Path $temporaryRoot 'fixture.txt'), 'Synthetic fixture')
Invoke-FixtureGit -GitArguments @('add', 'fixture.txt', '.gitignore', 'docs/screenshots/editor.webp')
Invoke-FixtureGit -GitArguments @('commit', '--quiet', '-m', 'test: source-only fixture')
& (Join-Path $projectRoot 'tools/checks/source-assets.ps1') -Root $temporaryRoot -History
$linkedCheckout = Join-Path $temporaryRoot 'linked checkout'
Invoke-FixtureGit -GitArguments @('worktree', 'add', '--quiet', '--detach', $linkedCheckout)
if (-not (Test-Path -LiteralPath (Join-Path $linkedCheckout '.git') -PathType Leaf)) {
    throw 'The worktree fixture did not create a .git file.'
}
& (Join-Path $projectRoot 'tools/checks/source-assets.ps1') -Root $linkedCheckout
Invoke-FixtureGit -GitArguments @('switch', '--quiet', '-c', 'asset-fixture')
[IO.File]::WriteAllBytes($fakeAsset, [byte[]]@(0))
Invoke-FixtureGit -GitArguments @('add', 'relic-icons.png')
Invoke-FixtureGit -GitArguments @('commit', '--quiet', '-m', 'test: synthetic asset fixture')
$assetTree = Invoke-FixtureGit -GitArguments @('rev-parse', 'HEAD^{tree}')
Invoke-FixtureGit -GitArguments @('switch', '--quiet', 'clean')
& (Join-Path $projectRoot 'tools/checks/source-assets.ps1') -Root $temporaryRoot
Assert-HistoryRejected
Invoke-FixtureGit -GitArguments @('branch', '--delete', '--force', 'asset-fixture')
Assert-HistoryRejected
Invoke-FixtureGit -GitArguments @('update-ref', 'refs/snapshots/asset-fixture', $assetTree)
Invoke-FixtureGit -GitArguments @('reflog', 'expire', '--expire=now', '--all')
Assert-HistoryRejected
Invoke-FixtureGit -GitArguments @('update-ref', '-d', 'refs/snapshots/asset-fixture')
& (Join-Path $projectRoot 'tools/checks/source-assets.ps1') -Root $temporaryRoot -History

Remove-TemporaryDirectory $temporaryRoot
Write-Host 'PASS: Git and ZIP sources allow documentation screenshots and skip fetched dependencies; strict archives and Git history reject game assets.'
