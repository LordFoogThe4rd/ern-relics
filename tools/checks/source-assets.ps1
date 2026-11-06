[CmdletBinding()]
param(
    [string]$Root = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [switch]$AllFiles,
    [switch]$History
)
$ErrorActionPreference = 'Stop'
$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
function Test-ForbiddenAssetPath([string]$Path) {
    # Documentation captures are allowed; extracted game assets remain forbidden.
    if ($Path -match '^docs/screenshots/[^/]+\.webp$') { return $false }
    $sourceCode = $Path -match '^(src|tests)/.+\.(cpp|hpp|hpp\.in)$'
    return (-not $sourceCode -and $Path -match '(^|/)(data|output)/') -or
        $Path -match '\.(png|jpe?g|gif|webp|dds|tpf|bnd|dcx|param|bhd|bdt|bin|tsv|sl2|co2|rgba)$' -or
        $Path -match '(^|/)oo2core[^/]*\.dll$'
}
function Read-GitPaths([string[]]$GitArguments) {
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = [Diagnostics.ProcessStartInfo]::new('git')
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $process.StartInfo.StandardOutputEncoding = [Text.UTF8Encoding]::new($false, $true)
    foreach ($argument in @('--no-replace-objects', '-C', $resolvedRoot) + $GitArguments) {
        $process.StartInfo.ArgumentList.Add($argument)
    }
    try {
        [void]$process.Start()
        $errorOutput = $process.StandardError.ReadToEndAsync()
        $output = $process.StandardOutput.ReadToEnd()
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw "Cannot inspect Git history: $($errorOutput.GetAwaiter().GetResult())" }
        return $output.Split([char]0, [StringSplitOptions]::RemoveEmptyEntries)
    } finally {
        $process.Dispose()
    }
}
$paths = if ($AllFiles -or -not (Test-Path -LiteralPath (Join-Path $resolvedRoot '.git'))) {
    Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Force |
        ForEach-Object { $_.FullName.Substring($resolvedRoot.Length + 1).Replace('\','/') } |
        Where-Object { $AllFiles -or ($_ -notmatch '^(dist|third_party|output|\.venv)/' -and $_ -notmatch '(^|/)\.git/') }
} else {
    & git -C $resolvedRoot -c core.quotepath=false ls-files --cached --others --exclude-standard
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the source tree.' }
}
$forbidden = @($paths | Where-Object {
    (Test-Path -LiteralPath (Join-Path $resolvedRoot $_) -PathType Leaf) -and (Test-ForbiddenAssetPath $_)
})
if ($forbidden) { throw "Generated or game assets are not allowed in the source tree:`n$($forbidden -join "`n")" }
Write-Host "Source asset check passed: $resolvedRoot"
if ($History) {
    if (-not (Test-Path -LiteralPath (Join-Path $resolvedRoot '.git'))) { throw 'The history audit requires a full Git checkout.' }
    $shallow = & git -C $resolvedRoot rev-parse --is-shallow-repository
    if ($LASTEXITCODE -ne 0 -or $shallow -ne 'false') { throw 'The history audit requires complete Git history, not a shallow checkout.' }
    $commits = @(& git --no-replace-objects -C $resolvedRoot rev-list --all --reflog)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate historical commits.' }
    $references = @(& git -C $resolvedRoot for-each-ref '--format=%(objectname)')
    if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate Git references.' }
    $revisions = @($commits + $references | Sort-Object -Unique)
    $historicalPaths = foreach ($revision in $revisions) {
        Read-GitPaths -GitArguments @('ls-tree', '-r', '-z', '--name-only', '--full-tree', $revision)
    }
    $forbiddenHistory = @($historicalPaths | Where-Object { Test-ForbiddenAssetPath $_ } | Sort-Object -Unique)
    if ($forbiddenHistory) { throw "Generated or game assets are not allowed in Git history:`n$($forbiddenHistory -join "`n")" }
    Write-Host "Git history asset check passed: $($commits.Count) commits and $($references.Count) refs."
}
