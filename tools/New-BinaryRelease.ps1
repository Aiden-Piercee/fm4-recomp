[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildRoot,
    [Parameter(Mandatory)][string]$SdkSourceRoot,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$')][string]$Version='0.1.0-alpha.2'
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$name="fm4-recomp-$Version-win-x64"
$stage=Join-Path $OutputDirectory $name
if (Test-Path -LiteralPath $stage) { throw "Output already exists: $stage" }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
# Only these named native outputs enter the player package.
foreach ($file in 'fm4.exe','fm4_SpeechFacade_default.dll','fm4_XMediaFacade_default.dll','rexruntime.dll','rexgpu-xenos.dll','TracyClient.dll') {
    Copy-Item -LiteralPath (Join-Path $BuildRoot $file) -Destination $stage
}
$config=Get-Content -LiteralPath (Join-Path $root 'fm4/fm4_runtime.toml') -Raw
$config=$config -replace '(?m)^game_data_root\s*=.*$', 'game_data_root = "game"'
$config=$config -replace '(?m)^user_data_root\s*=.*$', 'user_data_root = "user"'
Set-Content -LiteralPath (Join-Path $stage 'fm4.toml') -Value $config -Encoding utf8NoBOM
Copy-Item -LiteralPath (Join-Path $root 'Launch FM4 Player.bat') -Destination (Join-Path $stage 'Launch FM4.bat')
Copy-Item -LiteralPath (Join-Path $root 'docs/PLAYING.md') -Destination (Join-Path $stage 'README.md')
Copy-Item -LiteralPath (Join-Path $root 'LICENSE'),(Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination $stage
New-Item -ItemType Directory -Path (Join-Path $stage 'game'),(Join-Path $stage 'disc2-content') | Out-Null
# Use the compiler installation's redistributable DLLs, never arbitrary System32 copies.
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=(& $vswhere -products * -latest -property installationPath).Trim()
$crt=Get-ChildItem "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC14*.CRT" -Directory | Sort-Object FullName | Select-Object -Last 1
if (-not $crt) { throw 'App-local Visual C++ redistributable DLLs not found' }
Copy-Item -Path (Join-Path $crt.FullName '*.dll') -Destination $stage
$licenses=Join-Path $stage 'licenses'
New-Item -ItemType Directory -Path $licenses | Out-Null
Copy-Item -LiteralPath (Join-Path $SdkSourceRoot 'LICENSE') -Destination (Join-Path $licenses 'ReXGlue-LICENSE.txt')
foreach ($dependency in Get-ChildItem -LiteralPath (Join-Path $SdkSourceRoot 'thirdparty') -Directory) {
    $files=@(Get-ChildItem -LiteralPath $dependency.FullName -File | Where-Object {$_.Name -match '^(LICENSE|COPYING|NOTICE|COPYRIGHT)'})
    if ($files.Count) {
        $destination=Join-Path $licenses $dependency.Name
        New-Item -ItemType Directory -Path $destination | Out-Null
        $files | Copy-Item -Destination $destination
    }
}
$records=@(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    @{path=[IO.Path]::GetRelativePath($stage,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
})
@{platform='win-x64';version=$Version;game_files_included=$false;files=$records} | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $stage 'PACKAGE-MANIFEST.json') -Encoding utf8NoBOM
$zip=Join-Path $OutputDirectory "$name.zip"
if (Test-Path -LiteralPath $zip) { throw "Archive already exists: $zip" }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
$hash=(Get-FileHash -LiteralPath $zip).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$zip.sha256","$hash  $name.zip`n",[Text.Encoding]::ASCII)
Write-Output "Player package: $zip`nSHA256: $hash"
