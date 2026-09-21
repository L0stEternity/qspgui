<#
.SYNOPSIS
Build a release-ready QSP Classic player with MSVC and stage it as one folder.

.DESCRIPTION
Configures, builds, installs and packages the player in a single step, so the
result is a directory you can copy anywhere and run: the exe, qsp.dll, the
translations compiled from create_lang/*.po, and the soundfont, all side by
side, with a version baked into the binary.

This is the native Windows counterpart to build_release_windows.sh, which
cross-compiles the official 32-bit release inside docker. Same output layout,
no docker required.

The build directory is kept between runs, so the first build pays for compiling
wxWidgets from source (expect several minutes) and later ones are incremental.
Pass -Clean to start over.

.PARAMETER Version
Version to stamp into the binary and the package name. Defaults to `git
describe`, with any leading "v" removed.

.PARAMETER Arch
Win32 (default, matching the official releases) or x64.

.PARAMETER Generator
CMake generator for a fresh build directory. Ignored once one is configured.

.PARAMETER Classic
Build the classic wxHtmlWindow renderer instead of the wxWebView one.

.PARAMETER Tests
Also build and run the unit tests; a failure stops the release.

.PARAMETER Clean
Delete the build directory before configuring.

.PARAMETER NoZip
Leave the staged folder without also producing a .zip.

.EXAMPLE
.\build_release_msvc.ps1
Builds dist\qspgui-<git version>-win32\ and the matching .zip.

.EXAMPLE
.\build_release_msvc.ps1 -Version 5.9.6 -Tests
#>

[CmdletBinding()]
param(
    [string]$Version,
    [ValidateSet('Win32', 'x64')]
    [string]$Arch = 'Win32',
    [string]$BuildDir,
    [string]$OutDir = 'dist',
    [string]$Generator = 'Visual Studio 17 2022',
    [switch]$Classic,
    [switch]$Tests,
    [switch]$Clean,
    [switch]$NoZip
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = $PSScriptRoot

function Step($message) {
    Write-Host ""
    Write-Host "==> $message" -ForegroundColor Cyan
}

function Invoke-Checked {
    param([string]$Exe, [string[]]$Arguments)
    Write-Host "    $Exe $($Arguments -join ' ')" -ForegroundColor DarkGray
    # CMake and MSBuild report warnings on stderr. Under $ErrorActionPreference
    # = 'Stop' that alone aborts the script, so judge these by exit code only.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Exe @Arguments
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe failed with exit code $LASTEXITCODE"
    }
}

# --- Version -----------------------------------------------------------------
#
# A release should say what it is. When nothing is passed we ask git, so a build
# off a tag gets that tag and a build off a branch gets a version that names the
# commit it came from rather than a misleading round number.

if (-not $Version) {
    $described = & git -C $root describe --tags --dirty 2>$null
    if ($LASTEXITCODE -eq 0 -and $described) {
        $Version = ($described -replace '^v', '').Trim()
    } else {
        $Version = '0.0.0'
        Write-Warning "No git tag found; falling back to version $Version"
    }
}

if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+') {
    throw "Version '$Version' must start with MAJOR.MINOR.PATCH - CMake parses the numeric part out of it"
}

# What the player will actually show. The build keeps the full string as its
# id, but strips git describe's commits-ahead/hash/dirty tail before putting a
# version in front of the user; CMakeLists.txt does the same, and this is only
# so the summary below says what the title bar will say.
$displayVersion = $Version -replace '-dirty$', '' -replace '-[0-9]+-g[0-9a-fA-F]+$', ''

$archTag = if ($Arch -eq 'Win32') { 'win32' } else { 'x64' }
if (-not $BuildDir) { $BuildDir = Join-Path $root "build_release_$archTag" }
if (-not [System.IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $root $OutDir }

$renderer = if ($Classic) { 'classic wxHtmlWindow' } else { 'wxWebView' }
$packageName = "qspgui-$Version-$archTag"
$stageRoot = Join-Path $BuildDir 'stage'
$distDir = Join-Path $OutDir $packageName

Write-Host "QSP Classic release build" -ForegroundColor Green
Write-Host "  version   : $displayVersion"
if ($displayVersion -ne $Version) {
    Write-Host "  build id  : $Version"
}
Write-Host "  platform  : $Arch"
Write-Host "  renderer  : $renderer"
Write-Host "  build dir : $BuildDir"
Write-Host "  output    : $distDir"

# --- Configure ---------------------------------------------------------------

if ($Clean -and (Test-Path $BuildDir)) {
    Step "Removing $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

Step "Configuring"

$webview = if ($Classic) { 'OFF' } else { 'ON' }
$buildTests = if ($Tests) { 'ON' } else { 'OFF' }

$configureArgs = @('-S', $root, '-B', $BuildDir)

# -G/-A only apply to a fresh cache; passing them at a configured tree makes
# CMake refuse the whole run if they differ from what is already there.
if (Test-Path (Join-Path $BuildDir 'CMakeCache.txt')) {
    Write-Host "    reusing the existing cache in $BuildDir" -ForegroundColor DarkGray
} else {
    $configureArgs += @('-G', $Generator, '-A', $Arch)
}

$configureArgs += @(
    "-DAPP_VERSION=$Version",
    "-DQSPGUI_USE_WEBVIEW=$webview",
    "-DQSPGUI_BUILD_TESTS=$buildTests",
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_INSTALL_PREFIX=$stageRoot"
)
Invoke-Checked 'cmake' $configureArgs

# --- Build -------------------------------------------------------------------

Step "Building (Release)"
Invoke-Checked 'cmake' @('--build', $BuildDir, '--config', 'Release', '--parallel')

if ($Tests) {
    Step "Running tests"
    Invoke-Checked 'ctest' @('--test-dir', $BuildDir, '--build-config', 'Release', '--output-on-failure')
}

# --- Stage -------------------------------------------------------------------
#
# Install into a scratch prefix first, then flatten it. The install tree keeps
# the bin/ + lib/ split the Linux packages need; a portable Windows folder wants
# everything at the top level and has no use for the import libraries.

Step "Installing to a clean staging tree"
if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot }
Invoke-Checked 'cmake' @('--install', $BuildDir, '--config', 'Release', '--component', 'Main')

Step "Assembling $packageName"
if (Test-Path $distDir) { Remove-Item -Recurse -Force $distDir }
New-Item -ItemType Directory -Path $distDir -Force | Out-Null

$stageBin = Join-Path $stageRoot 'bin'
if (-not (Test-Path $stageBin)) { throw "Install produced nothing in $stageBin" }
Copy-Item -Path (Join-Path $stageBin '*') -Destination $distDir -Recurse -Force

# qspgui.cfg is written by the player at runtime and holds window geometry and
# the last used language. A copy left behind by a test run must not ship.
$strayConfig = Join-Path $distDir 'qspgui.cfg'
if (Test-Path $strayConfig) { Remove-Item -Force $strayConfig }

foreach ($doc in @('LICENSE', 'README.md', 'CHANGELOG.md')) {
    $source = Join-Path $root $doc
    if (Test-Path $source) { Copy-Item $source $distDir -Force }
}

# --- Verify ------------------------------------------------------------------
#
# Cheap checks that catch the two things that have actually gone wrong before:
# a binary with no version stamped in, and a package missing its data files.

Step "Verifying the package"

$exe = Join-Path $distDir 'qspgui.exe'
if (-not (Test-Path $exe)) { throw "qspgui.exe is missing from $distDir" }

$stamped = (Get-Item $exe).VersionInfo.FileVersion
if (-not $stamped) { throw "qspgui.exe carries no version resource" }
Write-Host "    qspgui.exe file version: $stamped"

$expected = @('qsp.dll', 'langs', 'sound\midi.sf2')
foreach ($item in $expected) {
    $path = Join-Path $distDir $item
    if (-not (Test-Path $path)) { throw "$item is missing from the package" }
}

$catalogues = @(Get-ChildItem -Path (Join-Path $distDir 'langs') -Filter '*.mo' -Recurse)
$sources = @(Get-ChildItem -Path (Join-Path $root 'create_lang') -Filter 'qspgui_*.po')
if ($catalogues.Count -ne $sources.Count) {
    throw "Packaged $($catalogues.Count) translations but create_lang has $($sources.Count) .po files"
}
Write-Host "    translations: $($catalogues.Count) catalogues, compiled from create_lang"

$stale = @($catalogues | Where-Object {
    $po = Join-Path $root "create_lang\$($_.BaseName).po"
    (Test-Path $po) -and ((Get-Item $po).LastWriteTimeUtc -gt $_.LastWriteTimeUtc)
})
if ($stale.Count -gt 0) {
    throw "Translations older than their .po sources: $($stale.BaseName -join ', ')"
}

# --- Package -----------------------------------------------------------------

if (-not $NoZip) {
    Step "Zipping"
    $zip = Join-Path $OutDir "$packageName.zip"
    if (Test-Path $zip) { Remove-Item -Force $zip }
    Compress-Archive -Path $distDir -DestinationPath $zip -CompressionLevel Optimal
    Write-Host "    $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
}

Write-Host ""
Write-Host "Done. Run it with:" -ForegroundColor Green
Write-Host "  $exe"
