#!/bin/bash
#
# File: runpgo.sh
# Description:  Runs the PGO loop unroll pass on the specified
#     program's bitcode file (which must be present, i.e., run
#     profile.sh before). Creates an executable and human readable
#     version of the optimized program. Add the -simple flag to
#     run the simple PGO loop unroll pass.
# Author: Julian Lettner
# Usage: runpgo.sh <program name (without extension)> [-simple]
#    ex: ./runpgo.sh kmeans -simple
# 
if [ -z "$1" ]; then
	echo usage: $0 "file.bc [-simple]"
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

# $2 might be -simple or empty
./opt -S $profiling_options $debug_options -load PgoLoopUnroll.dylib "$2-pgo-loop-unroll" < "$file_ext" > "$filename-pgo.s"

# Also create an executable so that we can run it for timing and making sure it prints the right output.
./llvm-as "$filename-pgo.s" -o "$filename-pgo.bc"
./clang "$filename-pgo.bc" -o "$filename-pgo"

#./$filename-pgo
