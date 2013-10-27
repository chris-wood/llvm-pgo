#!/bin/bash

./compile.sh is_prime 5 1024 2048 4096 8192 16384
./compile.sh block_test 5 1000 2000 3000 4000 5000
./compile.sh blocked 5 500 600 700 800 900 1000
./compile.sh factor 5 1000000 2000000 3000000 4000000 5000000
./compile.sh bubble 5 50000 100000 150000 200000 250000
./compile.sh kmeans 5 100000 200000 300000 400000 500000
./compile.sh permutations 5 8 9 10 11 12
./compile.sh mandlebrot 5 500 600 700 800 900 1000
./compile.sh fft 5 128 256 1,024 4,096 8,192
./compile.sh huffman_priority_queue 5 100 1000 10000 100000 1000000
./compile.sh huffman 5 100 1000 10000 100000 1000000
./compile.sh combinations 5 8 9 10 11 12
./compile.sh conjugate_transpose 5 "16 32" "512 1024" "1024 2048" "2048 4096"
./compile.sh kmeans 5 "10000 10" "100000 10" "1000000 10" "1000000 20" "1000000 50"
./compile.sh cholesky 5 65536 262144
./compile.sh deconvolution 5 256 512 1024 2048
./compile.sh euler_method 5 16384 32768 65536
