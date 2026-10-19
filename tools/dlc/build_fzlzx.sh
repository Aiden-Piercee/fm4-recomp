#!/bin/sh
# Builds libfzlzx.so (LZX decoder for Forza method-21 zips) from rexglue-sdk's libmspack.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
M="$HERE/../../.local/rexglue-sdk/thirdparty/libmspack/libmspack/mspack"
gcc -O2 -shared -fPIC -I"$M" "$HERE/fzlzx.c" "$M/lzxd.c" -o "${1:-$HERE/libfzlzx.so}"
echo "built ${1:-$HERE/libfzlzx.so}"
