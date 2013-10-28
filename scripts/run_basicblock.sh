#!/bin/bash
echo Running script: "$0"
program="$1"
compilerArgs="$2"
count="$3"
for i in ${@:5}
do
	echo ----------------------------------------
	echo *** Size: $i
	echo ----------------------------------------
	echo $program $compilerArgs $count
	./clang -03 -emit-llvm $program.c -c -o $program.bc $compilerArgs >> $program.$i.build 2>&1
	./opt -insert-edge-profiling $program.bc -o $program.profile.bc >> $program.$i.build 2>&1
	./llc $program.profile.bc -o $program.profile.s >> $program.$i.build 2>&1
	./clang -o $program.profile $program.profile.s ../lib/libprofile_rt.so $compilerArgs >> $program.$i.build 2>&1

	# Gather the profile data and output, for peace of mind
	./$program.profile $i > $program.profile.out >> $program.$i.dump 2>&1
	./llvm-prof $program.profile.bc >> $program.$i.dump 2>&1

	# Now do the block placement and gather the results...
	./opt -profile-loader -block-placement $program.bc >> $program.$i.optbuild 2>&1
	./llc $program.bc -o $program.mod.s >> $program.$i.optbuild 2>&1
	./clang -o $program.mod $program.mod.s ../lib/libprofile_rt.so $compilerArgs >> $program.$i.optbuild 2>&1
	./$program.mod $i > $program.mod.out >> $program.$i.dump 2>&1
	./clang -o $program $program.c $compilerArgs >> $program.$i.optbuild 2>&1

	# Now run the program with the time script and save the output
	COUNTER=0
	while [  $COUNTER -lt $count ]; do
		echo Running iteration $COUNTER with size $i and storing the time...
		perl time.pl $program      $i > $program.out.$i.$COUNTER
		perl time.pl $program.mod  $i > $program.mod.out.$i.$COUNTER
		let COUNTER=COUNTER+1
	done
done

