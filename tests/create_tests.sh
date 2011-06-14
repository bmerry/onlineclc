#!/bin/sh
set -e
rm -f QMTest/configuration QMTest/*.pyc QMTest/*.qm*
qmtest create-tdb
qmtest register test command_regex.ExecTest
qmtest create -i cmdparse.help -a program=../build/onlineclc -a stdout='Usage:.*' -a exit_code=0 -a arguments="['--help']" test command_regex.ExecTest
qmtest create -i cmdparse.noargs -a program=../build/onlineclc -a stderr='Source file not specified\n.*' -a exit_code=2 -a arguments="[]" test command_regex.ExecTest
qmtest create -i cmdparse.bad_device -a program=../build/onlineclc -a stderr='No OpenCL device called `bad'\'' found' -a exit_code=1 -a arguments="['-b', 'bad', 'empty.cl']" test command.ExecTest
