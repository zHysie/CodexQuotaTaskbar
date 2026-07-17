param(
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = 'build',
    [string]$OutputDirectory = 'dist'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
}
else {
    Join-Path $repoRoot $BuildDirectory
}
$outputRoot = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory
}
else {
    Join-Path $repoRoot $OutputDirectory
}
$executable = Join-Path $buildRoot "$Configuration\CodexQuotaTaskbar.exe"
$license = Join-Path $repoRoot 'LICENSE'
$releaseNotes = Join-Path $repoRoot 'RELEASE_NOTES.md'
$versionFile = Join-Path $buildRoot 'package-version.txt'
if (-not (Test-Path -LiteralPath $executable)) {
    throw 'Build the Release target before packaging.'
}
if (-not (Test-Path -LiteralPath $license)) {
    throw 'Project LICENSE is not selected. Packaging is intentionally blocked.'
}
if (-not (Test-Path -LiteralPath $releaseNotes)) {
    throw 'RELEASE_NOTES.md is required for a formal package.'
}
if (-not (Test-Path -LiteralPath $versionFile)) {
    throw 'Configured project version was not found. Run the build first.'
}
$version = (Get-Content -LiteralPath $versionFile -Raw -Encoding UTF8).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Configured project version is invalid: $version"
}

$output = Join-Path $outputRoot "CodexQuotaTaskbar-v$version-win-x64"
$archive = "$output.zip"
if (Test-Path -LiteralPath $output) {
    throw "Package directory already exists: $output"
}
if (Test-Path -LiteralPath $archive) {
    throw "Package archive already exists: $archive"
}
New-Item -ItemType Directory -Path $output | Out-Null
Copy-Item -LiteralPath $executable -Destination $output
Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') -Destination $output
Copy-Item -LiteralPath $releaseNotes -Destination $output
Copy-Item -LiteralPath $license -Destination $output
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md') -Destination $output
Copy-Item -LiteralPath (Join-Path $repoRoot 'third_party\nlohmann\LICENSE.MIT') -Destination (Join-Path $output 'nlohmann-json-LICENSE.MIT')
Write-Host "Package: $output"
Compress-Archive -LiteralPath $output -DestinationPath $archive -CompressionLevel Optimal
Write-Host "Archive: $archive"
