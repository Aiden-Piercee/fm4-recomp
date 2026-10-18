[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameDataRoot,
    [string]$Disc2ContentRoot,
    # Experimental: wait for resolve readback once per guest-visible sync point
    # instead of after every resolve (sdk-patches/0001).
    [switch]$DeferredReadbackSync,
    # Extra runtime settings for this run only, e.g. "--resolution_scale=2".
    # They are written into the deployed fm4.toml because the runtime applies the
    # config file after the command line, so a command-line value alone would be
    # overridden by any key the validated preset already sets.
    [string[]]$RuntimeArgs
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

Invoke-CheckedNative -FilePath $executable -ArgumentList $arguments -WorkingDirectory $buildRoot
