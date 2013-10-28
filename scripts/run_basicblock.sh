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
    ./clang -03 -emit-llvm $var.c -c -o $var.bc $compilerArgs >> $var.$i.build 2>&1
    ./opt -insert-edge-profiling $var.bc -o $var.profile.bc >> $var.$i.build 2>&1
	./llc $var.profile.bc -o $var.profile.s >> $var.$i.build 2>&1
	./clang -o $var.profile $var.profile.s ../lib/libprofile_rt.so $compilerArgs >> $var.$i.build 2>&1

	# Gather the profile data and output, for peace of mind
	./$var.profile $i > $var.profile.out >> $var.$i.dump 2>&1
	./llvm-prof $var.profile.bc >> $var.$i.dump 2>&1

	# Now do the block placement and gather the results...
	./opt -profile-loader -block-placement $var.bc >> $var.$i.optbuild 2>&1
	./llc $var.bc -o $var.mod.s >> $var.$i.optbuild 2>&1
	./clang -o $var.mod $var.mod.s ../lib/libprofile_rt.so $compilerArgs >> $var.$i.optbuild 2>&1
	./$var.mod $i > $var.mod.out >> $var.$i.dump 2>&1
	./clang -o $var $var.c $compilerArgs >> $var.$i.optbuild 2>&1

	# Now run the program with the time script and save the output
	COUNTER=0
	while [  $COUNTER -lt $count ]; do
		echo Running iteration $COUNTER with size $i and storing the time...
		perl time.pl $var      $i > $var.out.$i.$COUNTER
		perl time.pl $var.mod  $i > $var.mod.out.$i.$COUNTER
		let COUNTER=COUNTER+1
	done
done

