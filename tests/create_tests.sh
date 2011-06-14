#!/bin/sh

# TODO:
# - check that -o produces an output file
# - check that -o copes with an existing output file
# - check that -o /dev/stdout doesn't do anything too nasty
# - check that -I and -D work
# - fault injection on malloc()
# - mocking of CL functions to test zero platforms, zero devices, query failures etc.

set -e
rm -f QMTest/configuration QMTest/*.pyc QMTest/*.qm*
rm -rf *.qms
rm -f ../build/*.gcda
qmtest create-tdb
qmtest register test command_regex.ExecTest
PROGRAM=../build/onlineclc-cov

qmtest create -i cunit.all \
    -a program=../build/onlineclc-test \
    -a stdout='.+' \
    -a exit_code=0 \
    test command_regex.ExecTest

qmtest create -i cmdparse.help \
    -a program="$PROGRAM" \
    -a stdout='Usage:.*' \
    -a exit_code=0 \
    -a arguments="['--help']" \
    test command_regex.ExecTest
qmtest create -i cmdparse.noargs \
    -a program="$PROGRAM" \
    -a stderr='Source file not specified\n.*' \
    -a exit_code=2 \
    -a arguments="[]" \
    test command_regex.ExecTest
qmtest create -i cmdparse.bad_device \
    -a program="$PROGRAM" \
    -a stderr='No OpenCL device called `bad'\'' found' \
    -a exit_code=1 \
    -a arguments="['-b', 'bad', 'empty.cl']" \
    test command.ExecTest
qmtest create -i cmdparse.long \
    -a program="$PROGRAM" \
    -a exit_code=0 \
    -a arguments="['-D', 'a_very_long_symbol_to_test_that_the_dynamic_allocation_of_the_command_line_works_correctly_even_when_multiple_allocations_are_required=1', 'empty.cl']" \
    test command.ExecTest

qmtest create -i compile.empty \
    -a program="$PROGRAM" \
    -a exit_code=0 \
    -a arguments="['empty.cl']" \
    test command.ExecTest
qmtest create -i compile.quotes \
    -a program="$PROGRAM" \
    -a exit_code=0 \
    -a arguments="['\"quotes\".cl']" \
    test command.ExecTest
qmtest create -i compile.nosource \
    -a program="$PROGRAM" \
    -a stderr="Failed to open \`nosource.cl': No such file or directory" \
    -a exit_code=1 \
    -a arguments="['nosource.cl']" \
    test command.ExecTest
qmtest create -i compile.invalid \
    -a program="$PROGRAM" \
    -a stderr='.+' \
    -a exit_code=1 \
    -a arguments="['invalid.cl']" \
    test command_regex.ExecTest
qmtest create -i compile.invalid_output \
    -a program="$PROGRAM" \
    -a stderr="Failed to open \`bad/bad.out': No such file or directory" \
    -a exit_code=1 \
    -a arguments="['-o', 'bad/bad.out', 'empty.cl']" \
    test command.ExecTest
# TODO: set up a temp directory resource to clean up the file afterwards
qmtest create -i compile.write_output \
    -a program="$PROGRAM" \
    -a exit_code=0 \
    -a arguments="['-o', '../build/test.out', 'empty.cl']" \
    test command.ExecTest

# Doesn't pass because stdout is a pipe
#qmtest create -i compile.log_stdout \
#    -a program="$PROGRAM" \
#    -a stdout='.+' \
#    -a exit_code=0 \
#    -a arguments="['-o', '/dev/stdout', 'empty.cl']" \
#    test command_regex.ExecTest
