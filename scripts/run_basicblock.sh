#!/bin/bash
echo Running program: $1
var=$1
count=$2
for i in ${@:3}
do
	echo ----------------------------------------
	echo *** Size: $i
	echo ----------------------------------------
    ./clang -03 -emit-llvm $var.c -c -o $var.bc
    ./opt -insert-edge-profiling $var.bc -o $var.profile.bc
	./llc $var.profile.bc -o $var.profile.s
	./clang -o $var.profile $var.profile.s ../lib/libprofile_rt.so

	# Gather the profile data and output, for peace of mind
	./$var.profile $i > $var.profile.out
	./llvm-prof $var.profile.bc

	# Now do the block placement and gather the results...
	./opt -profile-loader -block-placement $var.bc
	./llc $var.bc -o $var.mod.s
	./clang -o $var.mod $var.mod.s ../lib/libprofile_rt.so
	./$var.mod $i > $var.mod.out
	./clang -o $var $var.c

	# Now run the program with the time script and save the output
	COUNTER=0
	while [  $COUNTER -lt $count ]; do
		echo Running iteration $COUNTER with size $i and storing the time...
		perl time.pl $var      $i > $var.out.$i.$COUNTER
		perl time.pl $var.mod  $i > $var.mod.out.$i.$COUNTER
		let COUNTER=COUNTER+1
	done
done

