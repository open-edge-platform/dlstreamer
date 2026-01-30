#Requires -RunAsAdministrator
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

$GSTREAMER_VERSION = "1.26.6"
$OPENVINO_VERSION = "2025.4"
$GSTREAMER_DEST_FOLDER = "C:\gstreamer"
$OPENVINO_DEST_FOLDER = "C:\openvino"
$DLSTREAMER_TMP = "C:\dlstreamer_tmp"

# Create temporary directory if it doesn't exist
if (-Not (Test-Path $DLSTREAMER_TMP)) {
	mkdir $DLSTREAMER_TMP
}

# Check if GStreamer is installed and if it's the correct version
$GSTREAMER_NEEDS_INSTALL = $false
$GSTREAMER_INSTALL_MODE = "none"  # values: none | fresh | reinstall
if (-Not (Test-Path $GSTREAMER_DEST_FOLDER)) {
	Write-Host "GStreamer not found - installation needed"
	$GSTREAMER_NEEDS_INSTALL = $true
	$GSTREAMER_INSTALL_MODE = "fresh"
} else {
	Write-Host "GStreamer found in folder $GSTREAMER_DEST_FOLDER"

	# Check if the correct version is installed
	$VERSION_SPECIFIC_PATH = "$GSTREAMER_DEST_FOLDER\1.0\msvc_x86_64"
	if (-Not (Test-Path $VERSION_SPECIFIC_PATH)) {
		Write-Host "GStreamer installation incomplete - reinstallation needed"
		$GSTREAMER_NEEDS_INSTALL = $true
		$GSTREAMER_INSTALL_MODE = "reinstall"
	} else {
		# Try to get installed version from pkg-config file
		$INSTALLED_VERSION = $null
		$PKG_CONFIG_FILE = "$VERSION_SPECIFIC_PATH\lib\pkgconfig\gstreamer-1.0.pc"
		if (Test-Path $PKG_CONFIG_FILE) {
			$VERSION_LINE = Get-Content $PKG_CONFIG_FILE | Select-String "Version:"
			if ($VERSION_LINE) {
				$INSTALLED_VERSION = ($VERSION_LINE -split ":")[1].Trim()
			}
		}

		if ($INSTALLED_VERSION -and $INSTALLED_VERSION -ne $GSTREAMER_VERSION) {
			Write-Host "GStreamer version mismatch - installed: $INSTALLED_VERSION, required: $GSTREAMER_VERSION"
			$GSTREAMER_NEEDS_INSTALL = $true
			$GSTREAMER_INSTALL_MODE = "reinstall"
		} elseif ($INSTALLED_VERSION) {
			Write-Host "GStreamer version $INSTALLED_VERSION verified - correct version installed"
		} else {
			Write-Host "Warning: Could not verify GStreamer version, but installation appears complete"
		}
	}
}

if ($GSTREAMER_NEEDS_INSTALL) {
	Write-Host "##################################### Preparing GStreamer ${GSTREAMER_VERSION} #######################################"

	$GSTREAMER_RUNTIME_INSTALLER = "${DLSTREAMER_TMP}\\gstreamer-1.0-msvc-x86_64_${GSTREAMER_VERSION}.msi"
	$GSTREAMER_DEVEL_INSTALLER = "${DLSTREAMER_TMP}\\gstreamer-1.0-devel-msvc-x86_64_${GSTREAMER_VERSION}.msi"

	if (Test-Path $GSTREAMER_RUNTIME_INSTALLER) {
		Write-Host "Using existing GStreamer runtime installer: $GSTREAMER_RUNTIME_INSTALLER"
	} else {
		Write-Host "Downloading GStreamer runtime installer..."
		Invoke-WebRequest -UserAgent "curl" -OutFile $GSTREAMER_RUNTIME_INSTALLER -Uri https://gstreamer.freedesktop.org/data/pkg/windows/${GSTREAMER_VERSION}/msvc/gstreamer-1.0-msvc-x86_64-${GSTREAMER_VERSION}.msi
	}

	if (Test-Path $GSTREAMER_DEVEL_INSTALLER) {
		Write-Host "Using existing GStreamer development installer: $GSTREAMER_DEVEL_INSTALLER"
	} else {
		Write-Host "Downloading GStreamer development installer..."
		Invoke-WebRequest -UserAgent "curl" -OutFile $GSTREAMER_DEVEL_INSTALLER -Uri https://gstreamer.freedesktop.org/data/pkg/windows/${GSTREAMER_VERSION}/msvc/gstreamer-1.0-devel-msvc-x86_64-${GSTREAMER_VERSION}.msi
	}

	if ($GSTREAMER_INSTALL_MODE -eq "fresh") {
		if (Test-Path $GSTREAMER_DEST_FOLDER) {
			Write-Host "Removing existing GStreamer directory remnants before installation..."
			Remove-Item -LiteralPath $GSTREAMER_DEST_FOLDER -Recurse -Force
		}

		Write-Host "Installing GStreamer runtime package..."
		Start-Process -Wait -FilePath "msiexec" -ArgumentList "/passive", "INSTALLDIR=C:\gstreamer", "/i", $GSTREAMER_RUNTIME_INSTALLER, "/qn"
		Write-Host "Installing GStreamer development package..."
		Start-Process -Wait -FilePath "msiexec" -ArgumentList "/passive", "INSTALLDIR=C:\gstreamer", "/i", $GSTREAMER_DEVEL_INSTALLER, "/qn"
		(Get-Content C:\gstreamer\1.0\msvc_x86_64\lib\pkgconfig\gstreamer-analytics-1.0.pc).Replace('-lm', '') | Set-Content C:\gstreamer\1.0\msvc_x86_64\lib\pkgconfig\gstreamer-analytics-1.0.pc
		Write-Host "################################################# GStreamer installation completed ###################################################"
	} elseif ($GSTREAMER_INSTALL_MODE -eq "reinstall") {
		Write-Host "#############################################################################################"
		Write-Host "Detected existing GStreamer installation that doesn't match the required version ${GSTREAMER_VERSION}."
		Write-Host "Automatic installation is paused until the current GStreamer is removed."
		Write-Host "Please uninstall the existing GStreamer using Control Panel before proceeding."
		Write-Host "The required installers have been downloaded for you and will be reused on the next run."
		Write-Host " "
		Write-Host "After uninstalling, ensure the old GStreamer installation folder ${GSTREAMER_DEST_FOLDER} is fully removed."
		Write-Host "Then rerun this script. The new version will install automatically."
		Write-Host "#############################################################################################"
		exit 1
	} else {
		Write-Warning "Internal state error: unknown GStreamer install mode '$GSTREAMER_INSTALL_MODE'."
		exit 1
	}
} else {
	Write-Host "################################# GStreamer ${GSTREAMER_VERSION} already installed ##################################"
}

# Check if OpenVINO is installed and if it's the correct version
$OPENVINO_NEEDS_INSTALL = $true
if (-Not [System.IO.File]::Exists("$OPENVINO_DEST_FOLDER\\setupvars.ps1")) {
	Write-Host "OpenVINO not found - installation needed"
	$OPENVINO_NEEDS_INSTALL = $true
} else {
	Write-Host "OpenVINO found in folder $OPENVINO_DEST_FOLDER"

	# Try to get installed version from version file
	$VERSION_FILE = "$OPENVINO_DEST_FOLDER\\runtime\\version.txt"
	if (Test-Path $VERSION_FILE) {
		$VERSION_CONTENT = Get-Content $VERSION_FILE -First 1
		if ($VERSION_CONTENT) {
			if ($VERSION_CONTENT.StartsWith($OPENVINO_VERSION)) {
				$INSTALLED_VERSION_FULL = ($VERSION_CONTENT -split '-')[0]
				Write-Host "OpenVINO version $INSTALLED_VERSION_FULL verified - compatible with required $OPENVINO_VERSION"
				$OPENVINO_NEEDS_INSTALL = $false
			} else {
				$INSTALLED_VERSION_FULL = ($VERSION_CONTENT -split '-')[0]
				Write-Host "OpenVINO version mismatch - installed: $INSTALLED_VERSION_FULL, required: $OPENVINO_VERSION"
				$OPENVINO_NEEDS_INSTALL = $true
			}
		} else {
			Write-Host "Warning: Could not read OpenVINO version file"
			$OPENVINO_NEEDS_INSTALL = $false
		}
	} else {
		Write-Host "Warning: Could not find OpenVINO version file, but installation appears complete"
		$OPENVINO_NEEDS_INSTALL = $false
	}
}
if ($OPENVINO_NEEDS_INSTALL) {
	Write-Host "####################################### Installing OpenVINO GenAI ${OPENVINO_VERSION} #######################################"

	# Remove existing OpenVINO installation if present
	if (Test-Path "${OPENVINO_DEST_FOLDER}") {
		Write-Host "Removing existing OpenVINO installation..."
		Remove-Item -LiteralPath "${OPENVINO_DEST_FOLDER}" -Recurse -Force
	}

	# Check if correct installer is already downloaded
	$OPENVINO_INSTALLER = "${DLSTREAMER_TMP}\\openvino_genai_windows_${OPENVINO_VERSION}.0.0_x86_64.zip"
	if (-Not (Test-Path $OPENVINO_INSTALLER)) {
		Write-Host "Downloading OpenVINO GenAI ${OPENVINO_VERSION}..."
		Invoke-WebRequest -OutFile $OPENVINO_INSTALLER -Uri "https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/${OPENVINO_VERSION}/windows/openvino_genai_windows_${OPENVINO_VERSION}.0.0_x86_64.zip"
	} else {
		Write-Host "Using existing OpenVINO installer: $OPENVINO_INSTALLER"
	}

	Write-Host "Extracting OpenVINO GenAI ${OPENVINO_VERSION}..."
	Expand-Archive -Path $OPENVINO_INSTALLER -DestinationPath "C:\" -Force
	$EXTRACTED_FOLDER = "C:\\openvino_genai_windows_${OPENVINO_VERSION}.0.0_x86_64"
	if (Test-Path $EXTRACTED_FOLDER) {
		# Rename the extracted folder to the final destination name
		$DEST_FOLDER_NAME = Split-Path $OPENVINO_DEST_FOLDER -Leaf
		Rename-Item -Path $EXTRACTED_FOLDER -NewName $DEST_FOLDER_NAME
	}
	Write-Host "############################################ Done ########################################################"
} else {
	Write-Host "################################# OpenVINO GenAI ${OPENVINO_VERSION} already correctly installed ##################################"
}

Write-Host 'Setting variables: GST_PLUGIN_PATH, Path (for DLLs)'
$CURRENT_DIR = (Get-Item .).FullName
[Environment]::SetEnvironmentVariable('GST_PLUGIN_PATH', "C:\gstreamer\1.0\msvc_x86_64\bin;C:\gstreamer\1.0\msvc_x86_64\lib\gstreamer-1.0;$CURRENT_DIR", [System.EnvironmentVariableTarget]::User)
$USER_PATH = [Environment]::GetEnvironmentVariable('Path', 'User')
$pathEntries = $USER_PATH -split ';'
if (-Not ($pathEntries -contains $CURRENT_DIR)) {
    [Environment]::SetEnvironmentVariable('Path', ($USER_PATH + ';' + $CURRENT_DIR), [System.EnvironmentVariableTarget]::User)
	Write-Host 'Added current directory to User Path variable'
}

Write-Host 'Setting variables: GST_PLUGIN_SCANNER'
$GSTREAMER_PLUGIN_SCANNER_PATH = (Get-ChildItem -Filter gst-plugin-scanner.exe -Recurse -Path 'C:\gstreamer' -Include 'gst-plugin-scanner.exe' -ErrorAction SilentlyContinue) | Select-Object -First 1
[Environment]::SetEnvironmentVariable('GST_PLUGIN_SCANNER', $GSTREAMER_PLUGIN_SCANNER_PATH, [System.EnvironmentVariableTarget]::User)

Write-Host 'Setting variables: Path (for gst-launch-1.0)'
$GSTREAMER_BIN_DIR = "$GSTREAMER_DEST_FOLDER\1.0\msvc_x86_64\bin"
$USER_PATH = [Environment]::GetEnvironmentVariable('Path', 'User')
$pathEntries = $USER_PATH -split ';'
if (-Not ($pathEntries -contains $GSTREAMER_BIN_DIR)) {
	[Environment]::SetEnvironmentVariable('Path', $USER_PATH + ';' + $GSTREAMER_BIN_DIR, [System.EnvironmentVariableTarget]::User)
	Write-Host 'Added gst-launch-1.0 directory to User Path variable'
}

Write-Host 'Setting variables:, OpenVINO_DIR, OPENVINO_LIB_PATHS, Path (for OpenVINO)'
[Environment]::SetEnvironmentVariable('OpenVINO_DIR', "$OPENVINO_DEST_FOLDER\runtime\cmake", [System.EnvironmentVariableTarget]::User)
[Environment]::SetEnvironmentVariable('OpenVINOGenAI_DIR', "$OPENVINO_DEST_FOLDER\runtime\cmake", [System.EnvironmentVariableTarget]::User)
[Environment]::SetEnvironmentVariable('OPENVINO_LIB_PATHS', "$OPENVINO_DEST_FOLDER\runtime\3rdparty\tbb\bin;$OPENVINO_DEST_FOLDER\runtime\bin\intel64\Release", [System.EnvironmentVariableTarget]::User)
$OPENVINO_TBB_DIR = "$OPENVINO_DEST_FOLDER\runtime\3rdparty\tbb\bin"
$OPENVINO_BIN_DIR = "$OPENVINO_DEST_FOLDER\runtime\bin\intel64\Release"
$USER_PATH = [Environment]::GetEnvironmentVariable('Path', 'User')
$pathEntries = $USER_PATH -split ';'
if (-Not (($pathEntries -contains $OPENVINO_TBB_DIR) -and ($pathEntries -contains $OPENVINO_BIN_DIR))) {
	[Environment]::SetEnvironmentVariable('Path', $USER_PATH + ";$OPENVINO_TBB_DIR;$OPENVINO_BIN_DIR", [System.EnvironmentVariableTarget]::User)
	Write-Host 'Added OpenVINO directories to User Path variable'
}

$env:Path = [System.Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [System.Environment]::GetEnvironmentVariable('Path','User')
$env:GST_PLUGIN_PATH = [System.Environment]::GetEnvironmentVariable('GST_PLUGIN_PATH','User')
$env:GST_PLUGIN_SCANNER = [System.Environment]::GetEnvironmentVariable('GST_PLUGIN_SCANNER','User')
$env:OpenVINO_DIR = [System.Environment]::GetEnvironmentVariable('OpenVINO_DIR','User')
$env:OPENVINO_LIB_PATHS = [System.Environment]::GetEnvironmentVariable('OPENVINO_LIB_PATHS','User')

Write-Host "USER ENVIRONMENT VARIABLES"
Write-Host "Path:"
$env:Path
Write-Host "GST_PLUGIN_PATH:"
$env:GST_PLUGIN_PATH
Write-Host "GST_PLUGIN_SCANNER:"
$env:GST_PLUGIN_SCANNER
Write-Host "OpenVINO_DIR:"
$env:OpenVINO_DIR
Write-Host "OPENVINO_LIB_PATHS:"
$env:OPENVINO_LIB_PATHS

try {
	if (Test-Path "$env:LOCALAPPDATA\Microsoft\Windows\INetCache\gstreamer-1.0\registry.x86_64-msvc.bin") {
		Write-Host "Clearing existing GStreamer cache"
		del "$env:LOCALAPPDATA\Microsoft\Windows\INetCache\gstreamer-1.0\registry.x86_64-msvc.bin"
		Write-Host ""
		Write-Host "Generating GStreamer cache. It may take up to a few minutes for the first run"
		Write-Host "Please wait for a moment... "
	}
	$(gst-inspect-1.0.exe gvadetect)
} catch {
	Write-Host "Error: Failed to inspect gvadetect element."
    Write-Host "Error details: $_"
    Write-Host "Please try updating GPU/NPU drivers and rebooting the system."
}

Write-Host "DLStreamer is ready"
