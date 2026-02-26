#Requires -RunAsAdministrator
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

$GSTREAMER_VERSION = "1.26.6"
$OPENVINO_VERSION = "2025.4.0"
$OPENVINO_VERSION_SHORT = "2025.4"
$GSTREAMER_DEST_FOLDER = "$env:ProgramFiles\gstreamer"
$OPENVINO_INSTALL_FOLDER = "$env:LOCALAPPDATA\Programs\openvino"
$DLSTREAMER_TMP = "$env:TEMP\dlstreamer_tmp"

# Create temporary directory if it doesn't exist
if (-Not (Test-Path $DLSTREAMER_TMP)) {
	mkdir $DLSTREAMER_TMP
}

function Update-Path {
	$env:PATH = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
}

function Write-Section {
	param(
		[string]$Message,
		[int]$Width = 120
	)
	$totalPadding = $Width - $Message.Length - 2
	if ($totalPadding -lt 0) {
		Write-Host $Message
		return
	}
	$leftPad = [Math]::Floor($totalPadding / 2.0)
	$rightPad = [Math]::Ceiling($totalPadding / 2.0)
	$line = ("#" * $leftPad) + " " + $Message + " " + ("#" * $rightPad)
	Write-Host $line
}

function Invoke-DownloadFile {
	param(
		[string]$Uri,
		[string]$OutFile,
		[string]$UserAgent
	)
	if (Test-Path $OutFile) {
		Write-Host "Using cached: $OutFile"
		return
	}
	$tempFile = "$OutFile.downloading"
	# Clean up any previous incomplete download
	if (Test-Path $tempFile) {
		Remove-Item -Path $tempFile -Force
	}
	try {
		$params = @{ Uri = $Uri; OutFile = $tempFile }
		if ($UserAgent) { $params.UserAgent = $UserAgent }
		Invoke-WebRequest @params
		Move-Item -Path $tempFile -Destination $OutFile -Force
	}
	catch {
		if (Test-Path $tempFile) {
			Remove-Item -Path $tempFile -Force
		}
		throw "Download failed for ${Uri}: $_"
	}
}

# ============================================================================
# Visual C++ Runtime
# ============================================================================
Write-Section "Checking Visual C++ Runtime"
$MSVC_RUNTIME_INSTALLED = $false
try {
	$msvcVersion = (Get-ItemProperty -Path "HKLM:\SOFTWARE\Wow6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes\X64" -Name "Version" -ErrorAction SilentlyContinue).Version
	if ($msvcVersion) {
		Write-Host "Visual C++ Runtime already installed: $msvcVersion"
		$MSVC_RUNTIME_INSTALLED = $true
	}
}
catch {
}

if (-Not $MSVC_RUNTIME_INSTALLED) {
	Write-Host "Visual C++ Runtime not found, downloading and installing..."
	$MSVC_INSTALLER = "$DLSTREAMER_TMP\vc_redist.x64.exe"
	Invoke-DownloadFile -Uri "https://aka.ms/vc14/vc_redist.x64.exe" -OutFile $MSVC_INSTALLER
	$process = Start-Process -Wait -PassThru -FilePath $MSVC_INSTALLER -ArgumentList "/install", "/quiet", "/norestart"
	if ($process.ExitCode -eq 0 -or $process.ExitCode -eq 3010) {
		Write-Host "Visual C++ Runtime installed successfully"
		if ($process.ExitCode -eq 3010) {
			Write-Host "Note: A system restart may be required to complete the installation"
		}
	}
	else {
		Write-Error "Visual C++ Runtime installation failed with exit code: $($process.ExitCode)"
	}
}
Write-Section "Done"

# ============================================================================
# GStreamer
# ============================================================================
$GSTREAMER_NEEDS_INSTALL = $false
$GSTREAMER_INSTALL_MODE = "none"  # values: none | fresh | upgrade

# Check registry for GStreamer installation
try {
	$regPath = "HKLM:\SOFTWARE\GStreamer1.0\x86_64"
	$regInstallDir = (Get-ItemProperty -Path $regPath -Name "InstallDir" -ErrorAction SilentlyContinue).InstallDir
	$regVersion = (Get-ItemProperty -Path $regPath -Name "Version" -ErrorAction SilentlyContinue).Version

	if ($regInstallDir -and $regVersion) {
		Write-Host "GStreamer found in registry - InstallDir: $regInstallDir, Version: $regVersion"
		$GSTREAMER_DEST_FOLDER = $regInstallDir.TrimEnd('\')
		# Check for conflicting architectures first
		$expectedPath = "$GSTREAMER_DEST_FOLDER\1.0\msvc_x86_64"
		$envMsvcX64 = [Environment]::GetEnvironmentVariable('GSTREAMER_1_0_ROOT_MSVC_X86_64', 'Machine')

		if ($envMsvcX64 -and ($envMsvcX64.TrimEnd('\') -ne $expectedPath)) {
			Write-Host "Warning: GSTREAMER_1_0_ROOT_MSVC_X86_64 points to unexpected location: $envMsvcX64"
		}
		$conflictingArchs = @()
		if ([Environment]::GetEnvironmentVariable('GSTREAMER_1_0_ROOT_MSVC_X86', 'Machine')) {
			$conflictingArchs += 'msvc_x86'
		}
		if ([Environment]::GetEnvironmentVariable('GSTREAMER_1_0_ROOT_MINGW_X86_64', 'Machine')) {
			$conflictingArchs += 'mingw_x86_64'
		}
		if ([Environment]::GetEnvironmentVariable('GSTREAMER_1_0_ROOT_MINGW_X86', 'Machine')) {
			$conflictingArchs += 'mingw_x86'
		}
		if ($conflictingArchs.Count -gt 0) {
			Write-Host "Warning: Found conflicting GStreamer architectures: $($conflictingArchs -join ', ')"
			Write-Host "Multiple GStreamer architectures may cause conflicts. Only msvc_x86_64 is supported."
		}
		# Parse and compare versions
		$installedParts = $regVersion.Split('.') | ForEach-Object { [int]$_ }
		$requiredParts = $GSTREAMER_VERSION.Split('.') | ForEach-Object { [int]$_ }
		$needsUpgrade = $false
		for ($i = 0; $i -lt [Math]::Max($installedParts.Length, $requiredParts.Length); $i++) {
			$installedPart = if ($i -lt $installedParts.Length) { $installedParts[$i] } else { 0 }
			$requiredPart = if ($i -lt $requiredParts.Length) { $requiredParts[$i] } else { 0 }
			if ($installedPart -lt $requiredPart) {
				$needsUpgrade = $true
				break
			}
			elseif ($installedPart -gt $requiredPart) {
				# Installed version is newer, no upgrade needed
				break
			}
		}

		if ($needsUpgrade) {
			Write-Host "GStreamer upgrade available - installed: $regVersion, required: $GSTREAMER_VERSION - upgrading"
			$GSTREAMER_NEEDS_INSTALL = $true
			$GSTREAMER_INSTALL_MODE = "upgrade"
		}
		else {
			# Verify installation directory structure exists
			$VERSION_SPECIFIC_PATH = "$GSTREAMER_DEST_FOLDER\1.0\msvc_x86_64"
			if (-Not (Test-Path $VERSION_SPECIFIC_PATH)) {
				Write-Host "GStreamer installation incomplete - msvc_x86_64 directory not found - reinstallation needed"
				$GSTREAMER_NEEDS_INSTALL = $true
				$GSTREAMER_INSTALL_MODE = "fresh"
			}
			else {
				Write-Host "GStreamer version $regVersion verified (compatible with $GSTREAMER_VERSION)"
				$GSTREAMER_NEEDS_INSTALL = $false
			}
		}
	}
	else {
		Write-Host "GStreamer not found in registry - installation needed"
		$GSTREAMER_NEEDS_INSTALL = $true
		$GSTREAMER_INSTALL_MODE = "fresh"
		$GSTREAMER_DEST_FOLDER = "$env:ProgramFiles\gstreamer"
	}
}
catch {
	Write-Host "GStreamer registry check failed - assuming not installed"
	$GSTREAMER_NEEDS_INSTALL = $true
	$GSTREAMER_INSTALL_MODE = "fresh"
	$GSTREAMER_DEST_FOLDER = "$env:ProgramFiles\gstreamer"
}

if ($GSTREAMER_NEEDS_INSTALL) {
	Write-Section "Preparing GStreamer ${GSTREAMER_VERSION}"

	$GSTREAMER_RUNTIME_INSTALLER = "${DLSTREAMER_TMP}\gstreamer-1.0-msvc-x86_64-${GSTREAMER_VERSION}.msi"
	$GSTREAMER_DEVEL_INSTALLER = "${DLSTREAMER_TMP}\gstreamer-1.0-devel-msvc-x86_64-${GSTREAMER_VERSION}.msi"

	Write-Host "Downloading GStreamer runtime installer..."
	Invoke-DownloadFile -UserAgent "curl/8.5.0" -OutFile $GSTREAMER_RUNTIME_INSTALLER -Uri "https://gstreamer.freedesktop.org/data/pkg/windows/${GSTREAMER_VERSION}/msvc/gstreamer-1.0-msvc-x86_64-${GSTREAMER_VERSION}.msi"

	Write-Host "Downloading GStreamer development installer..."
	Invoke-DownloadFile -UserAgent "curl/8.5.0" -OutFile $GSTREAMER_DEVEL_INSTALLER -Uri "https://gstreamer.freedesktop.org/data/pkg/windows/${GSTREAMER_VERSION}/msvc/gstreamer-1.0-devel-msvc-x86_64-${GSTREAMER_VERSION}.msi"

	if ($GSTREAMER_INSTALL_MODE -eq "fresh" -or $GSTREAMER_INSTALL_MODE -eq "upgrade") {
		Write-Host "Installing GStreamer runtime package..."
		$process = Start-Process -Wait -PassThru -FilePath "msiexec" -ArgumentList "/passive", "/i", $GSTREAMER_RUNTIME_INSTALLER, "/qn"
		if ($process.ExitCode -ne 0) {
			Write-Error "GStreamer runtime installation failed with exit code: $($process.ExitCode)"
		}
		Write-Host "Installing GStreamer development package..."
		$process = Start-Process -Wait -PassThru -FilePath "msiexec" -ArgumentList "/passive", "/i", $GSTREAMER_DEVEL_INSTALLER, "/qn"
		if ($process.ExitCode -ne 0) {
			Write-Error "GStreamer development installation failed with exit code: $($process.ExitCode)"
		}
		# FIXME: Remove this section after GStreamer 1.28
		$pkgConfigFile = "$GSTREAMER_DEST_FOLDER\1.0\msvc_x86_64\lib\pkgconfig\gstreamer-analytics-1.0.pc"
		if (Test-Path $pkgConfigFile) {
			(Get-Content $pkgConfigFile).Replace('-lm', '') | Set-Content $pkgConfigFile
		}
		Write-Section "GStreamer installation completed"
	}
}
else {
	Write-Section "GStreamer ${GSTREAMER_VERSION} already installed"
}

# ============================================================================
# OpenVINO runtime DLLs
# ============================================================================
$CURRENT_DIR = (Get-Item .).FullName
$OPENVINO_SOURCE_FOLDER = $null
$OPENVINO_NEEDS_DOWNLOAD = $false

# Check if OpenVINO is installed in the standard location
if (Test-Path "$OPENVINO_INSTALL_FOLDER\setupvars.ps1") {
	Write-Host "OpenVINO found in $OPENVINO_INSTALL_FOLDER"
	$VERSION_FILE = "$OPENVINO_INSTALL_FOLDER\runtime\version.txt"
	if (Test-Path $VERSION_FILE) {
		$VERSION_CONTENT = Get-Content $VERSION_FILE -First 1
		if ($VERSION_CONTENT -and $VERSION_CONTENT.StartsWith($OPENVINO_VERSION)) {
			$INSTALLED_VERSION_FULL = ($VERSION_CONTENT -split '-')[0]
			Write-Host "OpenVINO version $INSTALLED_VERSION_FULL verified - compatible with required $OPENVINO_VERSION"
			$OPENVINO_SOURCE_FOLDER = $OPENVINO_INSTALL_FOLDER
		}
		else {
			$INSTALLED_VERSION_FULL = ($VERSION_CONTENT -split '-')[0]
			Write-Host "OpenVINO version mismatch - installed: $INSTALLED_VERSION_FULL, required: $OPENVINO_VERSION"
			$OPENVINO_NEEDS_DOWNLOAD = $true
		}
	}
	else {
		$OPENVINO_NEEDS_DOWNLOAD = $true
	}
}
else {
	$OPENVINO_NEEDS_DOWNLOAD = $true
}

if ($OPENVINO_NEEDS_DOWNLOAD) {
	Write-Section "Downloading OpenVINO GenAI ${OPENVINO_VERSION}"

	$OPENVINO_INSTALLER = "${DLSTREAMER_TMP}\openvino_genai_windows_${OPENVINO_VERSION}.0_x86_64.zip"
	Write-Host "Downloading OpenVINO GenAI ${OPENVINO_VERSION}..."
	Invoke-DownloadFile -OutFile $OPENVINO_INSTALLER -Uri "https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/${OPENVINO_VERSION_SHORT}/windows/openvino_genai_windows_${OPENVINO_VERSION}.0_x86_64.zip"

	Write-Host "Extracting OpenVINO GenAI ${OPENVINO_VERSION}..."
	$EXTRACTED_FOLDER = "$DLSTREAMER_TMP\openvino_genai_windows_${OPENVINO_VERSION}.0_x86_64"
	if (Test-Path $EXTRACTED_FOLDER) {
		Remove-Item -LiteralPath $EXTRACTED_FOLDER -Recurse -Force
	}
	Expand-Archive -Path $OPENVINO_INSTALLER -DestinationPath "$DLSTREAMER_TMP" -Force
	$OPENVINO_SOURCE_FOLDER = $EXTRACTED_FOLDER
}

# Copy OpenVINO runtime DLLs to current directory
if ($OPENVINO_SOURCE_FOLDER) {
	Write-Host "Copying OpenVINO runtime DLLs to current directory"
	$OPENVINO_RUNTIME_DIR = "$OPENVINO_SOURCE_FOLDER\runtime\bin\intel64\Release"
	$OPENVINO_TBB_DIR = "$OPENVINO_SOURCE_FOLDER\runtime\3rdparty\tbb\bin"
	Get-ChildItem -Path $OPENVINO_RUNTIME_DIR -File | ForEach-Object {
		Copy-Item -Path $_.FullName -Destination $CURRENT_DIR -Force
	}
	Get-ChildItem -Path $OPENVINO_TBB_DIR -Filter "*.dll" -File | Where-Object { $_.Name -notlike "*_debug.dll" } | ForEach-Object {
		Copy-Item -Path $_.FullName -Destination $CURRENT_DIR -Force
	}
	Write-Section "Done"
}
else {
	Write-Error "Could not locate OpenVINO installation or download"
	exit 1
}

# ============================================================================
# Set Environment Variables
# ============================================================================

Write-Section "Setting User Environment Variables"
Write-Host 'Setting variables: GST_PLUGIN_PATH, Path (for DLLs)'
$CURRENT_DIR = (Get-Item .).FullName
[Environment]::SetEnvironmentVariable('GST_PLUGIN_PATH', "$CURRENT_DIR", [System.EnvironmentVariableTarget]::User)
$USER_PATH = [Environment]::GetEnvironmentVariable('Path', 'User')
$pathEntries = $USER_PATH -split ';'
if (-Not ($pathEntries -contains $CURRENT_DIR)) {
	[Environment]::SetEnvironmentVariable('Path', ($USER_PATH + ';' + $CURRENT_DIR), [System.EnvironmentVariableTarget]::User)
	Write-Host 'Added current directory to User Path variable'
}

Write-Host 'Setting variables: Path (for gst-launch-1.0)'
$GSTREAMER_BIN_DIR = "$GSTREAMER_DEST_FOLDER\1.0\msvc_x86_64\bin"
$USER_PATH = [Environment]::GetEnvironmentVariable('Path', 'User')
$pathEntries = $USER_PATH -split ';'
if (-Not ($pathEntries -contains $GSTREAMER_BIN_DIR)) {
	[Environment]::SetEnvironmentVariable('Path', $USER_PATH + ';' + $GSTREAMER_BIN_DIR, [System.EnvironmentVariableTarget]::User)
	Write-Host 'Added gst-launch-1.0 directory to User Path variable'
}

Update-Path
$env:GST_PLUGIN_PATH = [System.Environment]::GetEnvironmentVariable('GST_PLUGIN_PATH', 'User')

Write-Host "Path:"
$env:Path
Write-Host "GST_PLUGIN_PATH:"
$env:GST_PLUGIN_PATH
Write-Section "Done"

# Check if gvadetect element is available
try {
	if (Test-Path "$env:LOCALAPPDATA\Microsoft\Windows\INetCache\gstreamer-1.0\registry.x86_64-msvc.bin") {
		Write-Host "Clearing existing GStreamer cache"
		Remove-Item "$env:LOCALAPPDATA\Microsoft\Windows\INetCache\gstreamer-1.0\registry.x86_64-msvc.bin"
	}
	Write-Host "Generating GStreamer cache. It may take up to a few minutes. Please wait for a moment..."
	$(gst-inspect-1.0.exe gvadetect)
	Write-Host "DLStreamer is ready"
}
catch {
	Write-Host "Error: Failed to inspect gvadetect element."
	Write-Host "Error details: $_"
	Write-Host "Please try updating GPU/NPU drivers and rebooting the system."
	Write-Host "Optionally run the command to debug plugin loading:"
	Write-Host "  $env:GST_DEBUG=`"GST_PLUGIN_LOADING:5,GST_REGISTRY:5`"; gst-inspect-1.0 gvadetect 2>&1 | tee plugin_loading.txt"
}
