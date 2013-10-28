#!/bin/bash
echo Running script: "$0"
program="$1"
compilerArgs="$2"
count="$3"

# Storage for the last size param
first="$4"
last=0

# Run the normal experiment, where each input size is used to compare the unoptimized/optimized versions
for i in ${@:4}
do
	echo ----------------------------------------
	echo ----------------------------------------
	echo ----------------------------------------
	echo *** Size: $i
	echo *** $program $compilerArgs $count
	echo ----------------------------------------
	echo ----------------------------------------
	echo ----------------------------------------
	./clang -O3 -emit-llvm $program.c -c -o $program.bc $compilerArgs >> $program.$i.build 2>&1
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
	./clang -O3 -o $program $program.c $compilerArgs >> $program.$i.optbuild 2>&1

	# Now run the program with the time script and save the output
	COUNTER=0
	while [  $COUNTER -lt $count ]; do
		echo *** Running iteration $COUNTER with size $i and storing the time...
		perl time.pl $program      $i > $program.out.$i.$COUNTER
		perl time.pl $program.mod  $i > $program.mod.out.$i.$COUNTER
		let COUNTER=COUNTER+1
	done
	# Store the last size used for the subsequent part of the experiment
	last=$i 
done

# FIRST SIZE COMPARISON
echo ----------------------------------------
echo ----------------------------------------
echo ----------------------------------------
echo *** Optimizing for size $first and running all sizes
echo $program $compilerArgs $count
echo ----------------------------------------
echo ----------------------------------------
echo ----------------------------------------
./clang -O3 -emit-llvm $program.c -c -o $program.bc $compilerArgs >> $program.first.build 2>&1
./opt -insert-edge-profiling $program.bc -o $program.profile.bc >> $program.first.build 2>&1
./llc $program.profile.bc -o $program.profile.s >> $program.first.build 2>&1
./clang -o $program.profile $program.profile.s ../lib/libprofile_rt.so $compilerArgs >> $program.first.build 2>&1

# Gather the profile data and output, for peace of mind
#### THIS IS WHERE PROFILE DATA IS GATHERED FOR THE SIZE
./$program.profile $first > $program.profile.out >> $program.first.dump 2>&1
./llvm-prof $program.profile.bc >> $program.first.dump 2>&1

# Now do the block placement and gather the results...
./opt -profile-loader -block-placement $program.bc >> $program.first.optbuild 2>&1
./llc $program.bc -o $program.mod.s >> $program.first.optbuild 2>&1
./clang -o $program.mod $program.mod.s ../lib/libprofile_rt.so $compilerArgs >> $program.first.optbuild 2>&1
./$program.mod $first > $program.mod.out >> $program.first.dump 2>&1
./clang -O3 -o $program $program.c $compilerArgs >> $program.first.optbuild 2>&1

# Now run the program with the time script and save the output
for i in ${@:4}
do
	COUNTER=0
	while [  $COUNTER -lt $count ]; do
		echo *** Running iteration $COUNTER with size $i and storing the time...
		perl time.pl $program      $i > $program.first.out.$i.$COUNTER
		perl time.pl $program.mod  $i > $program.first.mod.out.$i.$COUNTER
		let COUNTER=COUNTER+1
	done
done

# LAST SIZE COMPARISON
echo ----------------------------------------
echo ----------------------------------------
echo ----------------------------------------
echo *** Optimizing for size $last and running all sizes
echo $program $compilerArgs $count
echo ----------------------------------------
echo ----------------------------------------
echo ----------------------------------------
./clang -O3 -emit-llvm $program.c -c -o $program.bc $compilerArgs >> $program.last.build 2>&1
./opt -insert-edge-profiling $program.bc -o $program.profile.bc >> $program.last.build 2>&1
./llc $program.profile.bc -o $program.profile.s >> $program.last.build 2>&1
./clang -o $program.profile $program.profile.s ../lib/libprofile_rt.so $compilerArgs >> $program.last.build 2>&1

# Gather the profile data and output, for peace of mind
#### THIS IS WHERE PROFILE DATA IS GATHERED FOR THE SIZE
./$program.profile $last > $program.profile.out >> $program.last.dump 2>&1
./llvm-prof $program.profile.bc >> $program.last.dump 2>&1

# Now do the block placement and gather the results...
./opt -profile-loader -block-placement $program.bc >> $program.last.optbuild 2>&1
./llc $program.bc -o $program.mod.s >> $program.last.optbuild 2>&1
./clang -o $program.mod $program.mod.s ../lib/libprofile_rt.so $compilerArgs >> $program.last.optbuild 2>&1
./$program.mod $last > $program.mod.out >> $program.last.dump 2>&1
./clang -O3 -o $program $program.c $compilerArgs >> $program.last.optbuild 2>&1

# Now run the program with the time script and save the output
for i in ${@:4}
do
	COUNTER=0
	while [  $COUNTER -lt $count ]; do
		echo *** Running iteration $COUNTER with size $i and storing the time...
		perl time.pl $program      $i > $program.last.out.$i.$COUNTER
		perl time.pl $program.mod  $i > $program.last.mod.out.$i.$COUNTER
		let COUNTER=COUNTER+1
	done
done

