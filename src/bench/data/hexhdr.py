from itertools import cycle, chain, repeat, starmap

def hexhdr(source, output, name):
  separators = chain(("", ), cycle(chain(repeat(", ", 7), (",\n", ))))
  with open(source, "rb") as src:
    with open(output, "w+t") as dst:
      print("hexhdr: processing {0}".format(source))
      dst.write("static unsigned const char {0}[] = {{\n".format(name))
      dst.writelines(starmap("{0}0x{1:02x}".format, zip(separators, src.read())))
      dst.write("\n};\n")

if __name__ == "__main__":
  import sys, os
  if len(sys.argv) <= 1:
    print("{} infile [outfile [varname]]".format(sys.argv[0]))
    exit(1)

  hexhdr(
    source = sys.argv[1],
    output = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1] + ".h",
    name =   sys.argv[3] if len(sys.argv) > 3 else os.path.splitext(os.path.basename(sys.argv[1]))[0],
  )
