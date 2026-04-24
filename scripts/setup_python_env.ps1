# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

Write-Host "`n=== DL Streamer Python Environment Setup ===" -ForegroundColor Cyan

$requirementsTxt = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot "..\requirements.txt")
)

function Assert-Status {
    param($message, $status, $details = "")
    if ($status) { return }
    Write-Host "[X ] " -ForegroundColor Red -NoNewline
    Write-Host $message
    if ($details) {
        Write-Host "    $details" -ForegroundColor Yellow
    }
    Write-Host "`nAborting" -ForegroundColor Red
    exit 1
}

function Invoke-Python {
    param([string]$code)
    $out = & python -c $code 2>&1 | Out-String
    return @{ ok = ($LASTEXITCODE -eq 0); output = $out.Trim() }
}

function Test-PythonImport {
    param($label, $code, $failDetails)
    $r = Invoke-Python $code
    Assert-Status "$label is not installed" $r.ok `
    ($r.output + "`n`n" + $failDetails)
    return $r
}

# 1. Check Python installation
$hasPython = [bool](Get-Command python -ErrorAction SilentlyContinue)
Assert-Status "Python is not in PATH" $hasPython "Install Python and add to PATH"
$pythonVersion = (python --version 2>&1 | Out-String).Trim()
Write-Host "$pythonVersion is installed" -ForegroundColor Green

# 2. Check GStreamer installation
$gstreamerRoot = $env:GSTREAMER_1_0_ROOT_MSVC_X86_64
Assert-Status "GSTREAMER_1_0_ROOT_MSVC_X86_64 is not set" ([bool]$gstreamerRoot) `
    "Check GStreamer installation and ensure environment variable is set"
$gstBin = Join-Path $gstreamerRoot "bin"
Assert-Status "GStreamer bin directory does not exist" (Test-Path $gstBin) `
    "Path: $gstBin"

# 3. Locate gstreamer-python pip package
$locateCode = "import gstreamer_python, os; " +
"print(os.path.dirname(gstreamer_python.__file__))"
$r = Invoke-Python $locateCode
Assert-Status "gstreamer-python package is not installed" $r.ok `
    "Install dependencies: pip install -r $requirementsTxt"
$candidate = Join-Path $r.output "Lib\site-packages"
Assert-Status "gstreamer-python site-packages not found" (Test-Path $candidate) `
    "Expected at: $candidate"
$gstPythonSitePackages = $candidate

# 4. Configure PYTHONPATH, PYGI_DLL_DIRS, GI_TYPELIB_PATH
Write-Host "Configuring environment for current session..." -ForegroundColor Yellow

$dlsPythonDir = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot "..\python")
)
Assert-Status "DL Streamer python binding is not found" (Test-Path $dlsPythonDir) `
    "Ensure it is installed. Expected at: $dlsPythonDir"

$typelibCandidates = @(
    (Join-Path $PSScriptRoot "..\lib\girepository-1.0"),
    (Join-Path $PSScriptRoot "..\build\src\gst\metadata")
) | ForEach-Object { [System.IO.Path]::GetFullPath($_) }
$dlsTypelibDir = $typelibCandidates |
Where-Object { Test-Path $_ } |
Select-Object -First 1
if (-not $dlsTypelibDir) { $dlsTypelibDir = $typelibCandidates[0] }
$searched = ($typelibCandidates -join "`n    ")
Assert-Status "GI typelib directory not found" (Test-Path $dlsTypelibDir) `
    "Searched:`n    $searched"

$parts = @()
if ($env:PYTHONPATH) {
    $parts += ($env:PYTHONPATH -split ";" | Where-Object { $_ })
}
foreach ($p in @($gstPythonSitePackages, $dlsPythonDir)) {
    if ($p -and ($parts -notcontains $p)) { $parts += $p }
}
$env:PYTHONPATH = $parts -join ";"
$env:PYGI_DLL_DIRS = $gstBin
$env:GI_TYPELIB_PATH = $dlsTypelibDir

Write-Host "PYTHONPATH:" -ForegroundColor Gray
foreach ($p in $parts) { Write-Host "    $p" -ForegroundColor Gray }
Write-Host "PYGI_DLL_DIRS:   $gstBin" -ForegroundColor Gray
Write-Host "GI_TYPELIB_PATH: $dlsTypelibDir" -ForegroundColor Gray

# 5. Check Python packages
Write-Host "Checking Python packages..." -ForegroundColor Yellow
$pipHint = "Install dependencies: pip install -r $requirementsTxt"
Test-PythonImport "gi" `
    "import gi; print(gi.__version__)" `
    $pipHint | Out-Null
Test-PythonImport "gstgva" `
    "import gstgva" `
    "Ensure DL Streamer python binding is installed" | Out-Null

# 6. Check GStreamer element creation
Write-Host "Checking GStreamer element creation..." -ForegroundColor Yellow
$gvaCode = "import gi; gi.require_version('Gst', '1.0'); " +
"from gi.repository import Gst; Gst.init(None); " +
"e = Gst.ElementFactory.make('gvadetect', 'd'); " +
"print('success' if e else 'failed')"
$r = Invoke-Python $gvaCode
Assert-Status "gvadetect element creation failed" ($r.ok -and $r.output -match "success") `
    "Ensure GST_PLUGIN_PATH is properly set"

Write-Host "DL Streamer Python environment is properly configured for this session." `
    -ForegroundColor Green
Write-Host ""
