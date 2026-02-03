# ==============================================================================
# Copyright (C) 2024-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import sys
import os
import argparse
import re
import fnmatch
import functools
import itertools
import datetime
from functools import reduce

COPYRIGHT_YEAR = str(datetime.datetime.now().year)

def check_copyright_year(copyright_patterns):
    def check(content):
        for i, pattern in enumerate(copyright_patterns):
            match = re.search(pattern[0], content, re.DOTALL)
            if match:
                if COPYRIGHT_YEAR not in match.group(1):
                    return f"Year {COPYRIGHT_YEAR} is missing" 
                else:
                    print(f"DEBUG: Year {COPYRIGHT_YEAR} is present")
        return None
    return check

def regular_expression_check(regexp, error_msg, result_checker, content):
    re_check = re.compile(regexp, re.DOTALL)
    result = re_check.search(content)
    if result_checker(result):
        return error_msg

def check_any_copyright_header(copyright_patterns, content):
    for pattern in copyright_patterns:
        if not regular_expression_check(pattern[0], pattern[1], pattern[2], content):
            return None
    return "There is no valid copyright header (MIT or Apache v2)"

def process_file(file, rulename, checkers):
    # Whole content check
    content = file.read()
    whole_file_errors = [e for e in [checker(content) for checker in checkers[0]] if e is not None]
    # Per line check
    lines = content.split("\n")
    line_errors = [el for el in list(itertools.chain.from_iterable(
        [[(check(line), idx) for (idx, line) in enumerate(lines)] for check in checkers[1]])) if el[0] is not None]
    if (whole_file_errors or line_errors):
        # print("Errors in " + file.name + " with rules: " + rulename)
        print((file.name))
        for error in whole_file_errors:
            print(("\tError: " + error))
        for error in line_errors:
            print(("\tError: {0} on line {1}".format(*error)))
        # print("\tTotal number of errors: {0}".format(
        #    len(whole_file_errors) + len(line_errors)))
        return False
    else:
        return True


def is_valid_file(parser, arg):
    if not os.path.exists(arg):
        print("The file %s does not exist." % arg)
        exit(0)
    else:
        return open(arg, 'r')  # return an open file handle


def create_parser():
    parser = argparse.ArgumentParser(
        description='Checking file againt predefined set of rules')
    parser.add_argument('-y', "--year", type=str)
    parser.add_argument('file',
                        type=lambda x: is_valid_file(parser, x),
                        nargs='+')
    return parser


def process_files(file_types_descs, files):
    result = [[process_file(f, check[0], check[2:]) for check in [
        e for e in file_types_descs if fnmatch.fnmatch(f.name, e[1])]] for f in list(files)]
    return reduce(
        lambda a, e: a and e,
        list(itertools.chain.from_iterable(result)),
        True)

def init():
    c_copyright_mit = (r'''\/\*+\s*\r?''' +
                       r'''\* Copyright \(C\) (.*) Intel Corporation\s*\r?''' +
                       r'''\*\s*\r?''' +
                       r'''\* SPDX-License-Identifier: MIT\s*\r?''' +
                       r'''( \*[^\n\r]*\s*\r?)*''' +
                       r''' \*+\/''',
                      "There is no C style MIT copyright header",
                      lambda x: not x)

    c_copyright_apache = (r'''\/\*+\s*\r?''' +
                         r'''\* Apache v2 license\s*\r?''' +
                         r'''\* Copyright \(C\) (.*) Intel Corporation\s*\r?''' +
                         r'''\* SPDX-License-Identifier: Apache-2\.0\s*\r?''' +
                         r'''( \*[^\n\r]*\s*\r?)*''' +
                         r''' \*+\/''',
                        "There is no C style Apache v2 copyright header",
                        lambda x: not x)


    sharp_copyright_mit = (r'''# ==============================================================================\s*\r?''' +
                          r'''# Copyright \(C\) (.*) Intel Corporation\s*\r?''' +
                          r'''#\s*\r?''' +
                          r'''# SPDX-License-Identifier: MIT\s*\r?''' +
                          r'''# ==============================================================================\s*\r?''',
                         "There is no sharp MIT copyright header",
                         lambda x: not x)

    sharp_copyright_apache = (r'''#\s*\r?''' +
                             r'''# Apache v2 license\s*\r?''' +
                             r'''# Copyright \(C\) (.*) Intel Corporation\s*\r?''' +
                             r'''# SPDX-License-Identifier: Apache-2\.0\s*\r?''' +
                             r'''#\s*\r?''',
                            "There is no sharp Apache v2 copyright header",
                            lambda x: not x)

    rem_copyright_mit = (r'''@?REM ==============================================================================\s*\r?''' +
                        r'''@?REM Copyright \(C\) (.*) Intel Corporation\s*\r?''' +
                        r'''@?REM\s*\r?''' +
                        r'''@?REM SPDX-License-Identifier: MIT\s*\r?''' +
                        r'''@?REM ==============================================================================\s*\r?''',
                    "There is no REM MIT copyright header",
                    lambda x: not x)

    using_in_header = (
        "using namespace [^;]*;",
        "There is using in header",
        lambda x: x)

    windows_line_endings = (
        ".*\r$",
        "There is windows line endings in header",
        lambda x: x)

    def rem_copyright_exist(content):
        return check_any_copyright_header([rem_copyright_mit], content)

    def c_copyright_exist(content):
        return check_any_copyright_header([c_copyright_mit, c_copyright_apache], content)

    def sharp_copyright_exist(content):
        return check_any_copyright_header([sharp_copyright_mit, sharp_copyright_apache], content)

    using_in_header_exist = functools.partial(regular_expression_check, *using_in_header)
    windows_line_ending_exist = functools.partial(regular_expression_check, *windows_line_endings)
    
    year_check_in_c_copyright = check_copyright_year([c_copyright_mit, c_copyright_apache])
    year_check_in_sharp_copyright = check_copyright_year([sharp_copyright_mit, sharp_copyright_apache])
    year_check_in_rem_copyright = check_copyright_year([rem_copyright_mit])

    # Checkers configuration
    # (Rule name, File name pattern,  whole file checkers,     line-by-line checkers)
    file_types_descs = [
        ("C/C++ Header checks", "*.h", [c_copyright_exist, year_check_in_c_copyright], [using_in_header_exist]),
        ("C++ Header checks", "*.hpp", [c_copyright_exist, year_check_in_c_copyright], [using_in_header_exist]),
        ("C Source file check", "*.c", [c_copyright_exist, year_check_in_c_copyright], []),
        ("C++ Source file check", "*.cpp", [c_copyright_exist, year_check_in_c_copyright], []),
        ("CMakeLists check", "*CMakeLists.txt", [sharp_copyright_exist, year_check_in_sharp_copyright], []),
        ("Python files check", "*.py", [sharp_copyright_exist, year_check_in_sharp_copyright], []),
        ("Shell files check", "*.sh", [sharp_copyright_exist, year_check_in_sharp_copyright], []),
        ("Batch files check", "*.bat", [rem_copyright_exist, year_check_in_rem_copyright], []),
    ]
    return file_types_descs

def main():
    parser = create_parser()
    arguments = parser.parse_args()
    return not process_files(init(), arguments.file)


if __name__ == "__main__":
    sys.exit(main())
