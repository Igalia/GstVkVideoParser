#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"
source $SCRIPT_DIR/valgrind.env

set -x
#valgrind $VALGRIND_OPTS $*
valgrind $VALGRIND_OPTS $* > valgrind_stdout.txt 2>&1
