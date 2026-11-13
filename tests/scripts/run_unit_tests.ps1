# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# Runs the C++ (ctest) and Python (pytest) unit tests on Windows; the counterpart of run_unit_tests.sh.
#
# Requires the build tree (for the C++ tests, built with scripts\build_dlstreamer_dlls.ps1 -enableTests)
# and, for the Python tests, `python` on PATH with requirements.txt and pytest==8.3.3 installed. Set
# MODELS_PATH and VIDEO_EXAMPLES_DIR; MODELS_PROC_PATH defaults to the repository's
# samples\gstreamer\model_proc.
#
# As with run_unit_tests.sh, failing tests don't change the exit code: CI reads the JUnit reports in
# ResultsDir. A non-zero exit code means a suite didn't produce its report.
# The caller's environment variables and current directory are restored on exit.

param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\..\build"),
    [string]$ResultsDir = (Join-Path (Get-Location) "ctest_result"),
    [string]$BuildType = "Release",
    [ValidateSet("All", "Cpp", "Python")]
    [string]$Suite = "All",
    # The OpenVINO runtime the plugins were built against
    [string]$OpenVINODir = $(if ($env:INTEL_OPENVINO_DIR) { $env:INTEL_OPENVINO_DIR } else { "$env:LOCALAPPDATA\Programs\openvino" })
)

$ErrorActionPreference = "Stop"

$repoDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$BuildDir = (Resolve-Path $BuildDir).Path
$binDir = Join-Path $BuildDir "intel64\$BuildType\bin"
if (-not (Test-Path $binDir)) {
    Write-Host "Error: build output not found: $binDir"
    exit 1
}

New-Item -ItemType Directory -Path $ResultsDir -Force | Out-Null
$ResultsDir = (Resolve-Path $ResultsDir).Path
# Reports from an earlier run would hide a suite that doesn't produce one this time
foreach ($report in "ctest-junit.xml", "CTestResults.xml", "LastTest*.log", "python_tests_results.xml", "unit_test_summary.txt") {
    Remove-Item (Join-Path $ResultsDir $report) -Force -ErrorAction SilentlyContinue
}

$savedEnv = @{}
Get-ChildItem env: | ForEach-Object { $savedEnv[$_.Name] = $_.Value }
Push-Location
$exitCode = 0
try {
    # Use the plugins from the build tree, with a private registry, and the OpenVINO runtime they
    # were built against
    $openvinoRuntimeDir = Join-Path $OpenVINODir "runtime"
    $env:OPENVINO_LIB_PATHS = "$openvinoRuntimeDir\bin\intel64\Release;$openvinoRuntimeDir\3rdparty\tbb\bin"
    $env:PATH = "$binDir;$env:OPENVINO_LIB_PATHS;$env:PATH"
    $env:GST_PLUGIN_PATH = $binDir
    $env:GST_REGISTRY = Join-Path ([System.IO.Path]::GetTempPath()) "gst_registry_dlstreamer_unit_tests.bin"
    Remove-Item $env:GST_REGISTRY -Force -ErrorAction SilentlyContinue
    $env:GST_DEBUG = "3"
    $env:GST_DEBUG_NO_COLOR = "1"
    if (-not $env:MODELS_PROC_PATH) {
        $env:MODELS_PROC_PATH = Join-Path $repoDir "samples\gstreamer\model_proc"
    }
    foreach ($var in "MODELS_PATH", "VIDEO_EXAMPLES_DIR") {
        if (-not (Test-Path "env:$var")) {
            Write-Host "$var env is not set. Some tests may fail."
        }
    }

    if ($Suite -ne "Python") {
        Write-Host "`n=== C++ unit tests (ctest) ===" -ForegroundColor Cyan
        # libcheck can't fork on Windows, so it can't time out a hung test case: rely on ctest --timeout.
        # Same exclusions as run_unit_tests.sh (test_symlink isn't built on Windows).
        Set-Location $BuildDir
        ctest -C $BuildType -T Test --timeout 900 --output-on-failure `
            -E "test_audio_detection_plugin" `
            --output-junit "$ResultsDir\ctest-junit.xml"
        Write-Host "ctest exit code: $LASTEXITCODE"

        if (Test-Path "Testing\TAG") {
            $tag = Get-Content "Testing\TAG" -TotalCount 1
            Copy-Item "Testing\$tag\Test.xml" "$ResultsDir\CTestResults.xml" -ErrorAction SilentlyContinue
        }
        # -T Test names the log LastTest_<timestamp>.log; earlier runs leave theirs behind
        Get-ChildItem "Testing\Temporary\LastTest*.log" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime | Select-Object -Last 1 | Copy-Item -Destination $ResultsDir

        if (-not (Test-Path "$ResultsDir\ctest-junit.xml")) {
            Write-Host "Error: ctest did not produce a JUnit report"
            $exitCode = 1
        }
    }

    if ($Suite -ne "Cpp") {
        Write-Host "`n=== Python unit tests (pytest) ===" -ForegroundColor Cyan
        # Python paths come only from here and setup_python_env.ps1, so that an installed
        # DL Streamer's gstgva or typelibs can't shadow the ones from this repository.
        # test_pipeline_optimizer imports openvino: use the bindings that come with the OpenVINO
        # runtime, as setupvars.ps1 does (without its dependency on the py launcher). A pip openvino
        # of another version loads its own openvino.dll, and the plugins then fail to load.
        if (-not (Test-Path "$OpenVINODir\python")) {
            Write-Host "OpenVINO Python bindings not found in $OpenVINODir\python. test_pipeline_optimizer will fail."
        }
        $env:PYTHONPATH = "$OpenVINODir\python;$repoDir\scripts\optimizer"
        $env:PYTHONIOENCODING = "utf-8"
        Remove-Item env:PYGI_DLL_DIRS, env:GI_TYPELIB_PATH -ErrorAction SilentlyContinue
        if (-not $env:GSTREAMER_1_0_ROOT_MSVC_X86_64) {
            # The GStreamer installer sets it machine-wide, possibly after this session started
            $env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = [Environment]::GetEnvironmentVariable("GSTREAMER_1_0_ROOT_MSVC_X86_64", "Machine")
        }

        # PyGObject, gstgva and the typelibs; also patches the GstAnalytics overrides.
        # Its checks capture native stderr, which must not stop this script.
        $ErrorActionPreference = "Continue"
        & "$repoDir\scripts\setup_python_env.ps1"
        $ErrorActionPreference = "Stop"

        python -m pytest --version
        if ($LASTEXITCODE -ne 0) {
            throw "pytest is not installed. Run: python -m pip install -r `"$repoDir\requirements.txt`" pytest==8.3.3"
        }

        # Same invocation as run_unit_tests.sh. With --continue-on-collection-errors a module that
        # fails to import (for example, because of a missing model) doesn't stop the other modules.
        Set-Location "$repoDir\tests\unit_tests\tests_gstgva"
        python -m pytest -v -s --tb=short --continue-on-collection-errors `
            --deselect=test_pipeline_custom_preproc.py::TestCustomPreProcPipeline::test_custom_opencv_resnet_pipeline `
            --junitxml="$ResultsDir\python_tests_results.xml"
        Write-Host "pytest exit code: $LASTEXITCODE"

        if (-not (Test-Path "$ResultsDir\python_tests_results.xml")) {
            Write-Host "Error: pytest did not produce a JUnit report"
            $exitCode = 1
        }
    }
}
finally {
    Pop-Location
    foreach ($var in Get-ChildItem env:) {
        if (-not $savedEnv.ContainsKey($var.Name)) {
            Remove-Item -LiteralPath "env:$($var.Name)"
        }
    }
    foreach ($name in $savedEnv.Keys) {
        # Only touch changed variables: setting one to an empty value would delete it
        if ((Get-Item -LiteralPath "env:$name" -ErrorAction SilentlyContinue).Value -cne $savedEnv[$name]) {
            Set-Item -LiteralPath "env:$name" -Value $savedEnv[$name]
        }
    }
}

Write-Host "`nTest results are located at: $ResultsDir"
exit $exitCode
