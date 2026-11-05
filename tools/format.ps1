[CmdletBinding()]
param(
    [switch]$Fix
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($Fix) {
    & uv run --project $projectRoot --locked ruff check --select I --fix $projectRoot
    if ($LASTEXITCODE -ne 0) { throw 'Python import formatting failed.' }
}
[string[]]$pythonFormatArguments = if ($Fix) { @() } else { '--check'; '--output-format'; 'concise' }
& uv run --project $projectRoot --locked ruff format @pythonFormatArguments $projectRoot
if ($LASTEXITCODE -ne 0) { throw 'Python formatting failed. Run tools/format.ps1 -Fix.' }
$files = @(foreach ($directory in @('src','tests')) {
    Get-ChildItem -LiteralPath (Join-Path $projectRoot $directory) -Recurse -File |
        Where-Object { $_.Extension -in '.cpp','.hpp' } | Sort-Object FullName | ForEach-Object { $_.FullName }
})
[string[]]$formatArguments = if ($Fix) { '-i' } else { '--dry-run'; '--Werror' }
& uv run --project $projectRoot --locked clang-format --style=file @formatArguments @files
if ($LASTEXITCODE -ne 0) { throw 'C++ formatting failed. Run tools/format.ps1 -Fix.' }
Write-Host "C++ formatting passed: $($files.Count) files, locked clang-format"
