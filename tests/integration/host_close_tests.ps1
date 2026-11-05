[CmdletBinding()]
param([Parameter(Mandatory)][string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $projectRoot 'tools/build/output.ps1')
$closeWindow = @{ Enabled = $true }

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class HostCloseTest {
    private delegate bool EnumWindow(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindow callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetClassName(IntPtr window, StringBuilder name, int count);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    public static bool CloseOnlyProcess(uint processId) {
        bool sent = false;
        EnumWindows((window, parameter) => {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            var name = new StringBuilder(256);
            GetClassName(window, name, name.Capacity);
            if (owner == processId && name.ToString() == "ERNRelicsHost") {
                sent = PostMessage(window, 0x0010, IntPtr.Zero, IntPtr.Zero);
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return sent;
    }
}
'@

# Exercise the real verifier and DLL, closing only the synthetic process it starts.
function Start-Process {
    param($FilePath, $ArgumentList, $WorkingDirectory, $WindowStyle,
          [switch]$PassThru, $RedirectStandardOutput, $RedirectStandardError)
    $process = Microsoft.PowerShell.Management\Start-Process @PSBoundParameters
    try {
        if ($closeWindow.Enabled) {
            $deadline = [DateTime]::UtcNow.AddSeconds(15)
            $sent = $false
            while (-not $process.HasExited -and [DateTime]::UtcNow -lt $deadline) {
                if ([HostCloseTest]::CloseOnlyProcess([uint32]$process.Id)) {
                    $sent = $true
                    break
                }
                Start-Sleep -Milliseconds 1
            }
            if (-not $sent) { throw 'Did not locate the synthetic host window before it exited.' }
        }
        if (-not $process.WaitForExit(10000)) { throw 'The synthetic host did not close.' }
        $process.Refresh()
        $closedHost.Exit = $process.ExitCode
        $closedHost.Report = Get-Content -LiteralPath (Join-Path $WorkingDirectory 'host-test.txt') -Raw
        return $process
    } finally {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
            $process.WaitForExit()
        }
    }
}

$closedHost = @{ Exit = $null; Report = $null }
$rejected = $false
try {
    & (Join-Path $projectRoot 'tools/verify.ps1') -BuildDirectory $BuildDirectory | Out-Null
} catch {
    if ($_.Exception.Message -ne 'DX12 integration test failed: exit 1') { throw }
    $rejected = $true
}
if (-not $rejected -or $closedHost.Exit -ne 1 -or
    $closedHost.Report.Trim() -ne 'FAIL: Self-test closed before completing integration checks') {
    throw "Premature self-test close passed verification: exit=$($closedHost.Exit), report=$($closedHost.Report)"
}
Write-Host 'PASS: an early close of the actual synthetic DX12 host fails verification.'

$package = New-TemporaryDirectory
try {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory 'ern-relics.dll') -Destination $package
    Copy-Item -LiteralPath (Join-Path $projectRoot 'config/ern-relics.ini') -Destination $package
    $closeWindow.Enabled = $false
    $rejected = $false
    try {
        & (Join-Path $projectRoot 'tools/verify.ps1') -BuildDirectory $BuildDirectory -PackageDirectory $package | Out-Null
    } catch {
        if ($_.Exception.Message -ne 'DX12 integration test failed: exit 1') { throw }
        $rejected = $true
    }
    if (-not $rejected -or $closedHost.Exit -ne 1 -or
        $closedHost.Report.Trim() -ne 'FAIL: Packaged DLL contains integration fixture exports') {
        throw "The smoke test accepted integration fixture exports: exit=$($closedHost.Exit), report=$($closedHost.Report)"
    }
    Write-Host 'PASS: package smoke verification rejects an actual DLL with integration fixture exports.'
} finally {
    Remove-TemporaryDirectory $package
}
