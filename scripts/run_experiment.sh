#!/bin/bash

#
# File: run_experiment.sh
# Description: Run the specified optimization pass experiment using the following
#    hardcoded set of input sizes and applications. It is assumed that all application
#    source files are in the same directory as this script.
# Author: Christopher A. Wood, woodc1@uci.edu
# Usage: run_experiment.sh <optimization_pass_prefix>
#    ex: ./run_experiment.sh run_basicblock
#

echo ----------------------------------------  
echo Running experiment for optimization: $1
echo ----------------------------------------

./$1.sh is_prime is_prime "" 5 65536 131072 262144 524288 1048576
./$1.sh block_test block_test "" 5 5000 7500 10000 12500 15000
./$1.sh blocked blocked "" 5 900 1000 1100 1200 1300 1400
./$1.sh factor factor "" 5 2000000 3000000 4000000 5000000 6000000
./$1.sh bubble bubble "" 5 50000 100000 150000 200000 250000
./$1.sh quicksort quicksort "" 5 50000 100000 150000 200000 250000
./$1.sh kmeans kmeans "-lm" 5 "0" "1" "2" "3" "4"
./$1.sh permutations permutations "" 5 8 9 10 11 12
./$1.sh mandlebrot_seq mandlebrot_seq "" 5 1000 1250 1500 1750 2000 2250
./$1.sh fft fft "-std=c99 -lm" 5 128 256 1024 4096 8192
./$1.sh huffman_priority_queue huffman_priority_queue "" 5 100 1000 10000 100000 1000000
./$1.sh huffman huffman "" 5 100 1000 10000 100000 1000000
./$1.sh combinations combinations "" 5 13 14 15 16 17
./$1.sh conjugate_transpose conjugate_transpose "-lm" 5 "16" "512" "1024" "2048"
./$1.sh cholesky cholesky "-std=c99 -lm" 5 65536 262144 524288 786432 1310720
./$1.sh deconvolution deconvolution "-std=c99 -lm" 5 256 512 1024 2048
./$1.sh euler_method euler_method "-std=c99 -lm" 5 16384 32768 65536 98304 131072
./$1.sh nbody nbody "" 5 10000000 20000000 30000000 40000000 50000000
./$1.sh lucas lucas "-lm" 5 75000 100000 125000 150000 175000
./$1.sh aes aes "" 5 250000 500000 750000 1000000 1250000
