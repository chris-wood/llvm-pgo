#!/usr/bin/python3
#===-- inline-excercise.py - Search for optimal inlining constants--------===
#
# This script performs a search across the space of constants used
# for the PGO inline analysis. 
#
# basic idea:
# 1. Generate profiling data for an input compiled with no optimizations.
# 2. Perform a "straight search" along the line Multiplier = 2 * Offset and
#    find the best point along this line. At each point along the line compile
#    a subset of benchmarks and test their performance.
# 3. Use a hill climbing algorithm to find the local maximum starting at
#    the point found in the "straight search"
# 4. Repeat steps 2 and 3 for the linear heuristic and the logarithmic heuristic
#
# Usage: inline-excercise.py [BINDIR]
#   Execute the script in the same directory as the benchmark application source
#   code. BINDIR is the path to the llvm bin directory containing clang and opt.
# 
# Author: Brian Belleville
#===----------------------------------------------------------------------===


import os
import subprocess
from sys import argv
from datetime import datetime, timedelta

# Each element of this vector contains information about how to build
# and run a single benchmark. Each element should be a vector. The
# first element should be the source file name, the second should be
# the command line argument, the remaining args should be any
# additional compiler args (if needed)
programs = [
    ["factor.c", "2000000"],
    ["blocked.c", "900"],
    ["kmeans.c", "1000000 10", "-lm"],
    ["huffman.c", "50000"],
    ["aes.c", "500000"]
]

clang = "./clang"
opt = "./opt"
libprofilert = "../lib/libprofile_rt.so"

default_profileout = "llvmprof.out"

def input_args(info):
    return info[1]

def benchmark_name(info):
    return info[0]

def compile_args(info):
    return info[2:]

def run_no_output(args):
    null = open("/dev/null")
    proc = subprocess.Popen(args, stdout = null, stderr = null)
    ret = proc.wait()
    if(ret != 0):
        raise Exception("process returned non 0. Process args: " + str(args) + "  Return value: " + str(ret))

def add_suffix(info, suffix):
    name_no_ext = os.path.splitext(benchmark_name(info))[0]
    return name_no_ext + suffix

def profile_info_filename(info):
    input_arg = input_args(info)
    profile_file_suffix = ".profile." + input_arg + ".out"
    return add_suffix(info, profile_file_suffix)

def profiling_build_out(info):
    return add_suffix(info, ".profile")

def profiled_bc_build_out(info):
    return add_suffix(info, ".profile.bc")

def reference_build_out(info):
    return add_suffix(info, ".opt")

def reference_bc_build_out(info):
    return add_suffix(info, ".opt.bc")

def pgo_build_out(info):
    return add_suffix(info, ".pgo_opt." + input_args(info))

def pgo_bc_build_out(info):
    return add_suffix(info, ".pgo_opt." + input_args(info) + ".bc")

def bc_build_out(info):
    return add_suffix(info, ".bc")

def compile_bc(info):
    subprocess.check_call([clang, "-O0", "-emit-llvm", benchmark_name(info),
                           "-c", "-o", bc_build_out(info)] + compile_args(info))

def build_with_profile(info):
    subprocess.check_call([opt, "-insert-edge-profiling", bc_build_out(info), "-o", profiled_bc_build_out(info)])
    subprocess.check_call([clang, "-O0", profiled_bc_build_out(info), libprofilert, "-o", profiling_build_out(info)]
                          + compile_args(info))

def build_reference(info):
    subprocess.check_call([opt, "-inline", bc_build_out(info), "-o", reference_bc_build_out(info)])
    subprocess.check_call([clang, "-O0", reference_bc_build_out(info), "-o", reference_build_out(info)]
                          + compile_args(info))
pgo_build_linear_default = False
def build_pgo(info, multiplier=False, offset=False, linear=pgo_build_linear_default):
    additionalArgs = []
    if(offset):
        additionalArgs.append("-pgi-off=" + str(offset))
    if(multiplier):
        additionalArgs.append("-pgi-mul=" + str(multiplier))
    if(linear):
        additionalArgs.append("-pgi-linear=" + "true")
    else:
        additionalArgs.append("-pgi-linear=" + "false")
    args = [opt] + additionalArgs + ["-profile-loader", "-profile-info-file",
                                     profile_info_filename(info), "-inline", bc_build_out(info),
                                     "-o", pgo_bc_build_out(info)]
    subprocess.check_call(args)
    subprocess.check_call([clang, "-O0", pgo_bc_build_out(info), "-o", pgo_build_out(info)] + compile_args(info))

def generate_profile(info):
    try:
        os.remove(default_profileout)
    except:
        pass
    run_no_output(["./" + profiling_build_out(info), input_args(info)])
    os.rename(default_profileout, profile_info_filename(info))

# This will return the time used to execute the provided command with
# stdout redirected to /dev/null, as measured by the time program.
# This function returns the elapsed (wall clock) time in seconds as a
# float value.
def time(args):
    null = open("/dev/null")
    time_cmd = ["/usr/bin/time", "-f", "%e"]
    cmd = time_cmd + args
    proc = subprocess.Popen(cmd, stdout=null, stderr=subprocess.PIPE)
    ret = proc.wait()
    if(ret != 0):
        raise Exception("process returned non 0. Process args: " + str(cmd) + " Return value: " + str(ret))
    return float(proc.stderr.read())


# find the time for optimizing using certain constants.
def measure(offset, multiplier):
    global measure_dict
    offset = int(offset)
    multiplier = int(multiplier)
    print("measuring offset = " + str(offset) + " multiplier = " + str(multiplier))
    total = 0
    for info in programs:
        build_pgo(info, offset=offset, multiplier=multiplier)
        # run 5 times to average out differences in measurments
        for i in range(0,5):
            total += time(["./" + pgo_build_out(info), input_args(info)])
    print("result: ", total)
    return total

# offset, multiplier
initialStepSizes = [400, 400]
epsilon = .01

# A simple hill climb search algorithm, with initial point
# startingPoint
def hill_climb(startingPoint):
    currentPoint = startingPoint
    stepSize = initialStepSizes
    candidate = [ -1,
                  -0.5,
                  0.5,
                  1 ]
    best = currentPoint
    bestScore = measure(currentPoint[0], currentPoint[1]);
    bestPoint = currentPoint
    while True:
        before = bestScore
        for i in range(0, len(currentPoint)):
            for j in range(0, len(candidate)): # try each candidate location
                testPoint = list(currentPoint)
                testPoint[i] = testPoint[i] + stepSize[i] * candidate[j]
                temp = measure(testPoint[0], testPoint[1])
                if(temp < bestScore):
                    bestScore = temp
                    bestPoint = testPoint
                    print("new best = ", bestScore, " at ", bestPoint)
        # if we found a new best, move to there
        if(currentPoint != bestPoint):
            print("move current point from: ", currentPoint)
            currentPoint = bestPoint
            print("to ", currentPoint)
        else:
            print("hill climb found best at: ", currentPoint)
            return currentPoint

# A simple search that will look along the line I think has a
# possibility to give good results.
def straight_search():
    best = measure(0,0)
    bestAt = [0, 0]
    # search in steps of 500 out to -10000, 20000
    step = 500
    for i in range(1, 21):
        offset = -1 * step * i
        multiplier = 2 * step * i
        r = measure(offset=offset, multiplier=multiplier)
        if(r < best):
            best = r
            bestAt = [offset, multiplier]
    print("best: ", best, " at offset: ", bestAt[0], " multiplier: ", bestAt[1])
    return bestAt

def main():
    global clang, opt, libprofilert, pgo_build_linear_default
    if(len(argv) > 1):
        builddir = argv[1]
        clang = builddir + clang
        opt = builddir + opt
        libprofilert = builddir + libprofilert
    # generate profiling information for unoptimized versions of each
    # benchmark
    for info in programs:
        name = benchmark_name(info)
        print("compiling " + name + " to bitcode")
        compile_bc(info)
        print("adding profiling information to " + name)
        build_with_profile(info)
        print("generating profile for " + name)
        generate_profile(info)
    # Test logarithmic heuristic first
    print("Starting straight search of logarithmic heuristic")
    pgo_build_linear_default = False
    # Find best point from the straight search
    log_straight_best = straight_search()
    print("starting hill climb")
    # Use the best point from the straight search to start the hill
    # climb search
    log_hill_best = hill_climb(log_straight_best)
    print("---------- end search over log heuristic ----------\n")

    # Test linear heuristic next
    print("Starting straight search of linear heuristic")
    pgo_build_linear_default = True
    # Find best point from the straight search
    linear_straight_best = straight_search()
    print("starting hill climb")
    # Use the best point from the straight search to start the hill
    # climb search
    linear_hill_best = hill_climb(linear_straight_best)
    print("---------- end search over linear heuristic ----------\n")
    # Print best points found
    print("log best at offset: ", log_hill_best[0], " multiplier: ", log_hill_best[1])
    print("linear best at offset: ", linear_hill_best[0], " multiplier: ", linear_hill_best[1])
main()
