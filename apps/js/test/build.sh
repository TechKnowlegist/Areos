#!/bin/sh
# Builds `jsrun`, a command-line test of the JavaScript side on a normal Linux computer:
#   ./build.sh && ./jsrun http://127.0.0.1:8000/ pump:20 render console
# (commands: pump:N render click:ID input:ID:text key:ID:Key console wait:MS). Plain http only.
set -e
H=$(cd "$(dirname "$0")" && pwd); T=$H/..
sh $T/prelude/build.sh $H/prelude.js
python3 -c "
d=open('$H/prelude.js','rb').read()
open('$H/prelude_data.c','w').write('const char jsh_prelude_start[] = {'+','.join(str(b) for b in d)+',0};\n')"
cd $H
gcc -O1 -g -w -D_GNU_SOURCE -DCONFIG_VERSION='"2025-04-26"' -I$T -o jsrun jsrun.c $T/jshost.c prelude_data.c $T/quickjs.c $T/libregexp.c $T/libunicode.c $T/cutils.c $T/dtoa.c -lm -lpthread
