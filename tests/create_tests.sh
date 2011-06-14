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
qmtest create-tdb
qmtest register test command_regex.ExecTest

qmtest create -i cunit.all \
    -a program=../build/onlineclc-test \
    -a stdout='.+' \
    -a exit_code=0 \
    test command_regex.ExecTest

qmtest create -i cmdparse.help \
    -a program=../build/onlineclc \
    -a stdout='Usage:.*' \
    -a exit_code=0 \
    -a arguments="['--help']" \
    test command_regex.ExecTest
qmtest create -i cmdparse.noargs \
    -a program=../build/onlineclc \
    -a stderr='Source file not specified\n.*' \
    -a exit_code=2 \
    -a arguments="[]" \
    test command_regex.ExecTest
qmtest create -i cmdparse.bad_device \
    -a program=../build/onlineclc \
    -a stderr='No OpenCL device called `bad'\'' found' \
    -a exit_code=1 \
    -a arguments="['-b', 'bad', 'empty.cl']" \
    test command.ExecTest
qmtest create -i compile.empty \
    -a program=../build/onlineclc \
    -a exit_code=0 \
    -a arguments="['empty.cl']" \
    test command.ExecTest
qmtest create -i compile.quotes \
    -a program=../build/onlineclc \
    -a exit_code=0 \
    -a arguments="['\"quotes\".cl']" \
    test command.ExecTest
qmtest create -i compile.nosource \
    -a program=../build/onlineclc \
    -a stderr="Failed to open \`nosource.cl': No such file or directory" \
    -a exit_code=1 \
    -a arguments="['nosource.cl']" \
    test command.ExecTest
qmtest create -i compile.invalid \
    -a program=../build/onlineclc \
    -a stderr='.+' \
    -a exit_code=1 \
    -a arguments="['invalid.cl']" \
    test command_regex.ExecTest
qmtest create -i compile.invalid_output \
    -a program=../build/onlineclc \
    -a stderr="Failed to open \`bad/bad.out': No such file or directory" \
    -a exit_code=1 \
    -a arguments="['-o', 'bad/bad.out', 'empty.cl']" \
    test command.ExecTest

# Doesn't pass because stdout is a pipe
#qmtest create -i compile.log_stdout \
#    -a program=../build/onlineclc \
#    -a stdout='.+' \
#    -a exit_code=0 \
#    -a arguments="['-o', '/dev/stdout', 'empty.cl']" \
#    test command_regex.ExecTest
