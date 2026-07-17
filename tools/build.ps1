param(
    [string]$BuildDirectory = ''
)

$ErrorActionPreference = 'Stop'

# Codex sandbox processes can inherit both `Path` and `PATH`. MSBuild treats its
# environment dictionary as case-insensitive and fails before invoking cl.exe.
# Normalize only this build process; user and machine environment variables are
# not changed.
$pathValue = [string]$env:Path
for ($attempt = 0; $attempt -lt 4; $attempt++) {
    $duplicate = [Environment]::GetEnvironmentVariables('Process').GetEnumerator() |
        Where-Object { [string]::Equals([string]$_.Key, 'Path', [StringComparison]::OrdinalIgnoreCase) } |
        Select-Object -First 1
    if (-not $duplicate) {
        break
    }
    [Environment]::SetEnvironmentVariable([string]$duplicate.Key, $null, 'Process')
}
[Environment]::SetEnvironmentVariable('Path', $pathValue, 'Process')

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer could not be found.'
}

$vsPath = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$generator = 'Visual Studio 17 2022'

if (-not $vsPath) {
    $vsPath = & $vswhere -latest -products '*' -version '[18.0,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsPath) {
        $generator = 'Visual Studio 18 2026'
        Write-Warning 'Visual Studio 2022 was not found. Building this project with Visual Studio 2026.'
        Write-Warning 'This build does not satisfy the final Visual Studio 2022 acceptance requirement.'
    }
}

if (-not $vsPath) {
    throw 'The Visual C++ x64 build tools are not installed.'
}

$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) {
    throw 'The Visual Studio CMake component is not installed.'
}
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    Join-Path $repoRoot 'build'
}
elseif ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
}
else {
    Join-Path $repoRoot $BuildDirectory
}
Push-Location $repoRoot
try {
    & $cmake --fresh -S . -B $buildRoot -G $generator -A x64
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & $cmake --build $buildRoot --config Release
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & $ctest --test-dir $buildRoot -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    Write-Host ''
    Write-Host "Application: $(Join-Path $buildRoot 'Release\CodexQuotaTaskbar.exe')"
    Write-Host "Built: $(Join-Path $buildRoot 'Release\CodexQuotaTaskbarPrototype.exe')"
    Write-Host "Probe: $(Join-Path $buildRoot 'Release\TaskbarProbe.exe')"
    Write-Host "Interaction probe: $(Join-Path $buildRoot 'Release\TaskbarInteractionProbe.exe')"
    Write-Host "Lifecycle probe: $(Join-Path $buildRoot 'Release\AppLifecycleProbe.exe')"
}
finally {
    Pop-Location
}
