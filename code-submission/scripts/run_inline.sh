#!/bin/bash

# Run the PGO inline optimization pass.

echo Running script: "$0"
program="$1"
execName="$2"
compilerArgs="$3"
count="$4"

# Storage for the last size param
first="$5"
last=0

# Run the normal experiment, where each input size is used to compare the unoptimized/optimized versions
for i in "${@:5}"
do
	echo ----------------------------------------
	echo ----------------------------------------
	echo ----------------------------------------
	echo Size: $i
	echo $execName $compilerArgs $count
	echo ----------------------------------------
	echo ----------------------------------------
	echo ----------------------------------------
	./clang -O0 -emit-llvm -o $execName.bc -c $program.c $compilerArgs >> $execName.${i// /_}.build 2>&1
	./opt -insert-edge-profiling $execName.bc -o $execName.profile.bc >> $execName.${i// /_}.build 2>&1
	./llc $execName.profile.bc -o $execName.profile.s >> $execName.${i// /_}.build 2>&1
	./clang -o $execName.profile $execName.profile.s ../lib/libprofile_rt.so $compilerArgs >> $execName.${i// /_}.build 2>&1

	# Gather the profile data and output, for peace of mind
	./$execName.profile $i > $execName.profile.out >> $execName.${i// /_}.dump 2>&1
	./llvm-prof $execName.profile.bc >> $execName.${i// /_}.dump 2>&1

	# Now do the pgo inlining and gather the results...
	./opt -profile-loader -inline $execName.bc >> $execName.${i// /_}.optbuild 2>&1
	./llc $execName.bc -o $execName.mod.s >> $execName.${i// /_}.optbuild 2>&1
	./clang -O0 -o $execName.mod $execName.mod.s ../lib/libprofile_rt.so $compilerArgs >> $execName.${i// /_}.optbuild 2>&1
	./$execName.mod $i > $execName.mod.out >> $execName.${i// /_}.dump 2>&1
	./clang -inline -o $execName $program.c $compilerArgs >> $execName.${i// /_}.optbuild 2>&1

	# Now run the program with the time script and save the output
	COUNTER=0
	while [  $COUNTER -lt $count ]; do
		echo Running iteration $COUNTER with size $i and storing the time...
		perl time.pl $execName      $i > $execName.out.${i// /_}.$COUNTER
		perl time.pl $execName.mod  $i > $execName.mod.out.${i// /_}.$COUNTER
		let COUNTER=COUNTER+1
	done
	# Store the last size used for the subsequent part of the experiment
	last=$i 
done

# FIRST SIZE COMPARISON
echo ----------------------------------------
echo ----------------------------------------
echo ----------------------------------------
echo Optimizing for size $first and running all sizes
echo $execName $compilerArgs $count
echo ----------------------------------------
echo ----------------------------------------
echo ----------------------------------------
./clang -O0 -emit-llvm $program.c -c -o $execName.bc $compilerArgs >> $execName.first.build 2>&1
./opt -insert-edge-profiling $execName.bc -o $execName.profile.bc >> $execName.first.build 2>&1
./llc $execName.profile.bc -o $execName.profile.s >> $execName.first.build 2>&1
./clang -o $execName.profile $execName.profile.s ../lib/libprofile_rt.so $compilerArgs >> $execName.first.build 2>&1

# Gather the profile data and output, for peace of mind
#### THIS IS WHERE PROFILE DATA IS GATHERED FOR THE SIZE
./$execName.profile $first > $execName.profile.out >> $execName.first.dump 2>&1
./llvm-prof $execName.profile.bc >> $execName.first.dump 2>&1

# Now do the inlining and gather the results...
./opt -profile-loader -inline $execName.bc >> $execName.first.optbuild 2>&1
./llc $execName.bc -o $execName.mod.s >> $execName.first.optbuild 2>&1
./clang -O0 -o $execName.mod $execName.mod.s ../lib/libprofile_rt.so $compilerArgs >> $execName.first.optbuild 2>&1
./$execName.mod $first > $execName.mod.out >> $execName.first.dump 2>&1
./clang -inline -o $execName $program.c $compilerArgs >> $execName.first.optbuild 2>&1
rm llvmprof.out			# remove the profiling data after using it to optimize

# Now run the program with the time script and save the output
for i in "${@:5}"
do
	COUNTER=0
	while [  $COUNTER -lt $count ]; do
		echo Running iteration $COUNTER with size $i and storing the time...
		perl time.pl $execName      $i > $execName.first.out.${i// /_}.$COUNTER
		perl time.pl $execName.mod  $i > $execName.first.mod.out.${i// /_}.$COUNTER
		let COUNTER=COUNTER+1
	done
done

# LAST SIZE COMPARISON
echo ----------------------------------------
echo ----------------------------------------
echo ----------------------------------------
echo Optimizing for size $last and running all sizes
echo $execName $compilerArgs $count
echo ----------------------------------------
echo ----------------------------------------
echo ----------------------------------------
./clang -O0 -emit-llvm $program.c -c -o $execName.bc $compilerArgs >> $execName.last.build 2>&1
./opt -insert-edge-profiling $execName.bc -o $execName.profile.bc >> $execName.last.build 2>&1
./llc $execName.profile.bc -o $execName.profile.s >> $execName.last.build 2>&1
./clang -o $execName.profile $execName.profile.s ../lib/libprofile_rt.so $compilerArgs >> $execName.last.build 2>&1

# Gather the profile data and output, for peace of mind
#### THIS IS WHERE PROFILE DATA IS GATHERED FOR THE SIZE
./$execName.profile $last > $execName.profile.out >> $execName.last.dump 2>&1
./llvm-prof $execName.profile.bc >> $execName.last.dump 2>&1

# Now do the inlining and gather the results...
./opt -profile-loader -inline $execName.bc >> $execName.last.optbuild 2>&1
./llc $execName.bc -o $execName.mod.s >> $execName.last.optbuild 2>&1
./clang -o $execName.mod $execName.mod.s ../lib/libprofile_rt.so $compilerArgs >> $execName.last.optbuild 2>&1
./$execName.mod $last > $execName.mod.out >> $execName.last.dump 2>&1
./clang -inline -o $execName $program.c $compilerArgs >> $execName.last.optbuild 2>&1
rm llvmprof.out			# remove the profiling data after using it to optimize

# Now run the program with the time script and save the output
for i in "${@:5}"
do
	COUNTER=0
	while [  $COUNTER -lt $count ]; do
		echo Running iteration $COUNTER with size $i and storing the time...
		perl time.pl $execName      $i > $execName.last.out.${i// /_}.$COUNTER
		perl time.pl $execName.mod  $i > $execName.last.mod.out.${i// /_}.$COUNTER
		let COUNTER=COUNTER+1
	done
done

