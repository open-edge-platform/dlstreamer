#Requires -RunAsAdministrator
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
param(
	[switch]$useInternalProxy
)

$GSTREAMER_VERSION = "1.26.11"
$OPENVINO_VERSION = "2026.0.0"
$OPENVINO_VERSION_SHORT = "2026.0"
$PYTHON_VERSION = "3.12.7"
$OPENVINO_DEST_FOLDER = "$env:LOCALAPPDATA\Programs\openvino"
$GSTREAMER_DEST_FOLDER = "$env:ProgramFiles\gstreamer\1.0\msvc_x86_64"
$DLSTREAMER_TMP = "$env:TEMP\dlstreamer_tmp"

if ($useInternalProxy) {
	$env:HTTP_PROXY = "http://proxy-dmz.intel.com:911"
	$env:HTTPS_PROXY = "http://proxy-dmz.intel.com:912"
	$env:NO_PROXY = ""
	Write-Host "Proxy set:"
	Write-Host "- HTTP_PROXY = $env:HTTP_PROXY"
	Write-Host "- HTTPS_PROXY = $env:HTTPS_PROXY"
	Write-Host "- NO_PROXY = $env:NO_PROXY"
}
else {
	Write-Host "No proxy set"
}

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

function Uninstall-GStreamer {
	# Check if this is an Inno installation (GStreamer 1.28+)
	$innoUninstallKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\c20a66dc-b249-4e6d-a68a-d0f836b2b3cf_is1"
	$quietUninstall = (Get-ItemProperty -Path $innoUninstallKey -Name "QuietUninstallString" -ErrorAction SilentlyContinue).QuietUninstallString

	if ($quietUninstall) {
		Write-Host "Uninstalling GStreamer (Inno)..."
		if ($quietUninstall -match '^"([^"]+)"\s*(.*)$') {
			$process = Start-Process -Wait -PassThru -FilePath $Matches[1] -ArgumentList $Matches[2]
			if ($process.ExitCode -ne 0) {
				Write-Host "GStreamer uninstall returned exit code: $($process.ExitCode)"
			}
		}
		else {
			Write-Host "Could not parse QuietUninstallString: $quietUninstall"
		}
		# Workaround: GStreamer 1.28.1 uninstaller does not remove registry entries
		if (Test-Path "HKLM:\SOFTWARE\GStreamer1.0\x86_64") {
			Remove-Item -Path "HKLM:\SOFTWARE\GStreamer1.0\x86_64" -Recurse -Force
			Write-Host "Removed GStreamer registry entries"
		}
	}
	else {
		Write-Host "Uninstalling GStreamer (MSI)..."
		try {
			$installer = New-Object -ComObject "WindowsInstaller.Installer"
			foreach ($upgradeCode in @("{c20a66dc-b249-4e6d-a68a-d0f836b2b3cf}", "{49c4a3aa-249f-453c-b82e-ecd05fac0693}")) {
				$products = $installer.RelatedProducts($upgradeCode)
				if ($products) {
					foreach ($productCode in $products) {
						$result = Start-Process -Wait -PassThru -FilePath "msiexec" -ArgumentList "/x", $productCode, "/qn", "/norestart"
						if ($result.ExitCode -ne 0 -and $result.ExitCode -ne 1605) {
							Write-Host "MSI uninstall returned exit code: $($result.ExitCode)"
						}
					}
				}
			}
			[System.Runtime.Interopservices.Marshal]::ReleaseComObject($installer) | Out-Null
		}
		catch {
			Write-Host "Error during MSI uninstall: $_"
		}
	}
}

# ============================================================================
# WinGet
# ============================================================================
if (-Not (Get-Command winget -errorAction SilentlyContinue)) {
	$progressPreference = 'silentlyContinue'
	Write-Section "Installing WinGet PowerShell module from PSGallery"
	Install-PackageProvider -Name NuGet -Force | Out-Null
	Install-Module -Name Microsoft.WinGet.Client -Force -Repository PSGallery | Out-Null
	Write-Host "Using Repair-WinGetPackageManager cmdlet to bootstrap WinGet..."
	Repair-WinGetPackageManager -AllUsers
	winget source update
	Write-Section "Done"
}
else {
	winget source update
	Write-Section "WinGet already installed"
}

# ============================================================================
# VS BuildTools, vcpkg and Windows SDK
# ============================================================================
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsInstaller = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vs_installer.exe"
$vsInstalled = $false
$vsPath = ""
if (Test-Path $vswhere) {
	# Check if all required components are installed
	$vsPath = & $vswhere -latest -products * -version "[18.0,)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.ComponentGroup.NativeDesktop.Core Microsoft.VisualStudio.Component.Vcpkg Microsoft.VisualStudio.Component.Windows11SDK.26100 -property installationPath
	if ($vsPath) {
		$vsInstalled = $true
	}
}

if (-Not $vsInstalled) {
	Write-Section "Installing VS BuildTools with vcpkg and Windows SDK"
	Invoke-DownloadFile -OutFile "$DLSTREAMER_TMP\vs_buildtools.exe" -Uri "https://aka.ms/vs/stable/vs_buildtools.exe"
	$process = Start-Process -Wait -PassThru -FilePath "$DLSTREAMER_TMP\vs_buildtools.exe" -ArgumentList "--quiet", "--wait", "--norestart", "--add", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "--add", "Microsoft.VisualStudio.ComponentGroup.NativeDesktop.Core", "--add", "Microsoft.VisualStudio.Component.Vcpkg", "--add", "Microsoft.VisualStudio.Component.Windows11SDK.26100"
	# VS returns 3010 when installation is successful but requires restart, treat it as success
	if ($process.ExitCode -ne 0 -and $process.ExitCode -ne 3010) {
		Write-Error "VS BuildTools installation failed with exit code: $($process.ExitCode)"
	}
	Update-Path
	$vsPath = & $vswhere -latest -products * -version "[18.0,)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.ComponentGroup.NativeDesktop.Core Microsoft.VisualStudio.Component.Vcpkg Microsoft.VisualStudio.Component.Windows11SDK.26100 -property installationPath
}
else {
	Write-Section "Updating VS BuildTools"
	$process = Start-Process -Wait -PassThru -FilePath $vsInstaller -ArgumentList "update", "--installPath", "`"$vsPath`"", "--quiet", "--norestart"
	if ($process.ExitCode -ne 0 -and $process.ExitCode -ne 3010) {
		Write-Error "VS BuildTools update returned exit code: $($process.ExitCode)"
	}
}
Write-Section "Done"

# ============================================================================
# GStreamer
# ============================================================================
$GSTREAMER_NEEDS_INSTALL = $false

try {
	$regPath = "HKLM:\SOFTWARE\GStreamer1.0\x86_64"
	$regInstallDir = (Get-ItemProperty -Path $regPath -Name "InstallDir" -ErrorAction SilentlyContinue).InstallDir
	$regVersion = (Get-ItemProperty -Path $regPath -Name "Version" -ErrorAction SilentlyContinue).Version

	if ($regInstallDir -and $regVersion) {
		Write-Host "GStreamer found in registry - InstallDir: $regInstallDir, Version: $regVersion"
		$GSTREAMER_DEST_FOLDER = $regInstallDir.TrimEnd('\')
		# Backward compatibility with 1.26
		if (-Not $GSTREAMER_DEST_FOLDER.EndsWith('\1.0\msvc_x86_64')) {
			$GSTREAMER_DEST_FOLDER = "$GSTREAMER_DEST_FOLDER\1.0\msvc_x86_64"
		}
		# Check for conflicting architectures
		$envMsvcX64 = [Environment]::GetEnvironmentVariable('GSTREAMER_1_0_ROOT_MSVC_X86_64', 'Machine')

		if ($envMsvcX64 -and ($envMsvcX64.TrimEnd('\') -ne $GSTREAMER_DEST_FOLDER)) {
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

		if ($regVersion -ne $GSTREAMER_VERSION) {
			Write-Host "GStreamer version mismatch - installed: $regVersion, required: $GSTREAMER_VERSION"
			Uninstall-GStreamer
			$GSTREAMER_NEEDS_INSTALL = $true
			$GSTREAMER_DEST_FOLDER = "$env:ProgramFiles\gstreamer\1.0\msvc_x86_64"
		}
		else {
			# Verify installation directory structure exists
			if (-Not (Test-Path $GSTREAMER_DEST_FOLDER)) {
				Write-Host "GStreamer installation incomplete - $GSTREAMER_DEST_FOLDER not found - reinstallation needed"
				$GSTREAMER_NEEDS_INSTALL = $true
			}
			else {
				Write-Host "GStreamer version $regVersion verified (matches required $GSTREAMER_VERSION)"
				$GSTREAMER_NEEDS_INSTALL = $false
			}
		}
	}
	else {
		Write-Host "GStreamer not found in registry - installation needed"
		$GSTREAMER_NEEDS_INSTALL = $true
		$GSTREAMER_DEST_FOLDER = "$env:ProgramFiles\gstreamer\1.0\msvc_x86_64"
	}
}
catch {
	Write-Host "GStreamer registry check failed - assuming not installed"
	$GSTREAMER_NEEDS_INSTALL = $true
	$GSTREAMER_DEST_FOLDER = "$env:ProgramFiles\gstreamer\1.0\msvc_x86_64"
}

if ($GSTREAMER_NEEDS_INSTALL) {
	# Determine installer type based on target version
	$vParts = $GSTREAMER_VERSION.Split('.') | ForEach-Object { [int]$_ }
	$useExeInstaller = ($vParts[0] -gt 1) -or ($vParts[0] -eq 1 -and $vParts[1] -ge 28)

	if ($useExeInstaller) {
		Write-Section "Installing GStreamer ${GSTREAMER_VERSION} (Inno)"
		$GSTREAMER_INSTALLER = "${DLSTREAMER_TMP}\gstreamer-1.0-msvc-x86_64-${GSTREAMER_VERSION}.exe"
		Write-Host "Downloading GStreamer installer..."
		Invoke-DownloadFile -UserAgent "curl/8.5.0" -OutFile $GSTREAMER_INSTALLER -Uri "https://gstreamer.freedesktop.org/data/pkg/windows/${GSTREAMER_VERSION}/msvc/gstreamer-1.0-msvc-x86_64-${GSTREAMER_VERSION}.exe"

		Write-Host "Installing GStreamer..."
		$process = Start-Process -Wait -PassThru -FilePath $GSTREAMER_INSTALLER -ArgumentList "/SILENT", "/LOG", "/TYPE=full", "/ALLUSERS"
		if ($process.ExitCode -ne 0) {
			Write-Error "GStreamer installation failed with exit code: $($process.ExitCode)"
		}

		# Workaround: GStreamer 1.28.1 writes GSTREAMER_1_0_ROOT_MSVC_X86_64 to wrong registry location, fixed in 1.28.2 https://gitlab.freedesktop.org/gstreamer/cerbero/-/issues/574
		$wrongRegKey = "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager"
		$rightRegKey = "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
		$envVarName = "GSTREAMER_1_0_ROOT_MSVC_X86_64"
		$wrongValue = (Get-ItemProperty -Path $wrongRegKey -Name $envVarName -ErrorAction SilentlyContinue).$envVarName
		if ($wrongValue) {
			Write-Host "Fixing $envVarName registry location..."
			Set-ItemProperty -Path $rightRegKey -Name $envVarName -Value $wrongValue
			Remove-ItemProperty -Path $wrongRegKey -Name $envVarName
		}
	}
	else {
		Write-Section "Installing GStreamer ${GSTREAMER_VERSION} (MSI)"
		$GSTREAMER_RUNTIME_INSTALLER = "${DLSTREAMER_TMP}\gstreamer-1.0-msvc-x86_64-${GSTREAMER_VERSION}.msi"
		$GSTREAMER_DEVEL_INSTALLER = "${DLSTREAMER_TMP}\gstreamer-1.0-devel-msvc-x86_64-${GSTREAMER_VERSION}.msi"

		Write-Host "Downloading GStreamer runtime installer..."
		Invoke-DownloadFile -UserAgent "curl/8.5.0" -OutFile $GSTREAMER_RUNTIME_INSTALLER -Uri "https://gstreamer.freedesktop.org/data/pkg/windows/${GSTREAMER_VERSION}/msvc/gstreamer-1.0-msvc-x86_64-${GSTREAMER_VERSION}.msi"

		Write-Host "Downloading GStreamer development installer..."
		Invoke-DownloadFile -UserAgent "curl/8.5.0" -OutFile $GSTREAMER_DEVEL_INSTALLER -Uri "https://gstreamer.freedesktop.org/data/pkg/windows/${GSTREAMER_VERSION}/msvc/gstreamer-1.0-devel-msvc-x86_64-${GSTREAMER_VERSION}.msi"

		Write-Host "Installing GStreamer runtime package..."
		$process = Start-Process -Wait -PassThru -FilePath "msiexec" -ArgumentList "/passive", "/i", $GSTREAMER_RUNTIME_INSTALLER, "/qn", "ADDLOCAL=ALL"
		if ($process.ExitCode -ne 0) {
			Write-Error "GStreamer runtime installation failed with exit code: $($process.ExitCode)"
		}
		Write-Host "Installing GStreamer development package..."
		$process = Start-Process -Wait -PassThru -FilePath "msiexec" -ArgumentList "/passive", "/i", $GSTREAMER_DEVEL_INSTALLER, "/qn", "ADDLOCAL=ALL"
		if ($process.ExitCode -ne 0) {
			Write-Error "GStreamer development installation failed with exit code: $($process.ExitCode)"
		}
		# FIXME: Remove this section after GStreamer 1.28
		$pkgConfigFile = "$GSTREAMER_DEST_FOLDER\lib\pkgconfig\gstreamer-analytics-1.0.pc"
		if (Test-Path $pkgConfigFile) {
			(Get-Content $pkgConfigFile).Replace('-lm', '') | Set-Content $pkgConfigFile
		}
	}

	# Re-read registry to get actual install location
	$regInstallDir = (Get-ItemProperty -Path "HKLM:\SOFTWARE\GStreamer1.0\x86_64" -Name "InstallDir" -ErrorAction SilentlyContinue).InstallDir
	if ($regInstallDir) {
		$GSTREAMER_DEST_FOLDER = $regInstallDir.TrimEnd('\')
		# Backward compatibility with 1.26
		if (-Not $GSTREAMER_DEST_FOLDER.EndsWith('\1.0\msvc_x86_64')) {
			$GSTREAMER_DEST_FOLDER = "$GSTREAMER_DEST_FOLDER\1.0\msvc_x86_64"
		}
	}
	Write-Section "GStreamer installation completed"
}
else {
	Write-Section "GStreamer ${GSTREAMER_VERSION} already installed"
}

# ============================================================================
# OpenVINO
# ============================================================================
$OPENVINO_NEEDS_INSTALL = $true
if (-Not (Test-Path "$OPENVINO_DEST_FOLDER\setupvars.ps1")) {
	Write-Host "OpenVINO not found - installation needed"
	$OPENVINO_NEEDS_INSTALL = $true
}
else {
	Write-Host "OpenVINO found in folder $OPENVINO_DEST_FOLDER"

	# Try to get installed version from version file
	$VERSION_FILE = "$OPENVINO_DEST_FOLDER\runtime\version.txt"
	if (Test-Path $VERSION_FILE) {
		$VERSION_CONTENT = Get-Content $VERSION_FILE -First 1
		if ($VERSION_CONTENT) {
			if ($VERSION_CONTENT.StartsWith($OPENVINO_VERSION)) {
				$INSTALLED_VERSION_FULL = ($VERSION_CONTENT -split '-')[0]
				Write-Host "OpenVINO version $INSTALLED_VERSION_FULL verified - compatible with required $OPENVINO_VERSION"
				$OPENVINO_NEEDS_INSTALL = $false
			}
			else {
				$INSTALLED_VERSION_FULL = ($VERSION_CONTENT -split '-')[0]
				Write-Host "OpenVINO version mismatch - installed: $INSTALLED_VERSION_FULL, required: $OPENVINO_VERSION"
				$OPENVINO_NEEDS_INSTALL = $true
			}
		}
		else {
			$OPENVINO_NEEDS_INSTALL = $true
		}
	}
	else {
		$OPENVINO_NEEDS_INSTALL = $true
	}
}

if ($OPENVINO_NEEDS_INSTALL) {
	Write-Section "Installing OpenVINO GenAI ${OPENVINO_VERSION}"

	# Remove existing OpenVINO installation if present
	if (Test-Path "${OPENVINO_DEST_FOLDER}") {
		Write-Host "Removing existing OpenVINO installation..."
		Remove-Item -LiteralPath "${OPENVINO_DEST_FOLDER}" -Recurse -Force
	}

	# Check if correct installer is already downloaded
	$OPENVINO_INSTALLER = "${DLSTREAMER_TMP}\openvino_genai_windows_${OPENVINO_VERSION}.0_x86_64.zip"
	Write-Host "Downloading OpenVINO GenAI ${OPENVINO_VERSION}..."
	Invoke-DownloadFile -OutFile $OPENVINO_INSTALLER -Uri "https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/${OPENVINO_VERSION_SHORT}/windows/openvino_genai_windows_${OPENVINO_VERSION}.0_x86_64.zip"

	Write-Host "Extracting OpenVINO GenAI ${OPENVINO_VERSION}..."
	$EXTRACTED_FOLDER = "$env:TEMP\openvino_genai_windows_${OPENVINO_VERSION}.0_x86_64"
	if (Test-Path $EXTRACTED_FOLDER) {
		Remove-Item -LiteralPath $EXTRACTED_FOLDER -Recurse -Force
	}
	Expand-Archive -Path $OPENVINO_INSTALLER -DestinationPath "$env:TEMP" -Force
	if (Test-Path $EXTRACTED_FOLDER) {
		$OPENVINO_PARENT = Split-Path $OPENVINO_DEST_FOLDER -Parent
		if (-Not (Test-Path $OPENVINO_PARENT)) {
			New-Item -ItemType Directory -Path $OPENVINO_PARENT -Force | Out-Null
		}
		Move-Item -Path $EXTRACTED_FOLDER -Destination $OPENVINO_DEST_FOLDER -Force
	}
	Write-Section "Done"
}
else {
	Write-Section "OpenVINO GenAI ${OPENVINO_VERSION} already installed"
}

# ============================================================================
# Git
# ============================================================================
$gitInstalled = (Get-WinGetPackage -Id Git.Git -Source winget -ErrorAction SilentlyContinue).Count -gt 0
if (-Not $gitInstalled) {
	Write-Section "Installing Git"
	Install-WinGetPackage -Id Git.Git -Source winget -Mode Silent
	Update-Path
	New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name "LongPathsEnabled" -Value 1 -PropertyType DWORD -Force
	git config --system core.longpaths true
	Write-Section "Done"
}
else {
	git --version
	Write-Section "Git already installed"
}

# ============================================================================
# CMake
# ============================================================================
$cmakeInstalled = (Get-WinGetPackage -Id Kitware.CMake -Source winget -ErrorAction SilentlyContinue).Count -gt 0
if (-Not $cmakeInstalled) {
	Write-Section "Installing CMake"
	Install-WinGetPackage -Id Kitware.CMake -Source winget -Mode Silent
	Update-Path
	Write-Section "Done"
}
else {
	cmake --version
	Write-Section "CMake already installed"
}

# ============================================================================
# Upgrade with winget
# ============================================================================
winget upgrade Git.Git Kitware.CMake -e --source winget

# ============================================================================
# Python
# ============================================================================
if (-Not (Get-Command python -errorAction SilentlyContinue)) {
	Write-Section "Installing Python"
	Invoke-DownloadFile -OutFile "${DLSTREAMER_TMP}\python-${PYTHON_VERSION}-amd64.exe" -Uri "https://www.python.org/ftp/python/${PYTHON_VERSION}/python-${PYTHON_VERSION}-amd64.exe"
	$process = Start-Process -Wait -PassThru -FilePath "${DLSTREAMER_TMP}\python-${PYTHON_VERSION}-amd64.exe"  -ArgumentList "/quiet", "InstallAllUsers=1", "PrependPath=1", "Include_test=0"
	if ($process.ExitCode -ne 0) {
		Write-Error "Python installation failed with exit code: $($process.ExitCode)"
	}
	Update-Path
	Write-Section "Done"
}
else {
	python --version
	Write-Section "Python already installed"
}

# ============================================================================
# Final environment setup
# ============================================================================
Write-Section "Setting paths"
# Ensure GStreamer bin is in user PATH for GStreamer 1.28+
$GSTREAMER_BIN = "$GSTREAMER_DEST_FOLDER\bin"
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$vParts = $GSTREAMER_VERSION.Split('.') | ForEach-Object { [int]$_ }
if ($vParts[0] -eq 1 -and $vParts[1] -ge 28) {
	if ($userPath -split ';' -notcontains $GSTREAMER_BIN) {
		[Environment]::SetEnvironmentVariable('Path', "$userPath;$GSTREAMER_BIN", [System.EnvironmentVariableTarget]::User)
		Write-Host "Added to user PATH: $GSTREAMER_BIN"
	}
}
else {
	$pathEntries = $userPath -split ';' | Where-Object { $_ -ne $GSTREAMER_BIN }
	$newPath = $pathEntries -join ';'
	if ($newPath -ne $userPath) {
		[Environment]::SetEnvironmentVariable('Path', $newPath, [System.EnvironmentVariableTarget]::User)
		Write-Host "Removed from user PATH: $GSTREAMER_BIN"
	}
}

Update-Path
$DLSTREAMER_SRC_LOCATION = $PWD.Path
setx PKG_CONFIG_PATH "$GSTREAMER_DEST_FOLDER\lib\pkgconfig"
$env:PKG_CONFIG_PATH = "$GSTREAMER_DEST_FOLDER\lib\pkgconfig"
# Setup OpenVINO environment variables
. "$OPENVINO_DEST_FOLDER\setupvars.ps1"
# Setup VS environment variables
$env:MSBUILDDISABLENODEREUSE = 1
$env:UseMultiToolTask = "true"
$VSDEVSHELL = Join-Path $vsPath "Common7\Tools\Launch-VsDevShell.ps1"
& $VSDEVSHELL -Arch amd64
Write-Section "Done"

# ============================================================================
# Build DL Streamer
# ============================================================================
Write-Section "Preparing build directory"
if (Test-Path "${DLSTREAMER_TMP}\build") {
	Remove-Item -LiteralPath "${DLSTREAMER_TMP}\build" -Recurse
}
mkdir "${DLSTREAMER_TMP}\build"
Set-Location -Path "${DLSTREAMER_TMP}\build"

Write-Section "Running CMake"
$VCPKG_CMAKE = Join-Path $vsPath "VC\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake -DCMAKE_TOOLCHAIN_FILE="${VCPKG_CMAKE}" "$DLSTREAMER_SRC_LOCATION"
if ($LASTEXITCODE -eq 0) {
	Write-Section "Building DL Streamer"
	cmake --build . --parallel $env:NUMBER_OF_PROCESSORS --target ALL_BUILD --config Release
	Write-Section "Done"
}
else {
	Write-Section "!CMake error!"
	exit $LASTEXITCODE
}
