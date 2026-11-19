#requires -Version 7.4
[CmdletBinding()]
param([switch] $Asan, [switch] $Probe)

$ErrorActionPreference = 'Stop'
if ($Probe) {
    ./tools/setup/shell.ps1
    if ($Asan) {
        $compiler = (Get-Command cl -CommandType Application).Source
        $runtime = Join-Path (Split-Path -Parent $compiler) 'clang_rt.asan_dynamic-x86_64.dll'
        if (!(Test-Path -LiteralPath $runtime -PathType Leaf)) { throw "AddressSanitizer runtime missing: $runtime" }
        Write-Host "AddressSanitizer runtime: $runtime"
    }
    return
}

function Test-Toolchain {
    # A failed Developer Shell activation can leave environment changes behind.
    $probeArguments = @('-Probe')
    if ($Asan) { $probeArguments += '-Asan' }
    & (Join-Path $PSHOME 'pwsh.exe') -NoProfile -NonInteractive -File $PSCommandPath @probeArguments | Out-Host
    return $LASTEXITCODE -eq 0
}

if (Test-Toolchain) { return }
if ($env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Automatic Visual Studio provisioning requires a GitHub-hosted runner.'
}

$config = Get-Content -LiteralPath .vsconfig -Raw | ConvertFrom-Json
$installerDirectory = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer'
$vswhere = Join-Path $installerDirectory 'vswhere.exe'
$instance = $null
if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
    # 18.6 is the first stable release with MSVC 14.51.
    $instances = & $vswhere -sort -products '*' -version '[18.6,19.0)' -format json
    if ($LASTEXITCODE -ne 0) { throw 'Cannot discover Visual Studio installations for provisioning.' }
    $instances = $instances | ConvertFrom-Json
    if ($instances) {
        . ./tools/setup/environment.ps1
        # When only ASan is missing, modify the instance the source shell selects.
        $instance = $instances | Select-Object -First 1
        try {
            $selectedPath = (Get-NativeToolchain).Installation
            $selected = $instances | Where-Object installationPath -EQ $selectedPath | Select-Object -First 1
            if ($selected) { $instance = $selected }
        } catch { Write-Verbose $_.Exception.Message }
    }
}

$arguments = @('--quiet', '--norestart')
if ($instance) {
    $installer = Join-Path $installerDirectory 'setup.exe'
    $path = $instance.installationPath
    $arguments = @('modify', '--channelId', $instance.channelId) + $arguments
} else {
    $path = Join-Path $env:ProgramFiles 'Microsoft Visual Studio/18/CI-BuildTools'
    $installer = Join-Path $env:RUNNER_TEMP 'vs_BuildTools.exe'
    # Use the serviced 18.11 channel, whose default toolset is MSVC 14.51.
    Invoke-WebRequest -Uri 'https://aka.ms/vs/18/release.ltsc.18.11/vs_BuildTools.exe' -OutFile $installer `
        -ConnectionTimeoutSeconds 30 -OperationTimeoutSeconds 60
    $arguments += '--wait'
}
$arguments += @('--installPath', ('"{0}"' -f $path))
foreach ($component in $config.components) {
    if (!$Asan -and $component -eq 'Microsoft.VisualStudio.Component.VC.ASAN') { continue }
    if ($instance -and $component -like 'Microsoft.VisualStudio.Workload.*') { continue }
    $arguments += @('--add', $component)
}

Write-Host "Provisioning Visual Studio components in $path"
$install = Start-Process -FilePath $installer -ArgumentList $arguments -WorkingDirectory (Get-Location).ProviderPath `
    -Wait -PassThru -WindowStyle Hidden
if ($install.ExitCode -notin @(0, 3010)) {
    throw "Visual Studio Installer failed with exit code $($install.ExitCode)."
}
if ($install.ExitCode -eq 3010) { Write-Warning 'Visual Studio Installer requested a restart. Checking toolchain availability.' }
if (!(Test-Toolchain)) { throw 'The pinned Visual Studio toolchain is unavailable after provisioning.' }
