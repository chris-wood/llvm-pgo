#!/bin/bash
echo Running program: $1
compilerArgs=$1
var=$2
count=$3
for i in ${@:4}
do
	echo ----------------------------------------
	echo *** Size: $i
	echo ----------------------------------------
    ./clang -03 -emit-llvm $var.c -c -o $var.bc $compilerArgs 2>&1>> $var.$i.build
    ./opt -insert-edge-profiling $var.bc -o $var.profile.bc 2>&1>> $var.$i.build
	./llc $var.profile.bc -o $var.profile.s 2>&1>> $var.$i.build
	./clang -o $var.profile $var.profile.s ../lib/libprofile_rt.so $compilerArgs 2>&1>> $var.$i.build

	# Gather the profile data and output, for peace of mind
	./$var.profile $i > $var.profile.out 2>&1>> $var.$i.dump
	./llvm-prof $var.profile.bc 2>&1>> $var.$i.dump

	# Now do the block placement and gather the results...
	./opt -profile-loader -block-placement $var.bc 2>&1>> $var.$i.optbuild
	./llc $var.bc -o $var.mod.s 2>&1>> $var.$i.optbuild
	./clang -o $var.mod $var.mod.s ../lib/libprofile_rt.so $compilerArgs 2>&1>> $var.$i.optbuild
	./$var.mod $i > $var.mod.out 2>&1>> $var.$i.dump
	./clang -o $var $var.c $compilerArgs 2>&1>> $var.$i.optbuild

	# Now run the program with the time script and save the output
	COUNTER=0
	while [  $COUNTER -lt $count ]; do
		echo Running iteration $COUNTER with size $i and storing the time...
		perl time.pl $var      $i > $var.out.$COUNTER
		perl time.pl $var.mod  $i > $var.mod.out.$COUNTER
		let COUNTER=COUNTER+1
	done
done

