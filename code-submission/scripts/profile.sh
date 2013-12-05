#!/bin/bash
#
# File: profile.sh
# Description:  Compiles, instruments and runs the program to
# 		gather profile data. The enabled type of profiling is
# 		edge-profiling of LLVM in version 3.3. All intermediate
# 		steps (compiled bitcode, instrumented bitcode) are also
# 		saved in human readable format (*.s) for manual inspection
# 		and debugging purposes.
# Author: Julian Lettner
# Usage: profile.sh <path to program source> <input size>
#    ex: ./profile.sh ~/some/path/kmeans.c 1000
#    
if [[ -z "$1" || -z "$2" ]]; then
	echo usage: $0 "source.c input-size"
	exit	
fi

path="$1"
file_ext=$(basename "$path")
filename="${file_ext%.*}"
n_input="$2"

./clean.sh "$filename"

# This is duplicated in clean.sh
file_bc="$filename.bc"
file_profile_bc="$filename.profile.bc"
file_profile="$filename.profile"

profiling="-insert-edge-profiling -insert-gcov-profiling"
# -insert-path-profiling and -insert-optimal-edge-profiling give errors ...

# Compile file to readable format too, so that we can compare input and output.
./clang -S -c -O1 -emit-llvm "$path" -o "$filename.s"

./clang -c -O1 -emit-llvm "$path" -o $file_bc
./opt $profiling $file_bc -o $file_profile_bc

./clang -o $file_profile $file_profile_bc ../../build/Debug+Asserts/lib/libprofile_rt.dylib

./$file_profile $n_input

./llvm-prof $file_bc
