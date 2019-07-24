#!/usr/bin/python3
# Perform the univalue tests on SV
import subprocess
import os
import pathlib
import traceback
import pipetestutils

def main():
    r1 = r2 = r3 = r4 = -1
    
    try:
        os.chdir("src/univalue")
        args = ["make", pipetestutils.nproc()]
        r1 = subprocess.call(args) 
    except Exception as e:
        print("make problem")
        print("type error: " + str(e))
        print(traceback.format_exc())
        exit(-1)

    try:
        args = ["test/object"]
        r2 = subprocess.call(args)     
    except Exception as e:
        print("Problem running object tests")
        print("type error: " + str(e))
        print(traceback.format_exc())
        exit(-2)

    try:
        args = ["test/no_nul"]
        r3 = subprocess.call(args)
    except Exception as e:
        print("Problem running no_null tests")
        print("type error: " + str(e))
        print(traceback.format_exc())
        exit(-3)
        
    try:
        args = ["test/unitester"]
        r4 = subprocess.call(args)
    except Exception as e:
        print("Problem running unitester tests")
        print("type error: " + str(e))
        print(traceback.format_exc())
        exit(-4)
        
    exit(abs(r1) + abs(r2) + abs(r3) + abs(r4))

if __name__ == '__main__':
    main()
