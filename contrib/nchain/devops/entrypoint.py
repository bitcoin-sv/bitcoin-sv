#!/usr/bin/python3
import subprocess
import shlex
import platform

def do_release_notes():
    rawargs = 'git log --pretty=oneline --grep "CORE-[0-9][0-9]"'
    args = shlex.split(rawargs)
    out = subprocess.check_output(args)
    notes = out.split(b"\n")
    with open("release-notes.txt", "w") as rn:
        for x in notes:
            print(x, file=rn)


def do_windows_build():
    pass

def do_linux_build():
    args = ['./autogen.sh']
    try:
        subprocess.call(args)
    except:
        exit(-1)

    args = ['./configure', '--enable-zmq']
    try:
        subprocess.call(args)
    except:
        exit(-1)

    
    args = ['make']
    try:
        subprocess.call(args)
    except:
        exit(-1)

def main():
    do_release_notes()

    # do the building
    s = platform.system()
    print("Building for {}".format(s))
    if s == "Linux":
        do_linux_build()
    else:
        do_windows_build()


if __name__ == '__main__':
    main()
