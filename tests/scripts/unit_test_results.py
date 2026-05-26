# ==============================================================================
# Copyright (C) 2024-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import xml.etree.ElementTree as ET
import sys

#usage: python unit_test_results.py path/to/your/results

results_path = sys.argv[1]

ctest_result_xml = f"{results_path}/ctest-junit.xml"
pytest_result_xml = f"{results_path}/python_tests_results.xml"

summary_file = f"{results_path}/unit_test_summary.txt"

def extract_test_results(xml_file, test_type):
    tree = ET.parse(xml_file)
    root = tree.getroot()
    failed_tests = []
#for pytests; attributes are in <testsuite> which is a child of <testsuites>
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
    with open(summary_file, 'a') as f:
        f.write(f"{test_type}: Total: {results[0]}, Passed: {results[1]}, "
                f"Failed: {results[2]}, Errors: {results[3]}, Skipped: {results[4]}\n")
        if results[2] > 0:
            f.write(f"Failed tests ({test_type}):\n")
            for test_name in results[5]:
                f.write(f"    - {test_name}\n")
#ctests
try:
    ctest_results = extract_test_results(ctest_result_xml, 'ctest')
    save_summary("CTest", ctest_results)
except FileNotFoundError:
    print(f"File not found {ctest_result_xml}")

#pytests
try:
    pytest_results = extract_test_results(pytest_result_xml, 'pytest')
    save_summary("Pytest", pytest_results)
except FileNotFoundError:
    print(f"File not found {pytest_result_xml}")
