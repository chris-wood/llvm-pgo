#!/bin/bash
if [ -z "$1" ]; then
	echo usage: $0 "file.bc"
	exit	
fi

path="$1"
file_ext=$(basename "$path")
filename="${file_ext%.*}"

profiling_options="-profile-loader -profile-metadata-loader -profile-verifier"

./opt $profiling_options -S -load PgoLoopUnroll.dylib -pgo-loop-unroll < "$file_ext" > "$filename-pgo.s"
