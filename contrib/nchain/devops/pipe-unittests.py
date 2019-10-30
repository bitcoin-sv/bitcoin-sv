#!/usr/bin/python3
# Perform the unit tests on SV
import subprocess
import os
import pathlib
import traceback
import pipetestutils

def main():
    r1 = -1

    try:
        pathlib.Path("build/reports").mkdir(parents=True, exist_ok=True)
        os.chdir("src/test")
    except Exception as e:
        print("Problem changing directory")
        print("type error: " + str(e))
        print(traceback.format_exc())
        exit(-1)
        
    try:
        args = ["./test_bitcoin", "--log_format=JUNIT" \
               , "--log_sink=../../build/reports/unittests.xml"]
        r1 = subprocess.call(args)
    except Exception as e:
        print("Problem running tests")
        print("type error: " + str(e))
        print(traceback.format_exc())
        exit(-2)
        
    exit(abs(r1))

if __name__ == '__main__':
    main()
