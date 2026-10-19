[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameDataRoot,
    [string]$Disc2ContentRoot,
    # Folder holding post-launch FM4 DLC packages (searched recursively, e.g. a
    # folder of "<pack name>\4D530910\00000002\<package>" trees).
    [string]$DlcContentRoot,
    # Experimental: wait for resolve readback once per guest-visible sync point
    # instead of after every resolve (requires sdk-patches/0001).
    [switch]$DeferredReadbackSync,
    # Extra runtime cvars for this run only, e.g. "--render_target_path_d3d12=rtv".
    # They are written into the deployed fm4.toml because the runtime applies the
    # config file after the command line, so a command-line value alone would be
    # overridden by any key the validated preset already sets.
    [string[]]$RuntimeArgs,
    # Debug: log every guest file open/read (kernel category at trace level,
    # noisy macros on, larger log files). Slower; use only to chase a crash.
    [switch]$TraceFiles,
    # Debug: log the model (.carbin) serializer field reads to serial_trace.log
    # next to fm4.exe (needs the TraceSer* hooks from fm4_hooks.toml in the build).
    [switch]$SerialTrace
)

. (Join-Path $PSScriptRoot "Release.Common.ps1")

$root = Get-Fm4RepositoryRoot
$resolvedGameRoot = Assert-Fm4GameInputs -GameDataRoot $GameDataRoot
$buildRoot = Join-Path $root ".local/work/fm4/out/build/win-amd64-release"
$executable = Join-Path $buildRoot "fm4.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Local build not found. Run tools/Build-Local.ps1 first."
}

$deployedConfig = Join-Path $buildRoot "fm4.toml"
Copy-Item -LiteralPath (Join-Path $root ".local/work/fm4/fm4_runtime.toml") -Destination $deployedConfig -Force

$cvarOverrides = [ordered]@{}
if ($DeferredReadbackSync) {
    $cvarOverrides["d3d12_readback_resolve_deferred_sync"] = "true"
}
# pwsh -File passes "a","b" as one string "a,b", so split on commas that start a new --option.
$splitRuntimeArgs = @(@($RuntimeArgs) | Where-Object { $_ } | ForEach-Object { $_ -split ',(?=--)' } | ForEach-Object { $_.Trim() })
foreach ($runtimeArg in $splitRuntimeArgs) {
    if (-not $runtimeArg) { continue }
    if ($runtimeArg -match '^--no-([A-Za-z0-9_]+)$') {
        $cvarOverrides[$Matches[1]] = "false"
    }
    elseif ($runtimeArg -match '^--([A-Za-z0-9_]+)=(.*)$') {
        $cvarOverrides[$Matches[1]] = $Matches[2]
    }
    elseif ($runtimeArg -match '^--([A-Za-z0-9_]+)$') {
        $cvarOverrides[$Matches[1]] = "true"
    }
    else {
        throw "Unsupported runtime argument '$runtimeArg'. Use --name=value, --name or --no-name."
    }
}
if ($TraceFiles) {
    $cvarOverrides["log_noisy"] = "true"
    $cvarOverrides["log_max_file_size_mb"] = "100"
}
if ($cvarOverrides.Count -ne 0) {
    $configLines = @(Get-Content -LiteralPath $deployedConfig)
    $overrideLines = @("# Per-run overrides written by Run-Local.ps1")
    foreach ($name in $cvarOverrides.Keys) {
        $value = [string]$cvarOverrides[$name]
        $configLines = @($configLines | Where-Object { $_ -notmatch "^\s*$([regex]::Escape($name))\s*=" })
        if ($value -notmatch '^(true|false|-?\d+(\.\d+)?)$') {
            $value = '"' + $value.Replace('\', '\\').Replace('"', '\"') + '"'
        }
        $overrideLines += "$name = $value"
        Write-Host "Override for this run: $name = $value"
    }
    # Keys go first so they stay in the root table even if sections are added later.
    Set-Content -LiteralPath $deployedConfig -Value (@($overrideLines) + "" + $configLines) -Encoding utf8NoBOM
}
if ($TraceFiles) {
    # Per-category levels live in a [log.levels] table, which must come after all root keys.
    Add-Content -LiteralPath $deployedConfig -Value @("", "[log.levels]", 'krnl = "trace"') -Encoding utf8NoBOM
    Write-Host "File trace enabled: kernel calls (NtCreateFile/NtReadFile) are logged to fm4.log"
}
if ($SerialTrace) {
    $env:FM4_SERIAL_TRACE = Join-Path $buildRoot "serial_trace.log"
    Write-Host "Serializer trace enabled: $env:FM4_SERIAL_TRACE"
}
$localUserRoot = Join-Path $root ".local/user"
[IO.Directory]::CreateDirectory($localUserRoot) | Out-Null
$arguments = @(
    "--game_data_root=$resolvedGameRoot",
    "--user_data_root=$localUserRoot"
)
if ($Disc2ContentRoot) {
    $resolvedDisc2Root = [IO.Path]::GetFullPath($Disc2ContentRoot)
    if (-not (Test-Path -LiteralPath $resolvedDisc2Root -PathType Container)) {
        throw "Disc 2 content folder not found: $resolvedDisc2Root"
    }
    $arguments += "--fm4_disc2_content_root=$resolvedDisc2Root"
}
if ($DlcContentRoot) {
    $resolvedDlcRoot = [IO.Path]::GetFullPath($DlcContentRoot)
    if (-not (Test-Path -LiteralPath $resolvedDlcRoot -PathType Container)) {
        throw "DLC content folder not found: $resolvedDlcRoot"
    }
    # Passed by environment so the deployed fm4.toml cannot override it.
    $env:FM4_DLC_ROOT = $resolvedDlcRoot
}

# fm4.exe is a Windows GUI program, so PowerShell would neither wait for it
# nor set $LASTEXITCODE when calling it directly. Start it explicitly instead.
$quotedArguments = @($arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' })
$process = Start-Process -FilePath $executable -ArgumentList $quotedArguments -WorkingDirectory $buildRoot -Wait -PassThru
if ($process.ExitCode -ne 0) {
    throw "FM4 exited with code $($process.ExitCode). See $(Join-Path $buildRoot 'fm4.log')"
}
