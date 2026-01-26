#!/bin/bash
# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -e

SCRIPTDIR="$(dirname "$(readlink -fm "$0")")"

build_dir=${1:-"${SCRIPTDIR}/../build"}
rebuild_with_code_coverage=${2:-"false"}
result_path=${3}
build_type=${4:-"Debug"}
timeout_mult=${5:-20}

# FIXME: our tests on GPU don't fit into default timeout (~4s)
export CK_TIMEOUT_MULTIPLIER=$timeout_mult
echo "Default timeout multiplier is set to: $CK_TIMEOUT_MULTIPLIER"

export GST_DEBUG=3
export GST_DEBUG_NO_COLOR=1

if [ -z "$result_path" ]; then
  result_path="$(realpath "./ctest_result")"
fi

if [ ! -d "$result_path" ]; then
  mkdir -p "$result_path"
fi

[ -z "$MODELS_PATH" ] && echo "MODELS_PATH env is not set. Some tests may fail."
[ -z "$MODELS_PROC_PATH" ] && echo "MODELS_PROC_PATH env is not set. Some tests may fail."
[ -z "$VIDEO_EXAMPLES_DIR" ] && echo "VIDEO_EXAMPLES_DIR env is not set. Some tests may fail."

export GST_PLUGIN_PATH="$build_dir"/intel64/$build_type/lib:$GST_PLUGIN_PATH
export LD_LIBRARY_PATH="$build_dir"/intel64/$build_type/lib:$LD_LIBRARY_PATH

pushd "$build_dir"

if [ "$rebuild_with_code_coverage" = true ]; then
  mkdir -p "$build_dir"
  # Cleanup gstreamer cache and build dir if there was a previous build
  rm -rf ~/.cache/gstreamer-1.0
  rm -rf "${build_dir:?}"/*

  if [[ "$(which cmake3)" == "cmake3 not found" ]] || [[ -z "$(which cmake3)" ]];
  then
    CMAKE=cmake;
  else
    CMAKE=cmake3;
  fi

  $CMAKE -DCMAKE_BUILD_TYPE="$build_type" -DENABLE_CODE_COVERAGE=ON -DENABLE_VAAPI=ON -DTREAT_WARNING_AS_ERROR=OFF .. ;

  make -j"$(nproc --all)" ;
elif [ ! -d "$build_dir" ]; then
  echo "Build dir doesn't exist: $build_dir"
  exit 1
fi

# Test CTest version for JUnit support
ctest_ver=$(ctest --version | head -n1 | grep -Po '\d+.\d+.\d+')
ctest_ver_junit="3.21.4" # Where was added support for JUnit report

echo "CTest version $ctest_ver"
ctest_args_junit=()
if [ "$ctest_ver_junit" = "$(echo -e "$ctest_ver\n$ctest_ver_junit" | sort -V | head -n1 )" ]; then
  echo "CTest has JUnit support"
  ctest_args_junit=(--output-junit "$result_path/ctest-junit.xml")
fi

# Here we want to run all tests but return error if tests crashed
set +e
ret_code=0

ctest -T Test --output-on-failure --verbose "${ctest_args_junit[@]}" || ret_code=$?
cp ./Testing/"$(head -n 1 Testing/TAG)"/Test.xml "$result_path/CTestResults.xml"
popd

SRC_DIR=$build_dir/..

pushd "$SRC_DIR"/tests/tests_gstgva
py.test --junitxml="$result_path"/python_tests_results.xml || ret_code=$?
popd

if [ "$rebuild_with_code_coverage" = true ]; then
  mkdir -p "$result_path"/code_coverage
  gcovr -r "$SRC_DIR" -e "$SRC_DIR"/thirdparty/ -e "$SRC_DIR"/tests/ -e "$SRC_DIR"/samples/ --html --html-details -o "$result_path"/code_coverage/index.html
fi

# 1, 8 means that tests were run but some failed. We check it in CI
if [[ "$ret_code" -eq 1 || "$ret_code" -eq 8 ]]
then
  echo "Overriding exit code. Actual exit code: $ret_code"
  ret_code=0
fi

echo "Test results are located at: $result_path"

exit "$ret_code"
