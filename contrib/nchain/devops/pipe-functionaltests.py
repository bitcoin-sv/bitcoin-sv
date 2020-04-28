#!/usr/bin/python3
# Perform the functional tests on SV
import subprocess
import os
import pathlib
import traceback
import pipetestutils

def main():
    r1 = -1

    try:
        args = ["python3", "test/functional/test_runner.py" \
                , "--extended", "--large-block-tests" \
                , "--junitouput=build/reports/func-tests.xml", "--jobs=2"]
        r1 = subprocess.call(args)
    except Exception as e:
        print("Problem running tests")
        print("type error: " + str(e))
        print(traceback.format_exc())
        exit(-1)

    print("functional tests completed with result code {}".format(r1))

    exit(abs(r1))

if __name__ == '__main__':
    main()
