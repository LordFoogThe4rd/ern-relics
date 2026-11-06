# Install the analyzer in the CI runner's temporary directory, without changing Visual Studio.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$version = & uv run --project $projectRoot --locked python -c 'import importlib.metadata; print(importlib.metadata.version("clang-format"))'
if ($LASTEXITCODE -ne 0) { throw 'Cannot read the pinned LLVM version.' }
if (-not $env:RUNNER_TEMP -or -not $env:GITHUB_PATH) { throw 'Run this setup script in GitHub Actions.' }
$installer = Join-Path $env:RUNNER_TEMP "LLVM-$version-win64.exe"
$directory = Join-Path $env:RUNNER_TEMP "ern-relics-llvm/$version"
# Official LLVM 22.1.3 release asset digest. Update with the clang-format pin.
$expectedHash = '72ca54c3b7940615776d8994c0b918a1d8c4981bdcf13b439eaa86a1bd477001'
$url = "https://github.com/llvm/llvm-project/releases/download/llvmorg-$version/LLVM-$version-win64.exe"
$tidy = Join-Path $directory 'bin/clang-tidy.exe'
if (-not (Test-Path -LiteralPath $tidy)) {
    Invoke-WebRequest $url -OutFile $installer -MaximumRetryCount 3
    if ((Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash -ne $expectedHash) {
        throw 'The LLVM installer checksum differs. Check the release and the pinned digest.'
    }
    # Keep only the analyzer and its builtin headers, rather than caching all of LLVM.
    & 7z x $installer "-o$directory" 'bin/clang-tidy.exe' 'lib/clang/*' -y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot extract the LLVM installer.' }
}
$actual = & $tidy --version
if ($LASTEXITCODE -ne 0 -or ($actual -join "`n") -notmatch "\bLLVM version $([regex]::Escape($version))\b") {
    throw 'The cached analyzer does not match the pinned LLVM version.'
}
Join-Path $directory 'bin' | Out-File -LiteralPath $env:GITHUB_PATH -Encoding utf8 -Append
