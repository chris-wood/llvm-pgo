#!/bin/bash

echo ----------------------------------------  
echo Running experiment for optimization: $1
echo ----------------------------------------

./$1.sh is_prime is_prime "" 5 65536 131072 262144 524288 1048576
./$1.sh block_test block_test "" 5 5000 7500 10000 12500 15000
./$1.sh blocked blocked "" 5 900 1000 1100 1200 1300 1400
./$1.sh factor factor "" 5 2000000 3000000 4000000 5000000 6000000
./$1.sh bubble bubble "" 5 50000 100000 150000 200000 250000
./$1.sh quicksort quicksort "" 5 50000 100000 150000 200000 250000
./$1.sh kmeans kmeans "-lm" 5 "10000 10" "100000 10" "1000000 10" "1000000 20" "1000000 50"
./$1.sh permutations permutations "" 5 8 9 10 11 12
./$1.sh mandlebrot_seq mandlebrot_seq "" 5 1000 1250 1500 1750 2000 2250
./$1.sh fft fft "-std=c99 -lm" 5 128 256 1024 4096 8192
./$1.sh huffman_priority_queue huffman_priority_queue "" 5 100 1000 10000 100000 1000000
./$1.sh huffman huffman "" 5 100 1000 10000 100000 1000000
./$1.sh combinations combinations "" 5 13 14 15 16 17
./$1.sh conjugate_transpose conjugate_transpose "" 5 "16 32" "512 1024" "1024 2048" "2048 4096"
./$1.sh cholesky cholesky "-std=c99 -lm" 5 65536 262144 524288 786432 1310720
./$1.sh deconvolution deconvolution "-std=c99 -lm" 5 256 512 1024 2048
./$1.sh euler_method euler_method "-std=c99 -lm" 5 16384 32768 65536 98304 131072
./$1.sh nbody nbody "" 5 10000000 20000000 30000000 40000000 50000000
./$1.sh lucas lucas "-lm" 5 75000 100000 125000 150000 175000
./$1.sh aes aes "" 5 250000 500000 750000 1000000 1250000

## omitted below because the clang front-end is stupid and won't let me compile and link multiple files together at the same time...
# ./$1.sh keccak-test keccak-test0 "" 5 "0 10000" "0 30000" "0 50000" "0 70000" "0 70000"
# ./$1.sh keccak-test keccak-test1 "" 5 "1 10000" "1 30000" "1 50000" "1 70000" "1 70000"
# ./$1.sh keccak-test keccak-test2 "" 5 "2 10000" "2 30000" "2 50000" "2 70000" "2 70000"
# ./$1.sh keccak-test keccak-test3 "" 5 "3 100" "3 200" "3 300" "3 400" "3 500"
# ./$1.sh keccak-test keccak-test0 "KeccakNISTInterface.c KeccakSponge.c KeccakF-1600-opt32.c genKAT.c KeccakDuplex.c" 5 "0 10000" "0 30000" "0 50000" "0 70000" "0 70000"
# ./$1.sh keccak-test keccak-test1 "KeccakNISTInterface.c KeccakSponge.c KeccakF-1600-opt32.c genKAT.c KeccakDuplex.c" 5 "1 10000" "1 30000" "1 50000" "1 70000" "1 70000"
# ./$1.sh keccak-test keccak-test2 "KeccakNISTInterface.c KeccakSponge.c KeccakF-1600-opt32.c genKAT.c KeccakDuplex.c" 5 "2 10000" "2 30000" "2 50000" "2 70000" "2 70000"
# ./$1.sh keccak-test keccak-test3 "KeccakNISTInterface.c KeccakSponge.c KeccakF-1600-opt32.c genKAT.c KeccakDuplex.c" 5 "3 100" "3 200" "3 300" "3 400" "3 500"