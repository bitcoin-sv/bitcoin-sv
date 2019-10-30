#!/usr/bin/python3
# Perform the util tests on SVs
import subprocess
import pipetestutils
import traceback

def main():
    r1 = -1

    try:
        args = ["python3", "test/util/bitcoin-util-test.py", "--verbose"]
        with open("build/reports/util-test.log","w") as outfile:
            r1 = subprocess.call(args, stdout=outfile)
    except Exception as e:
        print("Problem running make check")
        print("type error: " + str(e))
        print(traceback.format_exc())
        exit(-1)

    exit(abs(r1))

if __name__ == '__main__':
    main()
