#! /bin/bash

if ! [[ -x shell ]]; then
    echo "shell executable does not exist"
    exit 1
fi

./tester/run-tests.sh $*

