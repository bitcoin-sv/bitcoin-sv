#!/usr/bin/python3
# Perform the EC tests on SVs
import subprocess
import os
import pathlib
import traceback
import pipetestutils

def main():
    r1 = r2 = r3 = -1
    
    try:
        os.chdir("src/secp256k1")
        args = ["make", pipetestutils.nproc()]
        r1 = subprocess.call(args)
    except Exception as e:
        print("make problem, quitting")
        print("type error: " + str(e))
        print(traceback.format_exc())
        exit(-1)

    try:
        args = ["./exhaustive_tests"]
        r2 = subprocess.call(args)
    except Exception as e:
        print("Problem running exhaustive tests")
        print("type error: " + str(e))
        print(traceback.format_exc())
        exit(-2)

    try:
        args = ["./tests"]
        r3 = subprocess.call(args)
    except Exception as e:
        print("Problem running tests")
        print("type error: " + str(e))
        print(traceback.format_exc())
        exit(-3)
        
    exit(abs(r1) + abs(r2) + abs(r3))

if __name__ == '__main__':
    main()
