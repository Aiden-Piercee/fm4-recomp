[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SdkSourceRoot,
    # Rebuild every shader even if the headers are newer than the sources.
    [switch]$Force
)
# Compiles the native executor's HLSL (added by sdk-patches/0002) into the DXBC
# headers the SDK includes, so the repository carries shader sources only.
# Uses fxc.exe from the Windows SDK; set FM4_FXC to use a specific fxc.exe.

$ErrorActionPreference = 'Stop'
$sourceDir = Join-Path $SdkSourceRoot 'src/graphics/shaders/native_executor'
$outDir = Join-Path $SdkSourceRoot 'src/graphics/shaders/bytecode/d3d12_5_1'
if (-not (Test-Path -LiteralPath $sourceDir -PathType Container)) {
    return
}

$fxc = $env:FM4_FXC
if (-not $fxc) {
    $fxc = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\fxc.exe" -ErrorAction SilentlyContinue |
        Sort-Object { [version]($_.Directory.Parent.Name) } |
        Select-Object -Last 1 -ExpandProperty FullName
}
if (-not $fxc -or -not (Test-Path -LiteralPath $fxc -PathType Leaf)) {
    throw "fxc.exe was not found. Install the Windows SDK (Visual Studio Installer > Individual components > Windows SDK) or set FM4_FXC."
}

# Every variant the native executor references: name, profile, source, defines.
$variants = New-Object System.Collections.Generic.List[object]
foreach ($sourceKind in 'color', 'depth', 'uint') {
    $sourceDefines = @()
    if ($sourceKind -eq 'depth') { $sourceDefines += 'FH1_SOURCE_DEPTH=1' }
    if ($sourceKind -eq 'uint') { $sourceDefines += 'FH1_SOURCE_UINT=1' }
    foreach ($sourceMsaa in '', '_ms') {
        $msaaDefines = @($sourceDefines)
        if ($sourceMsaa) { $msaaDefines += 'FH1_SOURCE_MSAA=1' }
        $variants.Add(@("native_resolve_memory_${sourceKind}${sourceMsaa}_cs", 'cs_5_1', 'native_resolve_memory.cs.hlsl', $msaaDefines))
        $variants.Add(@("native_transfer_words_${sourceKind}${sourceMsaa}_cs", 'cs_5_1', 'native_transfer_words.cs.hlsl', $msaaDefines))
        $destIndex = 0
        foreach ($destKind in 'color', 'depth', 'stencil', 'uint') {
            foreach ($destMsaa in '', '_dms') {
                $destDefines = @($msaaDefines) + "FH1_DEST_KIND=$destIndex"
                if ($destMsaa) { $destDefines += 'FH1_DEST_MSAA=1' }
                $variants.Add(@("native_transfer_${destKind}${destMsaa}_from_${sourceKind}${sourceMsaa}_ps", 'ps_5_1', 'native_transfer.ps.hlsl', $destDefines))
            }
            $destIndex++
        }
    }
}
$kind = 1
foreach ($destKind in 'depth', 'stencil') {
    foreach ($destMsaa in '', '_dms') {
        $defines = @("FH1_DEST_KIND=$kind")
        if ($destMsaa) { $defines += 'FH1_DEST_MSAA=1' }
        $variants.Add(@("native_transfer_${destKind}${destMsaa}_from_words_ps", 'ps_5_1', 'native_transfer_from_words.ps.hlsl', $defines))
    }
    $kind = 2
}

# Any source change (including the shared .hlsli files) rebuilds everything.
$newestSource = (Get-ChildItem -LiteralPath $sourceDir -File -Include '*.hlsl', '*.hlsli' -Recurse |
    Measure-Object -Property LastWriteTimeUtc -Maximum).Maximum
[IO.Directory]::CreateDirectory($outDir) | Out-Null
$compiled = 0
foreach ($variant in $variants) {
    $name, $shaderProfile, $file, $defines = $variant
    $header = Join-Path $outDir "$name.h"
    if (-not $Force -and (Test-Path -LiteralPath $header) -and
        (Get-Item -LiteralPath $header).LastWriteTimeUtc -ge $newestSource) {
        continue
    }
    $fxcArgs = @('/nologo', '/T', $shaderProfile, '/E', 'main', '/O3')
    foreach ($define in $defines) { $fxcArgs += @('/D', $define) }
    $fxcArgs += @('/Vn', $name, '/Fh', $header, (Join-Path $sourceDir $file))
    & $fxc @fxcArgs | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "fxc failed to compile $name (exit $LASTEXITCODE)"
    }
    $compiled++
}
Write-Host "Native executor shaders: $compiled of $($variants.Count) compiled ($fxc)"
