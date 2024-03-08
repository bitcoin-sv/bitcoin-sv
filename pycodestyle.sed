# Copyright (c) 2023 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

# Usage
# shopt -s globstar
# sed -i -f pycodestyle.sed test/functional/**/*.py
# pycodestyle test/functional | awk '{ print  }' | sort | uniq -c | sort -nr

s:\([[({]\)\s\+:\1:g   # E201 whitespace after e.g.  '( ' -> '('
s:\(\S\)\s\+\([])}]\):\1\2:g   # E202 whitespace before e.g. ' )' -> ')'

s/\t/    /g          # W191 tabs to 4 spaces
s/\s\+$//            # W291 remove trailing whitespace

