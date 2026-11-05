#requires -Version 7.4
# Native tools remain outside the uv environment. Prefer the Visual Studio installation.
function Get-NativeToolchain {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ Build Tools and the Windows SDK.' }
    $installation = & $vswhere -latest -products '*' -version '[17.14,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.Windows11SDK.26100 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'Install Visual Studio 2022 17.14 or Visual Studio 2026 with the components in .vsconfig.' }
    $cmake = Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
    $ninja = Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
    if (-not (Test-Path -LiteralPath $ninja)) { $ninja = (Get-Command ninja -ErrorAction Stop).Source }
    $ninjaVersion = & $ninja --version
    if ($LASTEXITCODE -ne 0 -or $ninjaVersion -notmatch '^(\d+\.\d+\.\d+)' -or [version]$Matches[1] -lt [version]'1.11.0') {
        throw "Install Ninja 1.11 or newer. Found: $ninjaVersion"
    }
    [pscustomobject]@{
        Installation = $installation
        CMake = $cmake
        Ninja = $ninja
        LLVM = Join-Path $installation 'VC/Tools/Llvm/x64/bin'
    }
}

function Get-PinnedClangTidy($Toolchain, [string]$Python) {
    # One LLVM version policy for the formatter and analyzer, owned by the uv dependency pin.
    $expected = & $Python -c 'import importlib.metadata; print(importlib.metadata.version("clang-format"))'
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read the pinned LLVM version. Run tools/bootstrap.ps1 first.' }
    $candidates = @(
        Join-Path $Toolchain.LLVM 'clang-tidy.exe'
        Get-Command clang-tidy -All -CommandType Application -ErrorAction SilentlyContinue | ForEach-Object Source
    ) | Select-Object -Unique
    $found = @()
    foreach ($tidy in $candidates) {
        if (-not (Test-Path -LiteralPath $tidy)) { continue }
        $version = & $tidy --version
        if ($LASTEXITCODE -eq 0 -and ($version -join "`n") -match '\bLLVM version (\d+\.\d+\.\d+)') {
            if ($Matches[1] -eq $expected) { return $tidy }
            $found += "${tidy}: $($Matches[1])"
        }
    }
    throw "Install LLVM $expected, matching pyproject.toml, and add its bin directory to PATH. Found: $($found -join '; ')"
}
