# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#
# Builds gstanalytics.zip from a patched gstreamer source tree.
#
# Steps:
#   1. Download the gstreamer monorepo tarball into $env:TEMP\dlstreamer_tmp
#   2. Extract it once into a stable sibling directory and seed a local git
#      baseline. The baseline is preserved across runs for incremental builds.
#   3. Hard-reset the source tree and re-apply
#      dependencies/patches/0020-gst-analytics.patch every run.
#   4. Configure and build dependencies/windows/CMakeLists.txt to produce
#      gstanalytics-1.0-0.dll.
#   5. Compile the GIR into a typelib using g-ir-compiler from the existing
#      GStreamer install (the GIR ships in this repo).
#   6. Stage the layout and write dependencies/windows/gstanalytics.zip.
#
# Caller must already have the MSVC build environment activated. 
# The output zip is consumed by install_gstanalytics_patch.ps1.

#Requires -Version 5.1

param(
	[string]$GStreamerVersion = "1.28.2",
	[string]$GStreamerDir,
	[string]$OutputZip
)

$ErrorActionPreference = 'Stop'

$ScriptDir = $PSScriptRoot
$RepoRoot  = [System.IO.Path]::GetFullPath((Join-Path $ScriptDir "..\.."))
$PatchFile = Join-Path $RepoRoot "dependencies\patches\0020-gst-analytics.patch"
$RepoGir   = Join-Path $ScriptDir "GstAnalytics-1.0.gir"

if (-not $GStreamerDir) {
	$reg = Get-ItemProperty -Path "HKLM:\SOFTWARE\GStreamer1.0\x86_64" -Name "InstallDir" -ErrorAction SilentlyContinue
	if ($reg -and $reg.InstallDir) {
		$GStreamerDir = $reg.InstallDir.TrimEnd('\')
	}
	else {
		throw "GStreamerDir not provided and could not be read from registry"
	}
}
if (-not (Test-Path $GStreamerDir)) {
	throw "GStreamer install dir not found: $GStreamerDir"
}

if (-not $OutputZip) {
	$OutputZip = Join-Path $ScriptDir "gstanalytics.zip"
}

if (-not (Test-Path $PatchFile)) {
	throw "Patch file not found: $PatchFile"
}
if (-not (Test-Path $RepoGir)) {
	throw "GstAnalytics-1.0.gir not found in repo: $RepoGir"
}

$gitCmd = Get-Command git -ErrorAction SilentlyContinue
if (-not $gitCmd) { throw "git is required to apply the gst-analytics patch" }
$cmakeCmd = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmakeCmd) { throw "cmake is required to build gstanalytics" }
$girCompiler = Join-Path $GStreamerDir "bin\g-ir-compiler.exe"
if (-not (Test-Path $girCompiler)) {
	throw "g-ir-compiler.exe not found in GStreamer install: $girCompiler"
}
# Prefer the Windows-native bsdtar (System32\tar.exe).
$tarExe = Join-Path $env:SystemRoot 'System32\tar.exe'
if (-not (Test-Path $tarExe)) {
	$fallback = Get-Command tar -ErrorAction SilentlyContinue
	if (-not $fallback) { throw "tar is required to extract the gstreamer tarball" }
	$tarExe = $fallback.Source
}

$DlstreamerTmp = Join-Path $env:TEMP "dlstreamer_tmp"
if (-not (Test-Path $DlstreamerTmp)) {
	New-Item -ItemType Directory -Path $DlstreamerTmp | Out-Null
}

# gst-plugins-bad release tarball: ~8 MB, contains gst-libs/gst/analytics
# directly at the root.
$TarballName = "gst-plugins-bad-$GStreamerVersion.tar.xz"
$TarballPath = Join-Path $DlstreamerTmp $TarballName
$TarballUrl  = "https://gstreamer.freedesktop.org/src/gst-plugins-bad/$TarballName"

$SrcRoot      = Join-Path $DlstreamerTmp "gst-plugins-bad-$GStreamerVersion"
$BuildDir     = Join-Path $DlstreamerTmp "gstanalytics-build-$GStreamerVersion"
$AnalyticsSrc = Join-Path $SrcRoot "gst-libs\gst\analytics"
$BaselineStamp = Join-Path $DlstreamerTmp "gst-plugins-bad-$GStreamerVersion.baseline"

function Write-Section { param([string]$Message) Write-Host "==== $Message ====" -ForegroundColor Cyan }

# ---------------------------------------------------------------------------
# 1. Download tarball (cached)
# ---------------------------------------------------------------------------
if (-not (Test-Path $TarballPath)) {
	Write-Section "Downloading $TarballName"
	$tmp = "$TarballPath.downloading"
	if (Test-Path $tmp) { Remove-Item -Path $tmp -Force }
	try {
		Invoke-WebRequest -Uri $TarballUrl -OutFile $tmp -UseBasicParsing
		Move-Item -Path $tmp -Destination $TarballPath -Force
	}
	catch {
		if (Test-Path $tmp) { Remove-Item -Path $tmp -Force -ErrorAction SilentlyContinue }
		throw "Failed to download $TarballUrl : $_"
	}
}
else {
	Write-Host "Using cached tarball: $TarballPath"
}

# ---------------------------------------------------------------------------
# 2. Extract once + seed a local git baseline
# ---------------------------------------------------------------------------
if (-not (Test-Path $BaselineStamp)) {
	Write-Section "Extracting $TarballName"
	if (Test-Path $SrcRoot) { Remove-Item -LiteralPath $SrcRoot -Recurse -Force }
	# tar.exe (bsdtar) on Win10+ extracts .tar.xz; -C requires the dir to exist.
	if (-not (Test-Path $DlstreamerTmp)) {
		New-Item -ItemType Directory -Path $DlstreamerTmp | Out-Null
	}
	& $tarExe -xf $TarballPath -C $DlstreamerTmp
	if ($LASTEXITCODE -ne 0) { throw "Tarball extraction failed (exit $LASTEXITCODE)" }
	if (-not (Test-Path $SrcRoot)) {
		throw "Expected extracted dir not found: $SrcRoot"
	}

	Push-Location $SrcRoot
	try {
		# Force LF line endings + suppress CRLF warnings — Git's stderr noise
		# trips PowerShell's NativeCommandError stop behavior. Funneling
		# 2>&1 -> stdout makes those lines just show up as Write-Output.
		& git -c init.defaultBranch=baseline -c core.autocrlf=false -c core.safecrlf=false init -q 2>&1 | Write-Host
		& git -c core.autocrlf=false -c core.safecrlf=false `
			-c user.email=baseline@local -c user.name=baseline add -A 2>&1 | Write-Host
		& git -c core.autocrlf=false -c core.safecrlf=false `
			-c user.email=baseline@local -c user.name=baseline commit -q -m "baseline" --allow-empty 2>&1 | Write-Host
		if ($LASTEXITCODE -ne 0) { throw "Failed to seed git baseline" }
	}
	finally { Pop-Location }

	New-Item -ItemType File -Path $BaselineStamp -Force | Out-Null
	Write-Host "Source baseline ready: $SrcRoot"
}
else {
	Write-Host "Reusing cached source tree: $SrcRoot"
}

# ---------------------------------------------------------------------------
# 3. Reset + reapply patch every run
# ---------------------------------------------------------------------------
Write-Section "Resetting source tree and applying patch"
Push-Location $SrcRoot
try {
	& git -c core.autocrlf=false -c core.safecrlf=false reset --hard --quiet 2>&1 | Write-Host
	if ($LASTEXITCODE -ne 0) { throw "git reset failed" }
	& git -c core.autocrlf=false -c core.safecrlf=false clean -fdx --quiet 2>&1 | Write-Host
	if ($LASTEXITCODE -ne 0) { throw "git clean failed" }
	# Patch uses monorepo paths (a/subprojects/gst-plugins-bad/...), so strip
	# 3 leading components when applying against the gst-plugins-bad tarball.
	# --include filters by post-strip paths, restricting the apply to the
	# gst-libs/ tree; the python overrides hunk (subprojects/gst-python/...)
	# is handled separately by setup_python_env.ps1.
	& git -c core.autocrlf=false -c core.safecrlf=false apply -p3 --include="gst-libs/*" `
		--ignore-whitespace --whitespace=nowarn $PatchFile 2>&1 | Write-Host
	if ($LASTEXITCODE -ne 0) { throw "Failed to apply patch $PatchFile" }
}
finally { Pop-Location }

if (-not (Test-Path "$AnalyticsSrc\gstanalyticsgroupmtd.c")) {
	throw "Patch did not produce expected file: $AnalyticsSrc\gstanalyticsgroupmtd.c"
}

# ---------------------------------------------------------------------------
# 4. Configure + build via CMake
# ---------------------------------------------------------------------------
Write-Section "Configuring CMake"
if (Test-Path $BuildDir) { Remove-Item -LiteralPath $BuildDir -Recurse -Force }
New-Item -ItemType Directory -Path $BuildDir | Out-Null

& cmake `
	-S $ScriptDir `
	-B $BuildDir `
	-G "NMake Makefiles" `
	-DCMAKE_BUILD_TYPE=Release `
	"-DGST_ANALYTICS_SRC_DIR=$AnalyticsSrc" `
	"-DGSTREAMER_PREFIX=$GStreamerDir"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Section "Building gstanalytics"
& cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }

$StageDir = Join-Path $BuildDir "stage"
$DllOut = Join-Path $StageDir "bin\gstanalytics-1.0-0.dll"
if (-not (Test-Path $DllOut)) { throw "Built DLL not found: $DllOut" }

# ---------------------------------------------------------------------------
# 5. GIR + typelib (GIR is checked in; typelib is generated)
# ---------------------------------------------------------------------------
Write-Section "Generating typelib"
$GirDestDir     = Join-Path $StageDir "share\gir-1.0"
$TypelibDestDir = Join-Path $StageDir "lib\girepository-1.0"
New-Item -ItemType Directory -Path $GirDestDir     -Force | Out-Null
New-Item -ItemType Directory -Path $TypelibDestDir -Force | Out-Null

$GirStaged   = Join-Path $GirDestDir     "GstAnalytics-1.0.gir"
$TypelibPath = Join-Path $TypelibDestDir "GstAnalytics-1.0.typelib"

Copy-Item -LiteralPath $RepoGir -Destination $GirStaged -Force

$existingGirDir = Join-Path $GStreamerDir "share\gir-1.0"
& $girCompiler --includedir=$existingGirDir --output=$TypelibPath $GirStaged
if ($LASTEXITCODE -ne 0) { throw "g-ir-compiler failed" }

# ---------------------------------------------------------------------------
# 6. Pack zip
# ---------------------------------------------------------------------------
Write-Section "Creating $OutputZip"
if (Test-Path $OutputZip) { Remove-Item -LiteralPath $OutputZip -Force }

$items = @(
	(Join-Path $StageDir "bin"),
	(Join-Path $StageDir "lib"),
	(Join-Path $StageDir "include"),
	(Join-Path $StageDir "share")
) | Where-Object { Test-Path $_ }

Compress-Archive -Path $items -DestinationPath $OutputZip -Force

Write-Host "Wrote $OutputZip"
$hash = (Get-FileHash -Path $OutputZip -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "SHA256: $hash"
