#!/bin/bash
if [ -z "$1" ]; then
	echo usage: $0 "file.bc"
	exit	
fi

path="$1"
file_ext=$(basename "$path")
filename="${file_ext%.*}"

profiling_options="-profile-loader -profile-metadata-loader -profile-verifier"

# enable all debug messages
# debug_options="-debug"
# 
# enable debug messages only in the PgoLoopUnrollPass
# corresponds to #define DEBUG_TYPE "pgo-loop-unroll"
debug_options="-debug-only=pgo-loop-unroll"

./opt -S $profiling_options $debug_options -load PgoLoopUnroll.dylib -pgo-loop-unroll < "$file_ext" > "$filename-pgo.s"
