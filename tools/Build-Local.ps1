[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameDataRoot,
    # 0 = automatic: one job per logical processor, capped by free RAM.
    [ValidateRange(0, 64)][int]$Jobs = 0,
    [string]$SdkSourceRoot,
    [switch]$ValidateOnly,
    # Rebuild only the (patched) SDK and refresh the GPU plugin in the existing
    # FM4 build. Skips code generation and the full FM4 rebuild.
    [switch]$SdkOnly
)

. (Join-Path $PSScriptRoot "Release.Common.ps1")

if (-not $IsWindows -or [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne "X64") {
    throw "The validated local builder currently supports Windows AMD64 only."
}

$root = Get-Fm4RepositoryRoot
$policy = Get-Fm4ReleasePolicy
& (Join-Path $PSScriptRoot "Test-RepositoryBoundary.ps1")
$resolvedGameRoot = Assert-Fm4GameInputs -GameDataRoot $GameDataRoot
$git = Resolve-Fm4Tool -Name "git"
$cmake = Resolve-Fm4Tool -Name "cmake"
$clang = Resolve-Fm4Tool -Name "clang"
$clangxx = Resolve-Fm4Tool -Name "clang++"
$ninja = Resolve-Fm4Tool -Name "ninja"
$compilerTarget = (& $clangxx -dumpmachine).Trim()
if ($LASTEXITCODE -ne 0 -or $compilerTarget -notmatch "windows-msvc$") {
    throw "Clang must target the MSVC ABI; detected target: $compilerTarget"
}

if ($ValidateOnly) {
    Write-Host "Input and toolchain validation passed. No SDK source was downloaded and no code was generated."
    return
}

if ($Jobs -eq 0) {
    $logicalProcessors = [Environment]::ProcessorCount
    $freeRamGb = 0
    try {
        # FreePhysicalMemory is reported in KiB.
        $freeRamGb = [math]::Floor((Get-CimInstance -ClassName Win32_OperatingSystem).FreePhysicalMemory / 1MB)
    }
    catch {
        $freeRamGb = 0
    }
    # Recompiled translation units are large; budget about 2 GB of RAM per
    # compiler process so a 32-thread build does not exhaust memory.
    # Set FM4_GB_PER_JOB (e.g. 1.5) to tune it, or pass -Jobs to force a count.
    $gbPerJob = 2.0
    if ($env:FM4_GB_PER_JOB) { $gbPerJob = [math]::Max(0.5, [double]$env:FM4_GB_PER_JOB) }
    $jobsByMemory = if ($freeRamGb -gt 0) { [math]::Max(1, [math]::Floor($freeRamGb / $gbPerJob)) } else { 2 }
    $Jobs = [int][math]::Max(1, [math]::Min(64, [math]::Min($logicalProcessors, $jobsByMemory)))
    Write-Host "Using $Jobs parallel build jobs ($logicalProcessors logical processors, $freeRamGb GB free RAM). Override with -Jobs."
}

$localRoot = Join-Path $root ".local"
$sdkSource = if ($SdkSourceRoot) { [IO.Path]::GetFullPath($SdkSourceRoot) } else { Join-Path $localRoot "rexglue-sdk" }
[IO.Directory]::CreateDirectory($localRoot) | Out-Null

if (-not (Test-Path -LiteralPath (Join-Path $sdkSource ".git"))) {
    if ($SdkSourceRoot) {
        throw "The supplied SDK source is not a Git checkout: $sdkSource"
    }
    if (Test-Path -LiteralPath $sdkSource) {
        throw "SDK source path exists but is not a Git checkout: $sdkSource"
    }
    Invoke-CheckedNative -FilePath $git -ArgumentList @("clone", "--no-checkout", $policy.sdk.repository, $sdkSource) -WorkingDirectory $root
    Invoke-CheckedNative -FilePath $git -ArgumentList @("checkout", "--detach", $policy.sdk.commit) -WorkingDirectory $sdkSource
    Invoke-CheckedNative -FilePath $git -ArgumentList @("submodule", "update", "--init", "--recursive") -WorkingDirectory $sdkSource
}

$sdkHead = "$(& $git -C $sdkSource rev-parse HEAD)".Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Git could not read the SDK checkout at $sdkSource. If Git reported dubious ownership, run: git config --global --add safe.directory `"$($sdkSource.Replace('\', '/'))`""
}
if ($LASTEXITCODE -ne 0 -or $sdkHead -ne $policy.sdk.commit) {
    throw "SDK checkout is not at the required commit $($policy.sdk.commit): $sdkHead"
}

# Project-owned SDK patches (sdk-patches/*.patch) are applied in name order on
# top of the pinned commit. They are normalized to LF under .local so Git
# line-ending settings cannot break them. Any other SDK modification is refused.
$sdkPatchSourceRoot = Join-Path $root "sdk-patches"
$sdkPatchSources = @(if (Test-Path -LiteralPath $sdkPatchSourceRoot -PathType Container) {
    Get-ChildItem -LiteralPath $sdkPatchSourceRoot -Filter "*.patch" -File | Sort-Object Name
})
$sdkPatchWorkRoot = Join-Path $localRoot "sdk-patches"
if (Test-Path -LiteralPath $sdkPatchWorkRoot) {
    Remove-Item -LiteralPath $sdkPatchWorkRoot -Recurse -Force
}
[IO.Directory]::CreateDirectory($sdkPatchWorkRoot) | Out-Null
$sdkPatches = @($sdkPatchSources | ForEach-Object {
    $normalizedPatch = Join-Path $sdkPatchWorkRoot $_.Name
    $patchText = [IO.File]::ReadAllText($_.FullName).Replace("`r`n", "`n")
    [IO.File]::WriteAllText($normalizedPatch, $patchText, [Text.UTF8Encoding]::new($false))
    $normalizedPatch
})

# The exact patch files the last build applied, kept so the next build can
# reverse what is really in the checkout even after a branch switch, an
# edited patch or new patches stacked on the series.
$sdkPatchAppliedRoot = Join-Path $localRoot "sdk-patches-applied"
$sdkDirty = @(& $git -C $sdkSource status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0) {
    throw "git status failed for the SDK checkout."
}
if ($sdkDirty.Count -ne 0 -and (Test-Path -LiteralPath $sdkPatchAppliedRoot -PathType Container)) {
    $appliedPatches = @(Get-ChildItem -LiteralPath $sdkPatchAppliedRoot -Filter "*.patch" -File |
        Sort-Object Name -Descending)
    foreach ($patch in $appliedPatches) {
        & $git -C $sdkSource apply --reverse --check $patch.FullName 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw "SDK checkout does not match the patches the last build applied ($($patch.Name) in $sdkPatchAppliedRoot does not reverse). Refusing a non-pinned toolchain build."
        }
        Invoke-CheckedNative -FilePath $git -ArgumentList @("apply", "--reverse", $patch.FullName) -WorkingDirectory $sdkSource
        Write-Host "Reversed SDK patch the last build applied: $($patch.Name)"
    }
    $sdkDirty = @(& $git -C $sdkSource status --porcelain --untracked-files=no)
    if ($LASTEXITCODE -ne 0 -or $sdkDirty.Count -ne 0) {
        throw "SDK checkout has tracked modifications beyond the patches the last build applied. Refusing a non-pinned toolchain build."
    }
}
if (Test-Path -LiteralPath $sdkPatchAppliedRoot) {
    Remove-Item -LiteralPath $sdkPatchAppliedRoot -Recurse -Force
}
if ($sdkDirty.Count -ne 0) {
    # No record of the last build's patches (built before the record existed):
    # reverse the current series, skipping patches that were not applied.
    if ($sdkPatches.Count -eq 0) {
        throw "SDK checkout has tracked modifications. Refusing a non-pinned toolchain build."
    }
    # A previous build left the patches applied. Reverse them one at a time
    # (later patches may touch lines next to earlier ones) and require a clean
    # pinned tree afterwards, so the series below is always applied fresh.
    $reversePatches = @($sdkPatches)
    [array]::Reverse($reversePatches)
    $reversed = New-Object System.Collections.Generic.List[string]
    foreach ($patch in $reversePatches) {
        & $git -C $sdkSource apply --reverse --check $patch 2>$null
        if ($LASTEXITCODE -ne 0) {
            # A patch the previous build did not apply (another branch's series,
            # or one added since) still applies forward; skip it. The clean-tree
            # check below still refuses anything else.
            & $git -C $sdkSource apply --check $patch 2>$null
            if ($LASTEXITCODE -eq 0) {
                continue
            }
            for ($i = $reversed.Count - 1; $i -ge 0; $i--) {
                & $git -C $sdkSource apply $reversed[$i] 2>$null
            }
            throw "SDK checkout has tracked modifications that are not the project's sdk-patches ($([IO.Path]::GetFileName($patch)) does not reverse). Refusing a non-pinned toolchain build."
        }
        Invoke-CheckedNative -FilePath $git -ArgumentList @("apply", "--reverse", $patch) -WorkingDirectory $sdkSource
        $reversed.Add($patch)
    }
    $sdkDirty = @(& $git -C $sdkSource status --porcelain --untracked-files=no)
    if ($LASTEXITCODE -ne 0 -or $sdkDirty.Count -ne 0) {
        throw "SDK checkout has tracked modifications beyond the project's sdk-patches. Refusing a non-pinned toolchain build."
    }
}
$submoduleStatus = @(& $git -C $sdkSource submodule status --recursive)
if ($LASTEXITCODE -ne 0 -or @($submoduleStatus | Where-Object { $_ -match "^[-+]" }).Count -ne 0) {
    throw "SDK submodules are missing or do not match the pinned SDK commit."
}
[IO.Directory]::CreateDirectory($sdkPatchAppliedRoot) | Out-Null
foreach ($patch in $sdkPatches) {
    Invoke-CheckedNative -FilePath $git -ArgumentList @("apply", "--whitespace=nowarn", $patch) -WorkingDirectory $sdkSource
    Copy-Item -LiteralPath $patch -Destination (Join-Path $sdkPatchAppliedRoot ([IO.Path]::GetFileName($patch)))
    Write-Host "Applied SDK patch: $([IO.Path]::GetFileName($patch))"
}
# The native executor's shaders (sdk-patches/0002) are committed as HLSL only;
# compile them into the DXBC headers the SDK includes.
& (Join-Path $PSScriptRoot "Build-NativeShaders.ps1") -SdkSourceRoot $sdkSource

$sdkBuild = Join-Path $localRoot "sdk-build"
$sdkInstall = Join-Path $localRoot "sdk-install"
Invoke-CheckedNative -FilePath $cmake -ArgumentList @(
    "-S", $sdkSource,
    "-B", $sdkBuild,
    "-G", "Ninja Multi-Config",
    "-DCMAKE_C_COMPILER=$clang",
    "-DCMAKE_CXX_COMPILER=$clangxx",
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_C_FLAGS=-march=x86-64-v3",
    "-DCMAKE_CXX_FLAGS=-march=x86-64-v3",
    "-DCMAKE_CXX_STANDARD=23",
    "-DCMAKE_CONFIGURATION_TYPES=Release",
    "-DCMAKE_INSTALL_PREFIX=$sdkInstall"
) -WorkingDirectory $sdkSource
Invoke-CheckedNative -FilePath $cmake -ArgumentList @(
    "--build", $sdkBuild, "--config", "Release", "--target", "install", "--parallel", $Jobs
) -WorkingDirectory $sdkSource

$rexglue = Join-Path $sdkInstall "bin/rexglue.exe"
if (-not (Test-Path -LiteralPath $rexglue -PathType Leaf)) {
    throw "Pinned SDK build did not install rexglue.exe at $rexglue"
}

if ($SdkOnly) {
    # The GPU plugin is loaded at runtime and its command processor is private
    # to it, so it can be replaced without regenerating or relinking FM4.
    $existingBuildRoot = Join-Path $localRoot "work/fm4/out/build/win-amd64-release"
    if (-not (Test-Path -LiteralPath (Join-Path $existingBuildRoot "fm4.exe") -PathType Leaf)) {
        throw "No existing FM4 build at $existingBuildRoot. Run Build-Local.ps1 without -SdkOnly first."
    }
    Copy-Item -LiteralPath (Join-Path $sdkInstall "bin/rexgpu-xenos.dll") -Destination $existingBuildRoot -Force
    Write-Host "SDK rebuilt; refreshed rexgpu-xenos.dll in $existingBuildRoot"
    return
}

$projectWorkRoot = Join-Path $localRoot "work"
if (Test-Path -LiteralPath $projectWorkRoot) {
    $resolvedWorkRoot = [IO.Path]::GetFullPath($projectWorkRoot)
    if ((Split-Path -Parent $resolvedWorkRoot) -ne [IO.Path]::GetFullPath($localRoot)) {
        throw "Refusing to replace a build workspace outside .local: $resolvedWorkRoot"
    }
    Remove-Item -LiteralPath $resolvedWorkRoot -Recurse -Force
}
[IO.Directory]::CreateDirectory($projectWorkRoot) | Out-Null
$sourceAllowlist = @(Get-Content -LiteralPath (Join-Path $root $policy.source_allowlist) | ForEach-Object { $_.Trim().Replace("\", "/") } | Where-Object { $_ -and -not $_.StartsWith("#") })
foreach ($relativePath in $sourceAllowlist) {
    $source = Join-Path $root $relativePath
    $destination = Join-Path $projectWorkRoot $relativePath
    [IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}

$fm4Root = Join-Path $projectWorkRoot "fm4"
$bootstrapRoot = Join-Path $localRoot "project-bootstrap"
if (Test-Path -LiteralPath $bootstrapRoot) {
    $resolvedBootstrapRoot = [IO.Path]::GetFullPath($bootstrapRoot)
    if ((Split-Path -Parent $resolvedBootstrapRoot) -ne [IO.Path]::GetFullPath($localRoot)) {
        throw "Refusing to replace a bootstrap workspace outside .local: $resolvedBootstrapRoot"
    }
    Remove-Item -LiteralPath $resolvedBootstrapRoot -Recurse -Force
}
Invoke-CheckedNative -FilePath $rexglue -ArgumentList @(
    "init",
    "--project-name", "fm4",
    "--xex-path", (Join-Path $resolvedGameRoot "default.xex"),
    "--game-root", $resolvedGameRoot,
    "--project-root", $bootstrapRoot
) -WorkingDirectory $root
[IO.Directory]::CreateDirectory((Join-Path $fm4Root "generated")) | Out-Null
Copy-Item -LiteralPath (Join-Path $bootstrapRoot "generated/rexglue.cmake") -Destination (Join-Path $fm4Root "generated/rexglue.cmake")
Remove-Item -LiteralPath $bootstrapRoot -Recurse -Force

$manifestTemplate = Join-Path $fm4Root "fm4_manifest.toml"
$localManifest = Join-Path $fm4Root "fm4_manifest.local.toml"
$tomlGameRoot = $resolvedGameRoot.Replace("\", "/").Replace('"', '\"')
$manifest = (Get-Content -LiteralPath $manifestTemplate -Raw).Replace("../extracted", $tomlGameRoot)
Set-Content -LiteralPath $localManifest -Value $manifest -Encoding utf8NoBOM

Invoke-CheckedNative -FilePath $rexglue -ArgumentList @("codegen", $localManifest) -WorkingDirectory $fm4Root
Invoke-CheckedNative -FilePath $cmake -ArgumentList @(
    "--preset", "win-amd64-release",
    "-DCMAKE_PREFIX_PATH=$sdkInstall",
    "-DCMAKE_C_COMPILER=$clang",
    "-DCMAKE_CXX_COMPILER=$clangxx"
) -WorkingDirectory $fm4Root
Invoke-CheckedNative -FilePath $cmake -ArgumentList @(
    "--build", "--preset", "win-amd64-release", "--parallel", $Jobs
) -WorkingDirectory $fm4Root

$buildRoot = Join-Path $fm4Root "out/build/win-amd64-release"
Copy-Item -LiteralPath (Join-Path $fm4Root "fm4_runtime.toml") -Destination (Join-Path $buildRoot "fm4.toml") -Force
$artifacts = @(
    "fm4.exe",
    "fm4_SpeechFacade_default.dll",
    "fm4_XMediaFacade_default.dll",
    "rexgpu-xenos.dll",
    "rexruntime.dll",
    "TracyClient.dll",
    "fm4.toml"
) | ForEach-Object { Join-Path $buildRoot $_ } | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }

if (-not (Test-Path -LiteralPath (Join-Path $buildRoot "fm4.exe") -PathType Leaf)) {
    throw "FM4 build completed without the expected executable."
}

$projectCommit = (& $git -C $root rev-parse HEAD).Trim()
$projectDirty = @(& $git -C $root status --porcelain).Count -ne 0
$receipt = [ordered]@{
    schema_version = 1
    generated_locally = $true
    created_utc = [DateTime]::UtcNow.ToString("o")
    project_commit = $projectCommit
    project_worktree_dirty = $projectDirty
    sdk_repository = $policy.sdk.repository
    sdk_commit = $sdkHead
    sdk_patches = @(if ($sdkPatchSources.Count -ne 0) { Get-Fm4FileRecords -Paths @($sdkPatchSources | ForEach-Object FullName) -BasePath $root })
    game_inputs = @($policy.required_game_inputs)
    artifacts = Get-Fm4FileRecords -Paths $artifacts -BasePath $buildRoot
}
$receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $buildRoot "build-provenance.json") -Encoding utf8NoBOM

Write-Host "Local build complete: $buildRoot"
Write-Host "Run it with: pwsh -File .\tools\Run-Local.ps1 -GameDataRoot `"$resolvedGameRoot`""
