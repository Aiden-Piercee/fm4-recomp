<#
Quick incremental FM4 build (Forza-4-Evolution).

Use this when only hook CODE changed (fm4/src/*.cpp / *.h, fm4_runtime.toml).
It keeps the existing .local/work build tree, copies in the changed files and
lets Ninja recompile just those files and relink. No code generation, no SDK
rebuild, no wipe of the generated game code.

It REFUSES (and tells you to run the full "Build FM4.bat") when anything that
code generation depends on has changed since the last full build:
  - any fm4/*.toml except fm4_runtime.toml (hooks, functions, manifest, facades)
  - sdk-patches/*.patch
  - a missing or broken previous build
CMakeLists.txt / CMakePresets.json changes (e.g. a new src file) are allowed:
the project is re-configured, which does not regenerate the game code. A new
source file must also be listed in release/source-files.txt, like for the full build.

Usage: pwsh -File tools/Build-Quick.ps1 [-Jobs N] [-DryRun]
#>
[CmdletBinding()]
param(
    [ValidateRange(0, 64)][int]$Jobs = 0,
    # Only report what would be copied / rebuilt.
    [switch]$DryRun
)

. (Join-Path $PSScriptRoot "Release.Common.ps1")

if (-not $IsWindows) { throw "Build-Quick.ps1 runs on Windows only (same as Build-Local.ps1)." }

$root = Get-Fm4RepositoryRoot
$policy = Get-Fm4ReleasePolicy
$localRoot = Join-Path $root ".local"
$workRoot = Join-Path $localRoot "work"
$fm4Work = Join-Path $workRoot "fm4"
$buildRoot = Join-Path $fm4Work "out/build/win-amd64-release"
$sdkInstall = Join-Path $localRoot "sdk-install"

function Stop-Quick([string]$why) {
    Write-Host ""
    Write-Host "QUICK BUILD NOT POSSIBLE: $why" -ForegroundColor Yellow
    Write-Host "Run the full build instead: Build FM4.bat" -ForegroundColor Yellow
    exit 2
}

# ---- 1. a previous full build must exist ----
foreach ($p in @((Join-Path $buildRoot "fm4.exe"), (Join-Path $buildRoot "build.ninja"),
                 (Join-Path $fm4Work "generated/default/fm4_init.cpp"), (Join-Path $sdkInstall "bin/rexglue.exe"))) {
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { Stop-Quick "no previous full build (missing $p)" }
}

function Get-Hash([string]$path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return "" }
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}

# ---- 2. files that code generation depends on must be unchanged ----
$sourceAllowlist = @(Get-Content -LiteralPath (Join-Path $root $policy.source_allowlist) |
    ForEach-Object { $_.Trim().Replace("\", "/") } | Where-Object { $_ -and -not $_.StartsWith("#") })

$codegenInputs = @($sourceAllowlist | Where-Object { $_ -match '^fm4/[^/]+\.toml$' -and $_ -ne 'fm4/fm4_runtime.toml' })
$changedCodegen = @()
foreach ($rel in $codegenInputs) {
    if ((Get-Hash (Join-Path $root $rel)) -ne (Get-Hash (Join-Path $workRoot $rel))) { $changedCodegen += $rel }
}
if ($changedCodegen.Count -ne 0) {
    Stop-Quick "code generation inputs changed: $($changedCodegen -join ', ') (new or changed hooks need code generation)"
}

# SDK patches: the full build keeps LF-normalized copies in .local/sdk-patches
$patchSrc = @(if (Test-Path -LiteralPath (Join-Path $root "sdk-patches")) {
    Get-ChildItem -LiteralPath (Join-Path $root "sdk-patches") -Filter "*.patch" -File })
$patchDone = Join-Path $localRoot "sdk-patches"
$donePatches = @(if (Test-Path -LiteralPath $patchDone) { Get-ChildItem -LiteralPath $patchDone -Filter "*.patch" -File })
if ($patchSrc.Count -ne $donePatches.Count) { Stop-Quick "sdk-patches changed (count $($patchSrc.Count) vs $($donePatches.Count) built)" }
foreach ($p in $patchSrc) {
    $built = Join-Path $patchDone $p.Name
    if (-not (Test-Path -LiteralPath $built)) { Stop-Quick "sdk-patches changed ($($p.Name) is new)" }
    $a = [IO.File]::ReadAllText($p.FullName).Replace("`r`n", "`n")
    $b = [IO.File]::ReadAllText($built)
    if ($a -ne $b) { Stop-Quick "sdk-patches changed ($($p.Name))" }
}

# ---- 3. copy changed project files into the build tree ----
$copy = @()
$reconfigure = $false
foreach ($rel in $sourceAllowlist) {
    if ($rel -notmatch '^fm4/') { continue }                  # docs, scripts etc. do not affect the exe
    if ($codegenInputs -contains $rel) { continue }           # checked above, identical
    $src = Join-Path $root $rel
    $dst = Join-Path $workRoot $rel
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { Stop-Quick "listed file is missing: $rel" }
    if ((Get-Hash $src) -ne (Get-Hash $dst)) {
        $copy += $rel
        if ($rel -eq 'fm4/CMakeLists.txt' -or $rel -eq 'fm4/CMakePresets.json') { $reconfigure = $true }
    }
}

Write-Host "Quick build: $($copy.Count) changed file(s)$(if ($reconfigure) { ', CMake re-configure needed' })"
foreach ($rel in $copy) { Write-Host "  $rel" }
if ($copy.Count -eq 0) { Write-Host "Nothing changed since the last build; relinking check only." }
if ($DryRun) { Write-Host "Dry run: nothing copied or built."; exit 0 }

foreach ($rel in $copy) {
    $dst = Join-Path $workRoot $rel
    [IO.Directory]::CreateDirectory((Split-Path -Parent $dst)) | Out-Null
    Copy-Item -LiteralPath (Join-Path $root $rel) -Destination $dst -Force
}

# ---- 4. build ----
$cmake = Resolve-Fm4Tool -Name "cmake"
$clang = Resolve-Fm4Tool -Name "clang"
$clangxx = Resolve-Fm4Tool -Name "clang++"
if ($Jobs -eq 0) { $Jobs = [int][math]::Max(1, [math]::Min(32, [Environment]::ProcessorCount)) }

if ($reconfigure) {
    Invoke-CheckedNative -FilePath $cmake -ArgumentList @(
        "--preset", "win-amd64-release",
        "-DCMAKE_PREFIX_PATH=$sdkInstall",
        "-DCMAKE_C_COMPILER=$clang",
        "-DCMAKE_CXX_COMPILER=$clangxx"
    ) -WorkingDirectory $fm4Work
}
$sw = [Diagnostics.Stopwatch]::StartNew()
Invoke-CheckedNative -FilePath $cmake -ArgumentList @(
    "--build", "--preset", "win-amd64-release", "--parallel", $Jobs
) -WorkingDirectory $fm4Work
$sw.Stop()

Copy-Item -LiteralPath (Join-Path $fm4Work "fm4_runtime.toml") -Destination (Join-Path $buildRoot "fm4.toml") -Force

# ---- 5. note the quick build in the provenance receipt ----
$receiptPath = Join-Path $buildRoot "build-provenance.json"
try {
    $receipt = if (Test-Path -LiteralPath $receiptPath) { Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json -AsHashtable } else { [ordered]@{} }
    $quick = @(if ($receipt.Contains("quick_builds")) { $receipt["quick_builds"] })
    $quick += [ordered]@{ created_utc = [DateTime]::UtcNow.ToString("o"); files = $copy }
    $receipt["quick_builds"] = $quick
    $exe = Join-Path $buildRoot "fm4.exe"
    $receipt["quick_build_fm4_exe_sha256"] = Get-Hash $exe
    $receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding utf8NoBOM
} catch {
    Write-Host "Note: could not update build-provenance.json ($($_.Exception.Message))"
}

Write-Host ("Quick build complete in {0:n0} s: {1}" -f $sw.Elapsed.TotalSeconds, $buildRoot)
