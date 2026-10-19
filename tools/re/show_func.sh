#!/bin/bash
# show_func.sh ADDR [N] : print the first N PPC instructions (comments) of sub_ADDR from the generated C++.
G=${G:-$HOME/mnt/fm4-recomp/.local/work/fm4/generated/default}
a=${1^^}; n=${2:-14}
f=$(grep -l "^DEFINE_REX_FUNC(sub_$a)" $G/fm4_recomp.${a:0:4}*.cpp 2>/dev/null | head -1)
echo "=== sub_$a ($(basename "$f"))"
[ -n "$f" ] && awk -v s="DEFINE_REX_FUNC(sub_$a)" -v n=$n 'index($0,s)==1 {p=1} p && /^\t\/\/ / {sub(/^\t\/\/ /,"  "); print; c++} p && /ctx.current_instruction = / {} c>=n {exit}' "$f"
