[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SaveDirectory,
    [Parameter(Mandatory)][string]$Destination
)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $SaveDirectory).Path
$destination = Join-Path $Destination (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
if (Test-Path -LiteralPath $destination) { throw 'Backup destination already exists.' }
$files = @(Get-ChildItem -LiteralPath $source -File -Filter '*.sl2')
if ($files.Count -eq 0) { throw 'No Nightreign .sl2 saves found in the supplied directory.' }
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$manifest = foreach ($file in $files) {
    $hash = Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256
    $copy = Join-Path $destination $file.Name
    Copy-Item -LiteralPath $file.FullName -Destination $copy
    if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $hash.Hash) { throw "Backup verification failed for $($file.Name)" }
    [pscustomobject]@{ Name = $file.Name; Length = $file.Length; SHA256 = $hash.Hash }
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
Write-Host "Verified $($files.Count) save backups: $destination"
