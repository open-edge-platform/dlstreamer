# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#
# Installs / verifies the gstanalytics patch on top of an existing GStreamer
# installation. The patch is distributed as gstanalytics.zip located next to
# this script, and is extracted directly into the GStreamer installation
# folder.
#
# State files written under the GStreamer install root:
#   gstanalytics_patch.sha256     - sha256 of the currently-installed zip
#   gstanalytics_patch.manifest   - one path per line (relative to install
#                                   root) of every file the previous install
#                                   wrote, so we can remove them cleanly
#                                   before extracting a different zip.
#
# Modes:
#   -Mode Check    Reports patch status. Exit codes:
#                    0 = Current   (stamp matches the zip's sha256)
#                    1 = Missing   (no stamp file present)
#                    2 = Outdated  (stamp does not match the zip's sha256)
#                  Used by both the CI build script (which rebuilds the zip
#                  every run, so the hash is recomputed from the actual zip)
#                  and the NSIS installer (which ships a fixed zip).
#
#   -Mode Install  Removes any files listed in a previous manifest, extracts
#                  gstanalytics.zip into GStreamerDir, then writes the new
#                  manifest and stamp. Exit 0 on success.

param(
	[Parameter(Mandatory = $true)]
	[ValidateSet('Check', 'Install')]
	[string]$Mode,

	[Parameter(Mandatory = $true)]
	[string]$GStreamerDir,

	[string]$PatchZip
)

$ErrorActionPreference = 'Stop'

if (-not $PatchZip) {
	$PatchZip = Join-Path $PSScriptRoot 'gstanalytics.zip'
}

$stampFile    = Join-Path $GStreamerDir 'gstanalytics_patch.sha256'
$manifestFile = Join-Path $GStreamerDir 'gstanalytics_patch.manifest'

function Get-StampSha256 {
	if (-not (Test-Path $stampFile)) { return $null }
	$content = Get-Content -Path $stampFile -First 1 -ErrorAction SilentlyContinue
	if (-not $content) { return $null }
	return $content.Trim().ToLowerInvariant()
}

function Get-ZipSha256 {
	if (-not (Test-Path $PatchZip)) { return $null }
	return (Get-FileHash -Path $PatchZip -Algorithm SHA256).Hash.ToLowerInvariant()
}

if ($Mode -eq 'Check') {
	$current = Get-StampSha256
	if ($null -eq $current) {
		Write-Output 'Missing'
		exit 1
	}
	$expected = Get-ZipSha256
	if ($null -eq $expected) {
		# No zip to compare against; treat as missing so caller decides.
		Write-Output 'Missing'
		exit 1
	}
	if ($current -eq $expected) {
		Write-Output 'Current'
		exit 0
	}
	Write-Output 'Outdated'
	exit 2
}

# Install mode
if (-not (Test-Path $PatchZip)) {
	Write-Error "Patch zip not found: $PatchZip"
	exit 1
}
if (-not (Test-Path $GStreamerDir)) {
	Write-Error "GStreamer install directory not found: $GStreamerDir"
	exit 1
}

# Remove files from the previous install (if any) so deletions in the new
# patch are honored. Only files listed in the manifest are touched.
if (Test-Path $manifestFile) {
	Write-Host "Cleaning up previous gstanalytics patch files"
	$prevFiles = Get-Content -Path $manifestFile -ErrorAction SilentlyContinue
	foreach ($rel in $prevFiles) {
		$rel = $rel.Trim()
		if (-not $rel) { continue }
		$abs = Join-Path $GStreamerDir $rel
		if (Test-Path -LiteralPath $abs -PathType Leaf) {
			Remove-Item -LiteralPath $abs -Force -ErrorAction SilentlyContinue
		}
	}
	Remove-Item -LiteralPath $manifestFile -Force -ErrorAction SilentlyContinue
}

Write-Host "Applying gstanalytics patch to $GStreamerDir"
# Build a manifest of file entries for the next install's cleanup, then let
# Expand-Archive do the extraction.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($PatchZip)
try {
	$installed = New-Object System.Collections.Generic.List[string]
	foreach ($entry in $zip.Entries) {
		if ($entry.FullName.EndsWith('/')) { continue }
		[void]$installed.Add($entry.FullName.Replace('/', '\'))
	}
}
finally {
	$zip.Dispose()
}

Expand-Archive -Path $PatchZip -DestinationPath $GStreamerDir -Force
Set-Content -Path $manifestFile -Value $installed -Encoding ASCII

$expectedSha = Get-ZipSha256
Set-Content -Path $stampFile -Value $expectedSha -Encoding ASCII -NoNewline
Write-Host "Wrote stamp file: $stampFile (sha256=$expectedSha)"
exit 0
