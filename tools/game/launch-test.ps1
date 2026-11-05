[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [string]$Me3Exe = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs/garyttierney/me3/bin/me3.exe'),
    [string]$GameExe = (Join-Path ${env:ProgramFiles(x86)} 'Steam/steamapps/common/ELDEN RING NIGHTREIGN/Game/nightreign.exe'),
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).ProviderPath
foreach ($path in @($Me3Exe, $GameExe, (Join-Path $package 'ern-relics.dll'))) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required file not found: $path" }
}
$Me3Exe = (Resolve-Path -LiteralPath $Me3Exe).ProviderPath
$GameExe = (Resolve-Path -LiteralPath $GameExe).ProviderPath
if (Get-Process -Name nightreign,easyanticheat_eos,easyanticheat -ErrorAction SilentlyContinue) {
    throw 'Nightreign or EAC is already running. Close it before starting the test profile.'
}
$native = Join-Path $package 'ern-relics.dll'
$profile = Join-Path $package 'nightreign-relics-test.me3'
$nativeToml = ConvertTo-Json -InputObject $native.Replace('\', '/') -Compress
$profileContent = @"
profileVersion = "v1"
savefile = "ern-relics-test.sl2"
start_online = false

[[supports]]
game = "nightreign"

[[natives]]
path = $nativeToml
load_early = true
"@
[IO.File]::WriteAllText($profile, $profileContent, [Text.UTF8Encoding]::new($false))
if ($Check) {
    Write-Host "Test profile paths verified: $profile. No game was launched."
    return
}
$logName = 'launch-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
$arguments = @('launch', '--profile', ('"{0}"' -f $profile), '--exe', ('"{0}"' -f $GameExe))
$process = Start-Process -FilePath $Me3Exe -ArgumentList $arguments -WorkingDirectory $package -WindowStyle Hidden -RedirectStandardOutput (Join-Path $package "$logName.log") -RedirectStandardError (Join-Path $package "$logName.err.log") -PassThru
Write-Host "Started offline test profile with ern-relics-test.sl2. ME3 process: $($process.Id)"
