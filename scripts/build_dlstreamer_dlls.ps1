#Requires -RunAsAdministrator
# ==============================================================================
# Copyright (C) 2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
param(
	[switch]$useInternalProxy
)

$GSTREAMER_VERSION = "1.26.6"
$OPENVINO_VERSION = "2025.4"
$OPENVINO_DEST_FOLDER = "C:\\openvino"
$GSTREAMER_DEST_FOLDER = "C:\\gstreamer"
$DLSTREAMER_TMP = "C:\\dlstreamer_tmp"

if ($useInternalProxy) {
	$env:HTTP_PROXY="http://proxy-dmz.intel.com:911"
	$env:HTTPS_PROXY="http://proxy-dmz.intel.com:912"
	$env:NO_PROXY=""
	Write-Host "Proxy set:"
	Write-Host "- HTTP_PROXY = $env:HTTP_PROXY"
	Write-Host "- HTTPS_PROXY = $env:HTTPS_PROXY"
	Write-Host "- NO_PROXY = $env:NO_PROXY"
} else {
	Write-Host "No proxy set"
}

if (-Not (Test-Path $DLSTREAMER_TMP)) {
	mkdir $DLSTREAMER_TMP
}

if (-Not (Get-Command winget -errorAction SilentlyContinue)) {
	$progressPreference = 'silentlyContinue'
	Write-Host "######################## Installing WinGet PowerShell module from PSGallery ###########################"
	Install-PackageProvider -Name NuGet -Force | Out-Null
	Install-Module -Name Microsoft.WinGet.Client -Force -Repository PSGallery | Out-Null
	Write-Host "Using Repair-WinGetPackageManager cmdlet to bootstrap WinGet..."
	Repair-WinGetPackageManager -AllUsers
	Write-Host "########################################### Done ######################################################"
} else {
	Write-Host "########################### WinGet PowerShell module already installed ################################"
}

if (-Not (Test-Path "C:\\BuildTools")) {
	Write-Host "###################################### Installing VS BuildTools #######################################"
	Invoke-WebRequest -OutFile $DLSTREAMER_TMP\\vs_buildtools.exe -Uri https://aka.ms/vs/17/release/vs_buildtools.exe
	Start-Process -Wait -FilePath $DLSTREAMER_TMP\vs_buildtools.exe -ArgumentList "--quiet", "--wait", "--norestart", "--nocache", "--installPath", "C:\\BuildTools", "--add", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "--add", "Microsoft.VisualStudio.ComponentGroup.NativeDesktop.Core"
	Write-Host "############################################### Done ##################################################"
} else {
	Write-Host "################################# VS BuildTools already installed #####################################"
}

if (-Not (Test-Path "${env:ProgramFiles(x86)}\\Windows Kits")) {
	Write-Host "####################################### Installing Windows SDK #######################################"
	winget install --source winget --exact --id Microsoft.WindowsSDK.10.0.26100
	Write-Host "########################################### Done #####################################################"
} else {
	Write-Host "################################ Windows SDK already installed #######################################"
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

if (-Not (Test-Path "C:\\Program Files\\Git")) {
	Write-Host "####################################### Installing Git #######################################"
	winget install --id Git.Git -e --source winget
	git config --system core.longpaths true
	Write-Host "######################################### Done ###############################################"
} else {
	Write-Host "############################### Git already installed ########################################"
}

Write-Host "###################################### Setting paths ########################################"
${env:VCPKG_ROOT} = "C:\\vcpkg"
if (-Not ${env:PATH_SETUP_DONE}) {
	${env:PATH} = "C:\Program Files\Git\bin;${env:PATH}"
	setx path "${env:VCPKG_ROOT};${env:VCPKG_ROOT}\downloads\tools\cmake-3.30.1-windows\cmake-3.30.1-windows-i386\bin;${env:VCPKG_ROOT}\downloads\tools\python\python-3.12.7-x64-1"
	${env:PATH_SETUP_DONE} = 1
}
setx PKG_CONFIG_PATH "C:\gstreamer\1.0\msvc_x86_64\lib\pkgconfig"
C:\BuildTools\Common7\Tools\Launch-VsDevShell.ps1
$DLSTREAMER_SRC_LOCATION = $PWD.Path
Write-Host "############################################ DONE ###########################################"

if (-Not (Test-Path "${env:VCPKG_ROOT}")) {
	Write-Host "####################################### Installing VCPKG #######################################"
	git clone --recursive https://github.com/microsoft/vcpkg.git ${env:VCPKG_ROOT}
	Set-Location -Path ${env:VCPKG_ROOT}
	Start-Process -Wait -FilePath powershell.exe -ArgumentList "-file", "${env:VCPKG_ROOT}\scripts\bootstrap.ps1", "-disableMetrics" -NoNewWindow
	Add-Content -Path ${env:VCPKG_ROOT}\triplets\x64-windows.cmake -Value "`r`nset(VCPKG_BUILD_TYPE release)"
	Write-Host "############################################ Done ##############################################"
} else {
	Write-Host "################################### VCPKG already installed ####################################"
}

if (-Not (Get-Command python -errorAction SilentlyContinue)) {
	Write-Host "####################################### Installing Python #######################################"
	Invoke-WebRequest -OutFile ${DLSTREAMER_TMP}\\python-3.12.7-amd64.exe -Uri https://www.python.org/ftp/python/3.12.7/python-3.12.7-amd64.exe
	Start-Process -Wait -FilePath "${DLSTREAMER_TMP}\\python-3.12.7-amd64.exe"  -ArgumentList "/quiet", "InstallAllUsers=1", "PrependPath=1", "Include_test=0" -NoNewWindow
	Write-Host "############################################## Done #############################################"
} else {
	Write-Host "#################################### Python already installed ##################################"
}
if (-Not (Get-Command py -errorAction SilentlyContinue)) {
	Set-Alias -Name python3 -Value python
	Set-Alias -Name py -Value python
	py --version
}

Write-Host "#################################### Preparing build directory #####################################"
if (Test-Path "${DLSTREAMER_TMP}\\build") {
	Remove-Item -LiteralPath "${DLSTREAMER_TMP}\\build" -Recurse
}
mkdir "${DLSTREAMER_TMP}\\build"
Write-Host "############################################# Done ##################################################"

Write-Host "####################################### Configuring VCPKG ###########################################"
Set-Location -Path "${DLSTREAMER_TMP}\\build"
Start-Process -Wait -FilePath "vcpkg" -ArgumentList "integrate", "install", "--triplet=x64-windows" -NoNewWindow
Start-Process -Wait -FilePath "vcpkg" -ArgumentList "install", "--triplet=x64-windows", "--vcpkg-root=${env:VCPKG_ROOT}", "--x-manifest-root=${DLSTREAMER_SRC_LOCATION}" -NoNewWindow
Start-Process -Wait -FilePath "taskkill" -ArgumentList "/im", "msbuild.exe", "/f", "/t" -NoNewWindow
Write-Host "########################################## Done #####################################################"

Write-Host "################################## Initializing OpenVINO ############################################"
& "$OPENVINO_DEST_FOLDER\setupvars.ps1"
Write-Host "####################################### Done ######################################################"

Write-Host "##################################### Running CMAKE #################################################"
$exit_code = Start-Process -Wait -FilePath "cmake" -ArgumentList "-DCMAKE_TOOLCHAIN_FILE=${env:VCPKG_ROOT}\scripts\buildsystems\vcpkg.cmake", "${DLSTREAMER_SRC_LOCATION}" -NoNewWindow
if (-Not $exit_code.ExitCode) {
	Write-Host "######################################## Done #######################################################"
	Write-Host "################################# Building DL Streamer ##############################################"
	cmake --build . -v --parallel 16 --target ALL_BUILD --config Release
	Write-Host "######################################## Done #######################################################"
} else {
	Write-Host "##################################### !CMAKE error! #################################################"
	exit $exit_code.ExitCode
}
