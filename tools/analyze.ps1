[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [ValidateRange(1, 1024)][int]$Jobs = [Environment]::ProcessorCount
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'build/toolchain.ps1')
$python = & uv run --project $projectRoot --locked python -c 'import sys; print(sys.executable)'
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the tooling Python. Run tools/bootstrap.ps1 first.' }
$toolchain = Get-NativeToolchain
$tidy = Get-PinnedClangTidy -Toolchain $toolchain -Python $python
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$database = Join-Path $build 'compile_commands.json'
if (-not (Test-Path -LiteralPath $database)) { throw 'BuildDirectory must contain compile_commands.json. Configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON.' }
$commands = Get-Content -LiteralPath $database -Raw | ConvertFrom-Json
$files = @($commands | ForEach-Object {
    $file = [IO.Path]::GetFullPath($_.file, $_.directory)
    if ($file.StartsWith((Join-Path $projectRoot 'src') + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $file.StartsWith((Join-Path $projectRoot 'tests') + '\', [StringComparison]::OrdinalIgnoreCase)) { $file }
} | Sort-Object -Unique)
if (-not $files.Count) { throw 'The compilation database contains no project sources.' }
& $tidy --verify-config --config-file (Join-Path $projectRoot '.clang-tidy')
if ($LASTEXITCODE -ne 0) { throw 'Invalid clang-tidy configuration.' }
$results = $files | ForEach-Object -Parallel {
    $output = & $using:tidy -p $using:build --quiet $_ 2>&1
    [pscustomobject]@{ File = $_; ExitCode = $LASTEXITCODE; Output = $output }
} -ThrottleLimit $Jobs
foreach ($result in $results | Sort-Object File) {
    if ($result.Output) { $result.Output | Write-Host }
}
if ($results | Where-Object { $_.ExitCode -ne 0 }) { throw 'C++ static analysis failed.' }
Write-Host "C++ static analysis passed: $($files.Count) project sources, pinned clang-tidy"
