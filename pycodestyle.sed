# Usage
# shopt -s globstar
# sed -i -f pycodestyle.sed test/functional/**/*.py
# pycodestyle test/functional | awk '{ print  }' | sort | uniq -c | sort -nr

#s/\(\S\)( \+/\1(/g   # E201
#s/\(\S\) \+)/\1)/g   # E202

# s/\([,;:]\)\(\S\)/\1 \2/g  # E231 missing whitespace after ,;: tricky!

s/\t/    /g          # W191 tabs to 4 spaces
#s/\s\+$//            # W291 remove trailing whitespace

