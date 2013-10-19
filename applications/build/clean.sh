#!/bin/bash
if [ -z "$1" ]; then
	echo usage: $0 "filename (without extension)"
	exit	
fi

filename="$1"

file_bc="$filename.bc"
file_profile_bc="$filename.profile.bc"
file_profile="$filename.profile"
file_profile_pgo_s="$filename-pgo.s"

rm $file_bc
rm $file_profile_bc
rm $file_profile
rm $file_profile_pgo_s
rm "llvmprof.out"

echo "Cleaned up for $filename".