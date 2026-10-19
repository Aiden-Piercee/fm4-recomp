<#
.SYNOPSIS
  Runs allowlisted FM4 build and test jobs that are dropped into
  .local/automation/queue as JSON files, and writes results to
  .local/automation/results/<id>/.

.DESCRIPTION
  Start it once in a PowerShell 7 window and leave it running:

    pwsh -File .\tools\Automation-Agent.ps1 -GameDataRoot "D:\Games\FM4-extracted"

  Only these actions exist; nothing else in a job file is executed:
    build-sdk   tools/Build-Local.ps1 -SdkOnly
    build-full  tools/Build-Local.ps1
    run         tools/Run-Local.ps1 for a fixed time, with optional runtime
                cvars, timed screenshots of the game window and timed
                key/mouse presses using the game's keyboard bindings.

  Job file example (.local/automation/queue/0001-menu.json):
    {
      "id": "0001-menu",
      "action": "run",
      "deferred_readback_sync": true,
      "runtime_args": ["--render_target_path_d3d12=rov"],
      "duration_seconds": 60,
      "screenshots_at": [20, 40, 58],
      "inputs": [ { "at": 25, "key": "Space", "hold_ms": 120 } ]
    }

  Inputs send real key presses to the focused game window, so do not type
  or click while a run job is active. Press Ctrl+C to stop the agent.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameDataRoot,
    [ValidateRange(1, 60)][int]$PollSeconds = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$GameDataRoot = [IO.Path]::GetFullPath($GameDataRoot)
$automationRoot = Join-Path $root ".local/automation"
$queueRoot = Join-Path $automationRoot "queue"
$resultsRoot = Join-Path $automationRoot "results"
$doneRoot = Join-Path $automationRoot "done"
foreach ($dir in @($queueRoot, $resultsRoot, $doneRoot)) {
    [IO.Directory]::CreateDirectory($dir) | Out-Null
}
$buildRoot = Join-Path $root ".local/work/fm4/out/build/win-amd64-release"
$gameLog = Join-Path $buildRoot "fm4.log"

Add-Type -AssemblyName System.Drawing
if (-not ("Fm4Automation.Native" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Threading;
namespace Fm4Automation {
  public static class Native {
    [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit)] struct InputUnion { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
    [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public InputUnion u; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint count, INPUT[] inputs, int size);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int cmd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hwnd, ref POINT point);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();

    static void Send(INPUT input) {
      SendInput(1, new INPUT[] { input }, Marshal.SizeOf(typeof(INPUT)));
    }
    public static void Key(ushort scan, bool extended, int holdMs) {
      const uint SCANCODE = 0x0008, KEYUP = 0x0002, EXTENDED = 0x0001;
      INPUT down = new INPUT(); down.type = 1;
      down.u.ki.wScan = scan; down.u.ki.dwFlags = SCANCODE | (extended ? EXTENDED : 0u);
      Send(down);
      Thread.Sleep(holdMs);
      INPUT up = down; up.u.ki.dwFlags |= KEYUP;
      Send(up);
    }
    public static void Mouse(bool right, int holdMs) {
      INPUT down = new INPUT(); down.type = 0;
      down.u.mi.dwFlags = right ? 0x0008u : 0x0002u;
      Send(down);
      Thread.Sleep(holdMs);
      INPUT up = new INPUT(); up.type = 0;
      up.u.mi.dwFlags = right ? 0x0010u : 0x0004u;
      Send(up);
    }
  }
}
"@
}
[void][Fm4Automation.Native]::SetProcessDPIAware()

# Scan codes for the keys FM4's keyboard bindings use (see keybind_* cvars).
$scanCodes = @{
    "Escape" = @(0x01, $false); "Tab" = @(0x0F, $false); "Enter" = @(0x1C, $false)
    "Space" = @(0x39, $false); "Shift" = @(0x2A, $false); "Backspace" = @(0x0E, $false)
    "Up" = @(0x48, $true); "Down" = @(0x50, $true); "Left" = @(0x4B, $true); "Right" = @(0x4D, $true)
    "Q" = @(0x10, $false); "W" = @(0x11, $false); "E" = @(0x12, $false); "R" = @(0x13, $false)
    "A" = @(0x1E, $false); "S" = @(0x1F, $false); "D" = @(0x20, $false); "F" = @(0x21, $false)
    "C" = @(0x2E, $false)
}

function Write-Json([string]$Path, $Value) {
    $temp = "$Path.tmp"
    $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temp -Encoding utf8NoBOM
    Move-Item -LiteralPath $temp -Destination $Path -Force
}

function Get-GameProcess {
    return Get-Process -Name "fm4" -ErrorAction SilentlyContinue | Select-Object -First 1
}

function Save-GameScreenshot([string]$Path) {
    $process = Get-GameProcess
    if (-not $process) { return "game not running" }
    $process.Refresh()
    $hwnd = $process.MainWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { return "no game window" }
    if ([Fm4Automation.Native]::IsIconic($hwnd)) { [void][Fm4Automation.Native]::ShowWindow($hwnd, 9) }
    [void][Fm4Automation.Native]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 250
    $rect = New-Object Fm4Automation.Native+RECT
    [void][Fm4Automation.Native]::GetClientRect($hwnd, [ref]$rect)
    $origin = New-Object Fm4Automation.Native+POINT
    [void][Fm4Automation.Native]::ClientToScreen($hwnd, [ref]$origin)
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) { return "empty client area" }
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try { $graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, $bitmap.Size) } finally { $graphics.Dispose() }
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally { $bitmap.Dispose() }
    return "ok"
}

function Send-GameInput($GameInput) {
    $process = Get-GameProcess
    if (-not $process) { return "game not running" }
    $process.Refresh()
    if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
        [void][Fm4Automation.Native]::SetForegroundWindow($process.MainWindowHandle)
        Start-Sleep -Milliseconds 100
    }
    $hold = if ($GameInput.PSObject.Properties["hold_ms"]) { [int]$GameInput.hold_ms } else { 120 }
    $hold = [math]::Max(10, [math]::Min(30000, $hold))
    $key = [string]$GameInput.key
    if ($key -eq "LMB" -or $key -eq "RMB") {
        [Fm4Automation.Native]::Mouse($key -eq "RMB", $hold)
        return "ok"
    }
    if (-not $scanCodes.ContainsKey($key)) { return "unknown key $key" }
    $code = $scanCodes[$key]
    [Fm4Automation.Native]::Key([uint16]$code[0], [bool]$code[1], $hold)
    return "ok"
}

function Invoke-Script([string]$ResultDir, [string]$ScriptName, [string[]]$ScriptArgs, [switch]$NoWait) {
    $argumentList = @("-NoProfile", "-File", (Join-Path $root "tools/$ScriptName")) + $ScriptArgs
    $quoted = @($argumentList | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' })
    $parameters = @{
        FilePath = "pwsh"
        ArgumentList = $quoted
        WorkingDirectory = $root
        RedirectStandardOutput = (Join-Path $ResultDir "stdout.txt")
        RedirectStandardError = (Join-Path $ResultDir "stderr.txt")
        NoNewWindow = $true
        PassThru = $true
    }
    if (-not $NoWait) { $parameters.Wait = $true }
    return Start-Process @parameters
}

function Invoke-RunJob($Job, [string]$ResultDir, [hashtable]$Result) {
    if (Get-GameProcess) { throw "fm4.exe is already running; close it first." }
    $duration = if ($Job.PSObject.Properties["duration_seconds"]) { [int]$Job.duration_seconds } else { 60 }
    $duration = [math]::Max(5, [math]::Min(1800, $duration))
    $runtimeArgs = @()
    if ($Job.PSObject.Properties["runtime_args"]) {
        foreach ($arg in @($Job.runtime_args)) {
            if ([string]$arg -notmatch '^--(no-)?[A-Za-z0-9_]+(=[^,"]*)?$') { throw "Rejected runtime argument: $arg" }
            $runtimeArgs += [string]$arg
        }
    }
    $scriptArgs = @("-GameDataRoot", $GameDataRoot)
    if ($Job.PSObject.Properties["deferred_readback_sync"] -and [bool]$Job.deferred_readback_sync) { $scriptArgs += "-DeferredReadbackSync" }
    if ($runtimeArgs.Count -ne 0) { $scriptArgs += @("-RuntimeArgs", ($runtimeArgs -join ",")) }

    $logStartLength = if (Test-Path -LiteralPath $gameLog) { (Get-Item -LiteralPath $gameLog).Length } else { 0 }
    $timeline = New-Object System.Collections.Generic.List[object]
    foreach ($shot in @(if ($Job.PSObject.Properties["screenshots_at"]) { $Job.screenshots_at })) {
        $timeline.Add([pscustomobject]@{ at = [double]$shot; kind = "shot"; data = $null })
    }
    foreach ($inputItem in @(if ($Job.PSObject.Properties["inputs"]) { $Job.inputs })) {
        $timeline.Add([pscustomobject]@{ at = [double]$inputItem.at; kind = "input"; data = $inputItem })
    }
    $ordered = @($timeline | Sort-Object at)

    $launcher = Invoke-Script -ResultDir $ResultDir -ScriptName "Run-Local.ps1" -ScriptArgs $scriptArgs -NoWait
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $eventLog = New-Object System.Collections.Generic.List[string]
    $index = 0
    $exitedEarly = $false
    $sawGame = $false
    while ($clock.Elapsed.TotalSeconds -lt $duration) {
        $game = Get-GameProcess
        if ($game) { $sawGame = $true }
        elseif ($sawGame -or ($launcher.HasExited -and $clock.Elapsed.TotalSeconds -gt 15)) {
            $exitedEarly = $true
            $eventLog.Add(("{0,7:N1}s game exited" -f $clock.Elapsed.TotalSeconds))
            break
        }
        while ($index -lt $ordered.Count -and $ordered[$index].at -le $clock.Elapsed.TotalSeconds) {
            $evt = $ordered[$index]
            if ($evt.kind -eq "shot") {
                $name = "shot-{0:D4}s.png" -f [int]$evt.at
                $status = Save-GameScreenshot (Join-Path $ResultDir $name)
                $eventLog.Add(("{0,7:N1}s screenshot {1}: {2}" -f $clock.Elapsed.TotalSeconds, $name, $status))
            }
            else {
                $status = Send-GameInput $evt.data
                $eventLog.Add(("{0,7:N1}s input {1}: {2}" -f $clock.Elapsed.TotalSeconds, $evt.data.key, $status))
            }
            $index++
        }
        Start-Sleep -Milliseconds 200
    }

    $game = Get-GameProcess
    if ($game) {
        [void]$game.CloseMainWindow()
        if (-not $game.WaitForExit(10000)) { Stop-Process -Id $game.Id -Force }
    }
    if (-not $launcher.HasExited) { [void]$launcher.WaitForExit(10000) }

    if (Test-Path -LiteralPath $gameLog) {
        $stream = [IO.File]::Open($gameLog, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        try {
            $start = if ($stream.Length -ge $logStartLength) { $logStartLength } else { 0 }
            [void]$stream.Seek($start, [IO.SeekOrigin]::Begin)
            $output = [IO.File]::Create((Join-Path $ResultDir "fm4.log"))
            try { $stream.CopyTo($output) } finally { $output.Dispose() }
        }
        finally { $stream.Dispose() }
    }
    $eventLog | Set-Content -LiteralPath (Join-Path $ResultDir "events.txt") -Encoding utf8NoBOM
    $Result.exited_early = $exitedEarly
    $Result.ran_seconds = [math]::Round($clock.Elapsed.TotalSeconds, 1)
    $Result.game_started = $sawGame
}

Write-Host "FM4 automation agent watching $queueRoot (Ctrl+C to stop)."
while ($true) {
    Write-Json (Join-Path $automationRoot "agent-status.json") ([ordered]@{
        alive_utc = [DateTime]::UtcNow.ToString("o"); pid = $PID; game_data_root = $GameDataRoot
    })
    $jobFile = Get-ChildItem -LiteralPath $queueRoot -Filter "*.json" -File | Sort-Object Name | Select-Object -First 1
    if (-not $jobFile) { Start-Sleep -Seconds $PollSeconds; continue }

    $id = [IO.Path]::GetFileNameWithoutExtension($jobFile.Name) -replace '[^A-Za-z0-9_.-]', '_'
    $resultDir = Join-Path $resultsRoot $id
    [IO.Directory]::CreateDirectory($resultDir) | Out-Null
    $result = [ordered]@{ id = $id; state = "running"; started_utc = [DateTime]::UtcNow.ToString("o") }
    Write-Json (Join-Path $resultDir "status.json") $result
    Write-Host "[$((Get-Date).ToString('HH:mm:ss'))] job $id"
    try {
        $job = Get-Content -LiteralPath $jobFile.FullName -Raw | ConvertFrom-Json
        $result.action = [string]$job.action
        switch ([string]$job.action) {
            "build-sdk" {
                $p = Invoke-Script -ResultDir $resultDir -ScriptName "Build-Local.ps1" -ScriptArgs @("-GameDataRoot", $GameDataRoot, "-SdkOnly")
                $result.exit_code = $p.ExitCode
            }
            "build-full" {
                $p = Invoke-Script -ResultDir $resultDir -ScriptName "Build-Local.ps1" -ScriptArgs @("-GameDataRoot", $GameDataRoot)
                $result.exit_code = $p.ExitCode
            }
            "run" { Invoke-RunJob -Job $job -ResultDir $resultDir -Result $result }
            default { throw "Unknown action '$($job.action)'" }
        }
        $result.state = "done"
    }
    catch {
        $result.state = "failed"
        $result.error = $_.Exception.Message
    }
    $result.finished_utc = [DateTime]::UtcNow.ToString("o")
    Write-Json (Join-Path $resultDir "status.json") $result
    Move-Item -LiteralPath $jobFile.FullName -Destination (Join-Path $doneRoot $jobFile.Name) -Force
    Write-Host "[$((Get-Date).ToString('HH:mm:ss'))] job $id $($result.state)"
}
