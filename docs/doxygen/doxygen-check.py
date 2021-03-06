#!/usr/bin/python
#
# This file is part of the GROMACS molecular simulation package.
#
# Copyright (c) 2014, by the GROMACS development team, led by
# Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
# and including many others, as listed in the AUTHORS file in the
# top-level source directory and at http://www.gromacs.org.
#
# GROMACS is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public License
# as published by the Free Software Foundation; either version 2.1
# of the License, or (at your option) any later version.
#
# GROMACS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with GROMACS; if not, see
# http://www.gnu.org/licenses, or write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
#
# If you want to redistribute modifications to GROMACS, please
# consider that scientific software is very special. Version
# control is crucial - bugs must be traceable. We will be happy to
# consider code for inclusion in the official distribution, but
# derived work must not be called official GROMACS. Details are found
# in the README & COPYING files - if they are missing, get the
# official version at http://www.gromacs.org.
#
# To help us fund GROMACS development, we humbly ask that you cite
# the research papers on the package. Check out http://www.gromacs.org.

"""Check Doxygen documentation for issues that Doxygen does not warn about.

This script for some issues in the Doxygen documentation, using Doxygen XML
output.  Part of the checks are generic, like checking that all documented
entities have brief descriptions.  Other are specific to GROMACS, like checking
that only installed headers contribute to the public API documentation.

The checks should be self-evident from the source code of the script.
All the logic of parsing the Doxygen XML output and creating a GROMACS-specific
representation of the source tree is separated into separate Python modules
(doxygenxml.py and gmxtree.py, respectively).  Similarly, logic for handling
the output messages is in reporter.py.   This leaves only the actual checks and
the script command-line interface in this file.

The script can be run using the 'doc-check' target generated by CMake.
This target takes care of generating all the necessary input files and passing
them to the script.
"""

import sys
from optparse import OptionParser

from gmxtree import GromacsTree, DocType
from reporter import Reporter

def check_file(fileobj, reporter):
    """Check file-level documentation."""
    if not fileobj.is_documented():
        # TODO: Add rules for required documentation
        return

    if fileobj.is_source_file():
        # TODO: Add rule to exclude examples from this check
        if fileobj.is_installed():
            reporter.file_error(fileobj, "source file is installed")
        if fileobj.get_documentation_type() != DocType.internal:
            reporter.file_error(fileobj,
                    "source file documentation appears outside full documentation")
        elif fileobj.get_api_type() != DocType.internal:
            reporter.file_error(fileobj, "source file marked as non-internal")
    elif fileobj.is_test_file() and fileobj.is_installed():
        reporter.file_error(fileobj, "test file is installed")
    elif fileobj.is_installed():
        if fileobj.get_documentation_type() != DocType.public:
            reporter.file_error(fileobj,
                    "public header has non-public documentation")
    elif fileobj.get_documentation_type() == DocType.public:
        reporter.file_error(fileobj,
                "non-installed header has public documentation")
    elif fileobj.get_api_type() == DocType.public:
        reporter.file_error(fileobj,
                "non-installed header specified as part of public API")
    elif fileobj.get_documentation_type() < fileobj.get_api_type():
        reporter.file_error(fileobj,
                "API type ({0}) conflicts with documentation visibility ({1})"
                .format(fileobj.get_api_type(), fileobj.get_documentation_type()))

    if not fileobj.has_brief_description():
        reporter.file_error(fileobj,
                "is documented, but does not have brief description")

    expectedmod = fileobj.get_expected_module()
    if expectedmod:
        docmodules = fileobj.get_doc_modules()
        if docmodules:
            for module in docmodules:
                if module != expectedmod:
                    reporter.file_error(fileobj,
                            "is documented in incorrect module: {0}"
                            .format(module.get_name()))
        elif expectedmod.is_documented():
            reporter.file_error(fileobj,
                    "is not documented in any module, but {0} exists"
                    .format(expectedmod.get_name()))

def check_include(fileobj, includedfile, reporter):
    """Check an #include directive."""
    if includedfile.is_system():
        if includedfile.get_file():
            reporter.code_issue(includedfile,
                    "includes local file as {0}".format(includedfile))
    else:
        otherfile = includedfile.get_file()
        if not otherfile:
            reporter.code_issue(includedfile,
                    "includes non-local file as {0}".format(includedfile))
        elif fileobj.is_installed() and not includedfile.is_relative():
            reporter.code_issue(includedfile,
                    "installed header includes {0} using non-relative path"
                    .format(includedfile))
        if not otherfile:
            return
        if fileobj.is_installed() and not otherfile.is_installed():
            reporter.code_issue(includedfile,
                    "installed header includes non-installed {0}"
                    .format(includedfile))
        filemodule = fileobj.get_module()
        othermodule = otherfile.get_module()
        if fileobj.is_documented() and otherfile.is_documented():
            filetype = fileobj.get_documentation_type()
            othertype = otherfile.get_documentation_type()
            if filetype > othertype:
                reporter.code_issue(includedfile,
                        "{0} file includes {1} file {2}"
                        .format(filetype, othertype, includedfile))
        check_api = (othermodule and othermodule.is_documented() and
                filemodule != othermodule)
        if check_api and otherfile.get_api_type() < DocType.library:
            reporter.code_issue(includedfile,
                    "included file {0} is not documented as exposed outside its module"
                    .format(includedfile))

def check_entity(entity, reporter):
    """Check documentation for a code construct."""
    if entity.is_documented():
        if not entity.has_brief_description():
            reporter.doc_error(entity,
                    "is documented, but does not have brief description")

def check_class(classobj, reporter):
    """Check documentation for a class/struct/union."""
    check_entity(classobj, reporter)
    if classobj.is_documented():
        classtype = classobj.get_documentation_type()
        filetype = classobj.get_file_documentation_type()
        if classtype == DocType.public and not classobj.is_in_installed_file():
            reporter.doc_error(classobj,
                    "has public documentation, but is not in installed header")
        elif filetype is not DocType.none and classtype > filetype:
            reporter.doc_error(classobj,
                    "is in {0} file(s), but appears in {1} documentation"
                    .format(filetype, classtype))

def check_member(member, reporter):
    """Check documentation for a generic member."""
    check_entity(member, reporter)
    if member.is_documented():
        if not member.is_visible():
            # TODO: This is triggered by members in anonymous namespaces.
            reporter.doc_note(member,
                    "is documented, but is ignored by Doxygen, because its scope is not documented")
        if member.has_inbody_description():
            reporter.doc_note(member, "has in-body comments, which are ignored")

def main():
    """Run the checking script."""
    parser = OptionParser()
    parser.add_option('-S', '--source-root',
                      help='Source tree root directory')
    parser.add_option('-B', '--build-root',
                      help='Build tree root directory')
    parser.add_option('--installed',
                      help='Read list of installed files from given file')
    parser.add_option('-l', '--log',
                      help='Write issues into a given log file in addition to stderr')
    parser.add_option('--ignore',
                      help='Set file with patterns for messages to ignore')
    parser.add_option('--check-ignored', action='store_true',
                      help='Check documentation ignored by Doxygen')
    parser.add_option('-q', '--quiet', action='store_true',
                      help='Do not write status messages')
    options, args = parser.parse_args()

    installedlist = []
    if options.installed:
        with open(options.installed, 'r') as outfile:
            for line in outfile:
                installedlist.append(line.strip())

    reporter = Reporter(options.log)
    if options.ignore:
        reporter.load_filters(options.ignore)

    if not options.quiet:
        sys.stderr.write('Scanning source tree...\n')
    tree = GromacsTree(options.source_root, options.build_root, reporter)
    tree.set_installed_file_list(installedlist)
    if not options.quiet:
        sys.stderr.write('Reading source files...\n')
    tree.scan_files()
    if not options.quiet:
        sys.stderr.write('Reading Doxygen XML files...\n')
    tree.load_xml()

    reporter.write_pending()

    if not options.quiet:
        sys.stderr.write('Checking...\n')

    for fileobj in tree.get_files():
        check_file(fileobj, reporter)
        for includedfile in fileobj.get_includes():
            check_include(fileobj, includedfile, reporter)

    for classobj in tree.get_classes():
        check_class(classobj, reporter)

    for memberobj in tree.get_members():
        if memberobj.is_visible() or options.check_ignored:
            check_member(memberobj, reporter)

    reporter.write_pending()
    reporter.report_unused_filters()
    reporter.close_log()

main()
