#Requires -RunAsAdministrator
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# DL Streamer Environment Removal Script
# This script removes environment variables and optionally removes software
# Usage:
#   .\remove_dls_env.ps1                          # Remove env vars only
#   .\remove_dls_env.ps1 -RemoveSoftware          # Remove env vars and software

param(
	[switch]$RemoveSoftware
)

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

function Remove-PathEntry {
	param(
		[string]$PathToRemove
	)

	$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
	$pathEntries = $userPath -split ';' | Where-Object { $_ -and ($_ -ne $PathToRemove) }
	$newPath = $pathEntries -join ';'

	if ($newPath -ne $userPath) {
		[Environment]::SetEnvironmentVariable('Path', $newPath, [System.EnvironmentVariableTarget]::User)
		Write-Host "  Removed from PATH: $PathToRemove"
	}
}

function Uninstall-GStreamerPackage {
	param(
		[System.__ComObject]$Installer,
		[string]$UpgradeCode,
		[string]$PackageName
	)

	Write-Host "  Uninstalling GStreamer $PackageName..."
	try {
		$products = $Installer.RelatedProducts($UpgradeCode)
		if ($products) {
			foreach ($productCode in $products) {
				$process = Start-Process -Wait -PassThru -FilePath "msiexec" -ArgumentList "/x", $productCode, "/qn", "/norestart"
				if ($process.ExitCode -eq 0 -or $process.ExitCode -eq 1605) {
					Write-Host "  GStreamer $PackageName uninstalled"
				}
				else {
					Write-Host "  GStreamer $PackageName uninstall returned exit code: $($process.ExitCode)"
				}
			}
		}
	}
	catch {
		Write-Host "  Could not query GStreamer ${PackageName}: $_"
	}
}

Write-Section "DL Streamer Environment Removal"
# Get current directory
$CURRENT_DIR = (Get-Item .).FullName

# Detect GStreamer installation location from registry
$GSTREAMER_BIN_DIR = $null
try {
	$regPath = "HKLM:\SOFTWARE\GStreamer1.0\x86_64"
	$regInstallDir = (Get-ItemProperty -Path $regPath -Name "InstallDir" -ErrorAction SilentlyContinue).InstallDir
	if ($regInstallDir) {
		$GSTREAMER_BIN_DIR = "$($regInstallDir.TrimEnd('\'))\1.0\msvc_x86_64\bin"
	}
}
catch {
}

# Detect LIBVA drivers path from environment variable
$LIBVA_DRIVERS_PATH = [Environment]::GetEnvironmentVariable('LIBVA_DRIVERS_PATH', 'User')

# Detect VideoAccelerationCompatibilityPack paths from PATH
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$VideoAccelPaths = @()
if ($userPath) {
	$VideoAccelPaths = $userPath -split ';' | Where-Object { $_ -like "*VideoAccelerationCompatibilityPack*" }
}

# ============================================================================
# Remove Environment Variables
# ============================================================================

Write-Host "Removing User Environment Variables"

# Current setup_dls_env.ps1 variables
$varsToRemove = @(
	'GST_PLUGIN_PATH'
)

# Legacy variables
$legacyVarsToRemove = @(
	'LIBVA_DRIVER_NAME',
	'LIBVA_DRIVERS_PATH',
	'GST_PLUGIN_SCANNER',
	'OpenVINO_DIR',
	'OpenVINOGenAI_DIR',
	'OPENVINO_LIB_PATHS'
)

$allVars = $varsToRemove + $legacyVarsToRemove

foreach ($varName in $allVars) {
	$value = [Environment]::GetEnvironmentVariable($varName, 'User')
	if ($value) {
		[Environment]::SetEnvironmentVariable($varName, $null, [System.EnvironmentVariableTarget]::User)
		Write-Host "  Removed: $varName"
	}
}

Write-Host "Removing PATH Entries"

# Build list of paths to remove
$pathsToRemove = @(
	$CURRENT_DIR,
	"C:\gstreamer\1.0\msvc_x86_64\bin",
	"C:\openvino\runtime\3rdparty\tbb\bin",
	"C:\openvino\runtime\bin\intel64\Release"
)

if ($GSTREAMER_BIN_DIR) {
	$pathsToRemove += $GSTREAMER_BIN_DIR
}
if ($LIBVA_DRIVERS_PATH) {
	$pathsToRemove += $LIBVA_DRIVERS_PATH
}
if ($VideoAccelPaths) {
	$pathsToRemove += $VideoAccelPaths
}

# Remove duplicates
$pathsToRemove = $pathsToRemove | Select-Object -Unique

foreach ($path in $pathsToRemove) {
	Remove-PathEntry -PathToRemove $path
}

# ============================================================================
# Optionally Remove Installed Software
# ============================================================================

if ($RemoveSoftware) {
	Write-Section "Removing Software"

	# Remove GStreamer
	Write-Host "Checking for GStreamer installation..."
	try {
		$regPath = "HKLM:\SOFTWARE\GStreamer1.0\x86_64"
		$regInstallDir = (Get-ItemProperty -Path $regPath -Name "InstallDir" -ErrorAction SilentlyContinue).InstallDir
		$regVersion = (Get-ItemProperty -Path $regPath -Name "Version" -ErrorAction SilentlyContinue).Version
		
		if ($regInstallDir -and $regVersion) {
			Write-Host "  Found GStreamer $regVersion at $regInstallDir"
			
			# WiX upgrade codes for GStreamer packages (x86_64)
			$GSTREAMER_RUNTIME_UPGRADE_CODE = "{c20a66dc-b249-4e6d-a68a-d0f836b2b3cf}"
			$GSTREAMER_DEVEL_UPGRADE_CODE = "{49c4a3aa-249f-453c-b82e-ecd05fac0693}"
			
			# Use Windows Installer COM object to find related products
			$installer = New-Object -ComObject "WindowsInstaller.Installer"
			
			# Uninstall packages
			Uninstall-GStreamerPackage -Installer $installer -UpgradeCode $GSTREAMER_RUNTIME_UPGRADE_CODE -PackageName "runtime package"
			Uninstall-GStreamerPackage -Installer $installer -UpgradeCode $GSTREAMER_DEVEL_UPGRADE_CODE -PackageName "development package"
			
			# Release COM object
			[System.Runtime.Interopservices.Marshal]::ReleaseComObject($installer) | Out-Null
		}
		else {
			Write-Host "  GStreamer not found in registry"
		}
	}
	catch {
		Write-Host "  Error during GStreamer removal: $_"
	}
	
	# Remove OpenVINO installations
	Write-Host "Checking for OpenVINO installations..."

	$OPENVINO_INSTALL_FOLDER = "$env:LOCALAPPDATA\Programs\openvino"
	if (Test-Path $OPENVINO_INSTALL_FOLDER) {
		Write-Host "  Found OpenVINO at $OPENVINO_INSTALL_FOLDER, removing it..."
		Remove-Item -LiteralPath $OPENVINO_INSTALL_FOLDER -Recurse -Force -ErrorAction SilentlyContinue
	}

	if (Test-Path "C:\openvino") {
		Write-Host "  Found OpenVINO at C:\openvino, removing it..."
		Remove-Item -LiteralPath "C:\openvino" -Recurse -Force -ErrorAction SilentlyContinue
	}
}
else {
	Write-Host ""
	Write-Host "Software removal skipped. Use -RemoveSoftware flag to remove GStreamer and OpenVINO."
}

# ============================================================================
# Clean up temporary files
# ============================================================================

Write-Host "Cleaning up temporary files"

$tmpDirs = @(
	"$env:TEMP\dlstreamer_tmp",
	"C:\dlstreamer_tmp"
)

foreach ($tmpDir in $tmpDirs) {
	if (Test-Path $tmpDir) {
		Write-Host "  Removing $tmpDir..."
		Remove-Item -LiteralPath $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
	}
}

$gstCachePath = "$env:LOCALAPPDATA\Microsoft\Windows\INetCache\gstreamer-1.0"
if (Test-Path $gstCachePath) {
	Write-Host "  Removing GStreamer cache..."
	Remove-Item -LiteralPath $gstCachePath -Recurse -Force -ErrorAction SilentlyContinue
}

# ============================================================================
# Summary
# ============================================================================

Write-Section "Done"
Write-Host ""
Write-Host "Note: You may need to restart your terminal or system for all changes to take effect."
Write-Host ""
Write-Host "Usage examples:"
Write-Host "  .\remove_dls_env.ps1                          # Remove env vars only"
Write-Host "  .\remove_dls_env.ps1 -RemoveSoftware          # Remove env vars and software"
