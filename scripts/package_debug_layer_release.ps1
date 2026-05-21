param(
    [string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path,
    [string]$BuildDir = "build-android",
    [string]$ReleaseName = "ExynosTools_V3.0_STABLE_DEBUG_LAYER",
    [string]$DesktopDir = "$env:USERPROFILE\Desktop"
)

$ErrorActionPreference = "Stop"

$buildPath = Join-Path $RepoRoot $BuildDir
$soPath = Join-Path $buildPath "libVkLayer_ExynosTools.so"
$jsonPath = Join-Path $buildPath "VkLayer_exynostools.json"
$readmePath = Join-Path $RepoRoot "docs\ANDROID_VALIDATION.md"

if (-not (Test-Path $soPath)) {
    throw "Missing .so: $soPath. Build first with scripts/configure_android_local_repos.ps1"
}
if (-not (Test-Path $jsonPath)) {
    throw "Missing layer manifest: $jsonPath. Re-run CMake configure/generate first."
}

$releaseDir = Join-Path $DesktopDir $ReleaseName
$zipPath = Join-Path $DesktopDir "$ReleaseName.zip"

if (Test-Path $releaseDir) {
    Remove-Item -LiteralPath $releaseDir -Recurse -Force
}
if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

New-Item -ItemType Directory -Path $releaseDir | Out-Null

Copy-Item -LiteralPath $soPath -Destination (Join-Path $releaseDir "libVkLayer_ExynosTools.so")
Copy-Item -LiteralPath $jsonPath -Destination (Join-Path $releaseDir "VkLayer_exynostools.json")
Copy-Item -LiteralPath (Join-Path $RepoRoot "exynostools_config.ini") -Destination (Join-Path $releaseDir "exynostools_config.ini")
Copy-Item -LiteralPath (Join-Path $RepoRoot "CHANGELOG_V3.0.txt") -Destination (Join-Path $releaseDir "CHANGELOG_V3.0.txt")
if (Test-Path $readmePath) {
    Copy-Item -LiteralPath $readmePath -Destination (Join-Path $releaseDir "README_ANDROID_LAYER.md")
}

$zipItems = Get-ChildItem -LiteralPath $releaseDir | ForEach-Object { $_.FullName }
Compress-Archive -Path $zipItems -DestinationPath $zipPath -CompressionLevel Optimal

Write-Output "RELEASE_DIR=$releaseDir"
Write-Output "RELEASE_ZIP=$zipPath"
