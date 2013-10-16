#!/bin/bash
if [ -z "$1" ]; then
	echo usage: $0 "file.bc"
	exit	
fi

path="$1"
file_ext=$(basename "$path")
filename="${file_ext%.*}"

./opt -S -load PgoLoopUnroll.dylib -pgo-loop-unroll < "$file_ext" > "$filename-opt.s"
