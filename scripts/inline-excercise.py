#!/usr/bin/python3

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
llvm_dis = "./llvm-dis"
libprofilert = "../lib/libprofile_rt.so"

default_profileout = "llvmprof.out"

def input_args(info):
    return info[1]

def benchmark_name(info):
    return info[0]

def compile_args(info):
    return info[2:]

# basic idea:
# 1. Generate profiling data for a set input compiled with no optimizations.
# 2. compile program with default inlining program and time (use time program)
# 3. compile program with pgo inlining and time
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

def reference_ll_build_out(info):
    return add_suffix(info, ".opt.ll")

def pgo_build_out(info):
    return add_suffix(info, ".pgo_opt." + input_args(info))

def pgo_bc_build_out(info):
    return add_suffix(info, ".pgo_opt." + input_args(info) + ".bc")

def pgo_ll_build_out(info):
    return add_suffix(info, ".pgo_opt." + input_args(info) + ".ll")

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
    subprocess.check_call([llvm_dis, reference_bc_build_out(info), "-o", reference_ll_build_out(info)])
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
    subprocess.check_call([llvm_dis, pgo_bc_build_out(info), "-o", pgo_ll_build_out(info)])
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
    # # Search in steps of 1000 from -6000, 12000 to -10000, 20000
    # step = 1000
    # for i in range(6, 11):
    #     offset = -1 * step * i
    #     multiplier = 2 * step * i
    #     r = measure(offset=offset, multiplier=multiplier)
    #     if(r < best):
    #         best = r
    #         bestAt = [offset, multiplier]
    print("best: ", best, " at offset: ", bestAt[0], " multiplier: ", bestAt[1])
    return bestAt

def main():
    global clang, opt, libprofilert, pgo_build_linear_default, llvm_dis
    if(len(argv) > 1):
        builddir = argv[1]
        clang = builddir + clang
        opt = builddir + opt
        libprofilert = builddir + libprofilert
        llvm_dis = builddir + llvm_dis
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
        print("compiling reference build of ", name)
        build_reference(info)
        print("compiling pgo build of ", name)
        build_pgo(info)
        
main()
