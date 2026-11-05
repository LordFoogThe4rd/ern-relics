$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $projectRoot 'tools/build/output.ps1')
$fixture = New-TemporaryDirectory
$stable = 'ern-relics-1.0.0-dev'
$feature = 'ern-relics-1.0.0-some-new-feature'
$dist = Join-Path $fixture 'dist'

function Write-FixtureFile([string]$Relative, [string]$Content) {
    $path = Join-Path $fixture $Relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    [IO.File]::WriteAllText($path, $Content)
}
function Assert-Rejected([scriptblock]$Action, [string]$Message) {
    try { & $Action | Out-Null } catch {
        if ($_.Exception.Message -notlike $Message) { throw }
        return
    }
    throw "Expected rejection: $Message"
}
function Get-DistSnapshot {
    @(Get-ChildItem -LiteralPath $dist -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
        $_.FullName.Substring($fixture.Length) + ':' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }) -join "`n"
}
function Get-PackageSnapshot([string]$Directory) {
    @(Get-ChildItem -LiteralPath $Directory -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
        [IO.Path]::GetRelativePath($Directory, $_.FullName) + ':' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }) -join "`n"
}
function Assert-Package([string]$Name) {
    $package = Join-Path $dist $Name
    $expected = @('ern-relics.dll','ern-relics.ini','nightreign-relics.me3','LICENSE','ThirdPartyNotices.txt')
    $actual = @(Get-ChildItem -LiteralPath $package -Recurse -File -Force | ForEach-Object {
        [IO.Path]::GetRelativePath($package, $_.FullName)
    })
    if (Compare-Object $expected $actual) { throw 'The package must contain only the DLL, configuration, profile, and license notices.' }
    foreach ($fileName in @('LICENSE','ThirdPartyNotices.txt')) {
        if ((Get-FileHash -LiteralPath (Join-Path $package $fileName)).Hash -ne
            (Get-FileHash -LiteralPath (Join-Path $fixture $fileName)).Hash) { throw "The package changed $fileName." }
    }
    if (Get-ChildItem -LiteralPath $package -Directory -Force) { throw 'The package contains development directories.' }
    if (Get-ChildItem -LiteralPath $dist -Directory -Force | Where-Object { $_.Name -like '.stage-*' }) {
        throw 'Temporary staging directories were not removed.'
    }
    if ([IO.File]::ReadAllText((Join-Path $dist 'old.zip')) -ne 'Old archive' -or
        [IO.File]::ReadAllText((Join-Path $dist 'old-build/stale.dll')) -ne 'Old build') {
        throw 'Packaging changed another build or release archive.'
    }
}

try {
    foreach ($relative in @('LICENSE','ThirdPartyNotices.txt','config/ern-relics.ini','config/nightreign-relics.me3')) {
        Write-FixtureFile $relative 'Synthetic build fixture'
    }
    Write-FixtureFile 'src/keep.cpp' 'Source must survive output cleanup.'
    Write-FixtureFile 'compiler-output/ern-relics.dll' 'First DLL'
    Write-FixtureFile 'compiler-output/ern-relics-host.exe' 'Synthetic host'
    Write-FixtureFile 'dist/old-build/stale.dll' 'Old build'
    Write-FixtureFile 'dist/old.zip' 'Old archive'
    Write-FixtureFile "dist/$stable/stale.log" 'Old log'
    $build = Join-Path $fixture 'compiler-output'
    foreach ($script in @('build.ps1', 'check.ps1')) {
        $command = Get-Command (Join-Path $projectRoot "tools/$script")
        $label = $command.ScriptBlock.Ast.ParamBlock.Parameters |
            Where-Object { $_.Name.VariablePath.UserPath -eq 'Label' }
        if ($label.DefaultValue.SafeGetValue() -cne 'dev') {
            throw "$script must default to development output."
        }
    }
    # An explicit empty label must not overwrite a frozen versioned release either.
    $release = 'ern-relics-1.0.0'
    Write-BuildOutput -ProjectRoot $fixture -BuildDirectory $build -Name $release | Out-Null
    Write-FixtureFile "dist/$release/preserve.txt" 'Frozen release marker'
    Write-FixtureFile "dist/$release.zip" 'Frozen release archive'
    Write-FixtureFile "dist/$release.zip.sha256" 'Frozen release checksum'
    $released = Get-PackageSnapshot (Join-Path $dist $release)
    $before = Get-DistSnapshot
    Write-FixtureFile 'compiler-output/ern-relics.dll' 'Later development DLL'
    Assert-Rejected {
        Write-BuildOutput -ProjectRoot $fixture -BuildDirectory $build -Name $release
    } '*Release output already exists*'
    if ((Get-DistSnapshot) -cne $before) { throw 'A build changed preserved release output.' }
    Write-FixtureFile 'compiler-output/ern-relics.dll' 'First DLL'
    $output = Write-BuildOutput -ProjectRoot $fixture -BuildDirectory $build -Name $stable
    if ($output -ne (Join-Path $dist $stable)) { throw 'Packaging returned the wrong directory.' }
    Assert-Package $stable
    $snapshot = Get-DistSnapshot
    Write-BuildOutput -ProjectRoot $fixture -BuildDirectory $build -Name $stable | Out-Null
    if ((Get-DistSnapshot) -ne $snapshot) { throw 'Repeated builds changed the output.' }

    Remove-Item -LiteralPath (Join-Path $build 'ern-relics.dll')
    Assert-Rejected { Write-BuildOutput -ProjectRoot $fixture -BuildDirectory $build -Name $stable } '*does not exist*'
    if ((Get-DistSnapshot) -ne $snapshot) { throw 'Failed staging damaged an existing build.' }
    Assert-Package $stable
    Write-FixtureFile 'compiler-output/ern-relics.dll' 'First DLL'

    $validation = @{ Calls = 0; Snapshot = $null; Stage = $null }
    $inspectPackage = {
        param($StagedPackage)
        $validation.Calls++
        $validation.Stage = $StagedPackage
        if ((Split-Path -Parent $StagedPackage) -ne $dist -or
            (Split-Path -Leaf $StagedPackage) -notmatch '^\.stage-[0-9a-f]{32}$') {
            throw 'Validation must receive the isolated staging directory.'
        }
        $sources = @{
            'ern-relics.dll' = Join-Path $build 'ern-relics.dll'
            'ern-relics.ini' = Join-Path $fixture 'config/ern-relics.ini'
            'nightreign-relics.me3' = Join-Path $fixture 'config/nightreign-relics.me3'
            'LICENSE' = Join-Path $fixture 'LICENSE'
            'ThirdPartyNotices.txt' = Join-Path $fixture 'ThirdPartyNotices.txt'
        }
        $files = @(Get-ChildItem -LiteralPath $StagedPackage -Recurse -File | ForEach-Object {
            [IO.Path]::GetRelativePath($StagedPackage, $_.FullName)
        })
        if (Compare-Object @($sources.Keys) $files) { throw 'Validation received an incomplete package or extra files.' }
        foreach ($name in $sources.Keys) {
            if ((Get-FileHash -LiteralPath (Join-Path $StagedPackage $name)).Hash -ne
                (Get-FileHash -LiteralPath $sources[$name]).Hash) { throw "Validation received incorrect bytes for $name." }
        }
        $validation.Snapshot = Get-PackageSnapshot $StagedPackage
    }
    Write-FixtureFile 'compiler-output/ern-relics.dll' 'Candidate DLL'
    foreach ($attempt in 1..2) {
        Assert-Rejected {
            Write-BuildOutput -ProjectRoot $fixture -BuildDirectory $build -Name $stable -ValidatePackage {
                param($StagedPackage)
                & $inspectPackage $StagedPackage
                throw 'Injected package validation failure'
            }
        } 'Injected package validation failure'
        if ((Get-DistSnapshot) -ne $snapshot -or (Test-Path -LiteralPath $validation.Stage)) {
            throw 'Failed validation changed the previous package or retained staged output.'
        }
    }
    $output = Write-BuildOutput -ProjectRoot $fixture -BuildDirectory $build -Name $stable -ValidatePackage {
        param($StagedPackage)
        & $inspectPackage $StagedPackage
        'Synthetic package validation passed.'
    }
    if ($validation.Calls -ne 3 -or $output -isnot [string] -or $output -ne (Join-Path $dist $stable) -or
        (Get-PackageSnapshot $output) -ne $validation.Snapshot -or (Test-Path -LiteralPath $validation.Stage)) {
        throw 'Publication changed the validated package, skipped validation, or returned validator output.'
    }
    Assert-Package $stable
    Write-FixtureFile 'compiler-output/ern-relics.dll' 'First DLL'
    Write-BuildOutput -ProjectRoot $fixture -BuildDirectory $build -Name $stable | Out-Null

    foreach ($relative in @('src', 'dist', 'dist/../src')) {
        Assert-Rejected { Remove-GeneratedDirectory -Root $dist -Path (Join-Path $fixture $relative) } '*outside generated output*'
    }
    Assert-Rejected { Remove-TemporaryDirectory $fixture.Substring(0, $fixture.Length - 1) } '*unrecognized temporary directory*'

    $junction = Join-Path $dist "$stable/source-link"
    $hasJunction = $false
    try {
        try {
            New-Item -ItemType Junction -Path $junction -Target (Join-Path $fixture 'src') | Out-Null
            $hasJunction = $true
        } catch {
            if ($_.Exception.Message -notlike '*Access is denied*') { throw }
            Write-Host 'SKIP: this environment does not permit temporary junctions.'
        }
        if ($hasJunction) {
            Assert-Rejected { Write-BuildOutput -ProjectRoot $fixture -BuildDirectory $build -Name $stable } '*reparse points*'
            Assert-Rejected { Resolve-OutputPath (Join-Path $junction 'nested') } '*reparse point*'
        }
    } finally {
        if ($hasJunction) { Remove-Item -LiteralPath $junction -Force }
    }
    Assert-Package $stable
    if (-not (Test-Path -LiteralPath (Join-Path $fixture 'src/keep.cpp'))) { throw 'Cleanup removed source.' }

    Write-BuildOutput -ProjectRoot $fixture -BuildDirectory $build -Name $feature | Out-Null
    Write-FixtureFile 'compiler-output/ern-relics.dll' 'Latest DLL'
    Write-BuildOutput -ProjectRoot $fixture -BuildDirectory $build -Name $feature | Out-Null
    Assert-Package $stable
    Assert-Package $feature
    if ([IO.File]::ReadAllText((Join-Path $dist "$stable/ern-relics.dll")) -ne 'First DLL' -or
        [IO.File]::ReadAllText((Join-Path $dist "$feature/ern-relics.dll")) -ne 'Latest DLL') {
        throw 'The feature build replaced another build or kept an older DLL.'
    }
    if ((Get-PackageSnapshot (Join-Path $dist $release)) -cne $released -or
        [IO.File]::ReadAllText((Join-Path $dist "$release.zip")) -cne 'Frozen release archive' -or
        [IO.File]::ReadAllText((Join-Path $dist "$release.zip.sha256")) -cne 'Frozen release checksum') {
        throw 'Development builds changed a preserved release package, archive, or checksum.'
    }
    Write-Host 'PASS: build and check default to dev; versioned releases are preserved; named packages coexist with other builds and ZIPs; staged validation publishes exact tested bytes and preserves previous output on failure; repeated writes, failed staging, traversal, and junction cleanup are safe.'
} finally {
    Remove-TemporaryDirectory $fixture
}
