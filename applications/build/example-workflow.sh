$ pwd
# /Users/jlettner/Work/pgo/applications/build

$ ls -l
# total 72
# lrwxr-xr-x  1 jlettner  staff   49 Oct 16 14:21 PgoLoopUnroll.dylib -> ../../build/Debug+Asserts/lib/PgoLoopUnroll.dylib
# -rwxr-xr-x  1 jlettner  staff   32 Oct 18 16:35 build-llvm.sh
# lrwxr-xr-x  1 jlettner  staff   35 Oct 14 16:36 clang -> ../../build/Debug+Asserts/bin/clang
# -rwxr-xr-x  1 jlettner  staff  418 Oct 16 17:53 clean.sh
# lrwxr-xr-x  1 jlettner  staff   33 Oct 16 12:27 llc -> ../../build/Debug+Asserts/bin/llc
# lrwxr-xr-x  1 jlettner  staff   39 Oct 14 18:02 llvm-prof -> ../../build/Debug+Asserts/bin/llvm-prof
# lrwxr-xr-x  1 jlettner  staff   33 Oct 14 16:36 opt -> ../../build/Debug+Asserts/bin/opt
# -rwxr-xr-x  1 jlettner  staff  735 Oct 18 16:07 profile.sh
# -rwxr-xr-x  1 jlettner  staff  545 Oct 16 17:15 runpgo.sh


# The source file /applications/loop.c
# is a simplistic main function containing a loop with a constant iteration count.

#  -- Workflow starts here ---

# 1) Compile source file to to llvm bitcode file /applications/build.
$ ./clang -c -O1 -emit-llvm ../loop.c -o loop.bc
# Creates: loop.bc

# 2) Inject instrumentation code into llvm bitcode file.
$ ./opt -insert-edge-profiling -insert-gcov-profiling loop.bc -o loop.profile.bc
# Creates: loop.profile.bc

# 3) Generate (instrumented) executable from instrumented llvm bitcode file.
$ ./clang -o loop.profile loop.profile.bc ../../build/Debug+Asserts/lib/libprofile_rt.dylib
# Creates: loop.profile

# 4) Run instrumented executable.
$ ./loop.profile
# Creates: llvmprof.out

# 5) Display the gathered profiling information (optional).
$ ./llvm-prof loop.bc
# Creates: - (prints info to stdout)

# 6) Run custom pass.
$ ./opt -S -profile-loader -profile-metadata-loader -profile-verifier -debug-only=pgo-loop-unroll -load PgoLoopUnroll.dylib -pgo-loop-unroll < loop.bc > loop-pgo.s
# Creates: loop-pgo.s (loop is successfully unrolled)

# I added some debug output to the pass (/llvm/lib/Transforms/PgoLoopUnroll/PgoLoopUnrollPass.cpp) which prints
# the result of ProfileInfo.getExecutionCount for different constructs, but it appears to be always -1.0.

# Btw. steps 1-5 are automated by /applications/build/profile.sh.
