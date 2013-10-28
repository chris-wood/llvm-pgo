#!/bin/bash
  
echo Running experiment for optimization: $1

./$1.sh is_prime "" 5 1024 2048 4096 8192 16384
./$1.sh block_test "" 5 1000 2000 3000 4000 5000
./$1.sh blocked "" 5 500 600 700 800 900 1000
./$1.sh factor "" 5 1000000 2000000 3000000 4000000 5000000
./$1.sh bubble "" 5 50000 100000 150000 200000 250000
./$1.sh kmeans "-lm" 5 "10000 10" "100000 10" "1000000 10" "1000000 20" "1000000 50"
./$1.sh permutations "" 5 6 7 8 9 10
./$1.sh mandlebrot_seq "" 5 500 600 700 800 900 1000
./$1.sh fft "-std=c99 -lm" 5 128 256 1,024 4,096 8,192
./$1.sh huffman_priority_queue "" 5 100 1000 10000 100000 1000000
./$1.sh huffman "" 5 100 1000 10000 100000 1000000
./$1.sh combinations "" 5 9 10 11 12 13
./$1.sh conjugate_transpose "" 5 "16 32" "512 1024" "1024 2048" "2048 4096"
./$1.sh cholesky "-std=c99 -lm" 5 65536 262144
./$1.sh deconvolution "-std=c99 -lm" 5 256 512 1024 2048
./$1.sh euler_method "-std=c99 -lm" 5 16384 32768 65536
