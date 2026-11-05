# Shared by the local build and verification scripts.
function Resolve-OutputPath([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path))
    $ancestor = $absolute
    while ($ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Generated output cannot use a reparse point: $ancestor"
        }
        $ancestor = Split-Path -Parent $ancestor
    }
    return $absolute
}

function New-TemporaryDirectory {
    $path = Resolve-OutputPath (Join-Path ([IO.Path]::GetTempPath()) ('ern-relics-' + [guid]::NewGuid().ToString('N')))
    New-Item -ItemType Directory -Path $path | Out-Null
    return $path
}

function Remove-GeneratedDirectory([string]$Root, [string]$Path) {
    $parent = (Resolve-OutputPath $Root).TrimEnd('\')
    $absolute = (Resolve-OutputPath $Path).TrimEnd('\')
    if ((Split-Path -Parent $absolute) -ne $parent) {
        throw "Refusing to remove a directory outside generated output: $absolute"
    }
    if (-not (Test-Path -LiteralPath $absolute)) { return }
    if (Get-ChildItem -LiteralPath $absolute -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
        throw "Refusing to remove a generated tree containing reparse points: $absolute"
    }
    Remove-Item -LiteralPath $absolute -Recurse -Force
}

function Remove-TemporaryDirectory([string]$Path) {
    if ((Split-Path -Leaf $Path) -notmatch '^ern-relics-[0-9a-f]{32}$') {
        throw "Refusing to remove an unrecognized temporary directory: $Path"
    }
    Remove-GeneratedDirectory -Root ([IO.Path]::GetTempPath()) -Path $Path
}

function Write-BuildOutput {
    param(
        [string]$ProjectRoot,
        [string]$BuildDirectory,
        [Parameter(Mandatory)][string]$Name,
        [scriptblock]$ValidatePackage
    )
    $project = Resolve-OutputPath $ProjectRoot
    $dist = Resolve-OutputPath (Join-Path $project 'dist')
    $stage = Resolve-OutputPath (Join-Path $dist ('.stage-' + [guid]::NewGuid().ToString('N')))
    $output = Resolve-OutputPath (Join-Path $dist $Name)
    try {
        New-Item -ItemType Directory -Force -Path $stage | Out-Null
        Copy-Item -LiteralPath (Join-Path $BuildDirectory 'ern-relics.dll') -Destination $stage
        foreach ($fileName in @('ern-relics.ini','nightreign-relics.me3')) {
            Copy-Item -LiteralPath (Join-Path $project "config/$fileName") -Destination $stage
        }
        foreach ($fileName in @('LICENSE','ThirdPartyNotices.txt')) {
            Copy-Item -LiteralPath (Join-Path $project $fileName) -Destination $stage
        }
        # Validation throws on failure. Its diagnostics must not become return values.
        if ($ValidatePackage) { & $ValidatePackage $stage | Out-Host }
        if ($Name -match '^ern-relics-\d+\.\d+\.\d+$') {
            if (Test-Path -LiteralPath $output) {
                throw "Release output already exists; it will not be replaced: $output"
            }
            # Directory.Move also refuses a destination created after the check.
            [IO.Directory]::Move($stage, $output)
        } else {
            # Replace only this development package after validation succeeds.
            Remove-GeneratedDirectory -Root $dist -Path $output
            Move-Item -LiteralPath $stage -Destination $output
        }
        return $output
    } finally {
        Remove-GeneratedDirectory -Root $dist -Path $stage
    }
}
