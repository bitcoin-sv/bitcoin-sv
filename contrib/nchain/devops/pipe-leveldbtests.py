#!/usr/bin/python3
# Perform the leveldb  tests on SVs
import subprocess
import os
import pathlib
import traceback
import pipetestutils

def main():
    r2 = -1
    try:
        os.chdir("src/leveldb")
        args = ["make", pipetestutils.nproc(), "check"]
        r1 = subprocess.call(args)
    except Exception as e:
        print("Problem running make check")
        print("type error: " + str(e))
        print(traceback.format_exc())
        exit(-1)
            
    exit(abs(r1))


if __name__ == '__main__':
    main()
