# ==============================================================================
# Copyright (C) 2024-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Collect CTest and Pytest results into a text summary."""

import sys

# Only use defusedxml - no fallback to unsafe xml.etree
try:
    from defusedxml import ElementTree as ET
except ImportError:
    print("Error: defusedxml is required for secure XML parsing")
    print("Please install it with: pip install defusedxml")
    sys.exit(1)

# usage: python unit_test_results.py path/to/your/results

RESULTS_PATH = sys.argv[1]

CTEST_RESULT_XML = f"{RESULTS_PATH}/ctest-junit.xml"
PYTEST_RESULT_XML = f"{RESULTS_PATH}/python_tests_results.xml"

SUMMARY_FILE = f"{RESULTS_PATH}/unit_test_summary.txt"

def extract_test_results(xml_file, test_type):
    """Extract aggregate test counts and failed test names from a JUnit XML file."""
    tree = ET.parse(xml_file)
    root = tree.getroot()
    failed_tests = []
    # for pytests; attributes are in <testsuite> which is a child of <testsuites>
    if test_type == 'pytest' and root.tag == 'testsuites':
        testsuite = root.find('testsuite')
    else:
        testsuite = root

    total = int(testsuite.attrib.get('tests', 0))
    failed = int(testsuite.attrib.get('failures', 0))
    errors = int(testsuite.attrib.get('errors', 0))
    skipped = int(testsuite.attrib.get('skipped', 0))

    for testcase in testsuite.findall('testcase'):
        if test_type == 'ctest':
            if testcase.get('status') == 'fail':
                failed_tests.append(testcase.attrib['name'])
        elif test_type == 'pytest':
            failure = testcase.find('failure')
            if failure is not None:
                failed_tests.append(testcase.attrib['name'])

    passed = total - failed - errors - skipped
    return total, passed, failed, errors, skipped, failed_tests

def save_summary(test_type, results):
    """Append one test framework summary to the output file."""
    with open(SUMMARY_FILE, 'a', encoding='utf-8') as f:
        f.write(f"{test_type}: Total: {results[0]}, Passed: {results[1]}, "
                f"Failed: {results[2]}, Errors: {results[3]}, Skipped: {results[4]}\n")
        if results[2] > 0:
            f.write(f"Failed tests ({test_type}):\n")
            for test_name in results[5]:
                f.write(f"    - {test_name}\n")
#ctests
try:
    ctest_results = extract_test_results(CTEST_RESULT_XML, 'ctest')
    save_summary("CTest", ctest_results)
except FileNotFoundError:
    print(f"File not found {CTEST_RESULT_XML}")

#pytests
try:
    pytest_results = extract_test_results(PYTEST_RESULT_XML, 'pytest')
    save_summary("Pytest", pytest_results)
except FileNotFoundError:
    print(f"File not found {PYTEST_RESULT_XML}")
