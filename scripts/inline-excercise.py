#!/usr/bin/python3

import os
import subprocess
from datetime import datetime, timedelta

# Each element of this vector contains information about how to build
# and run a single benchmark. Each element should be a vector. The
# first element should be the source file name, the second should be
# the command line argument, the remaining args should be any
# additional compiler args (if needed)
# programs = [["nbody.c", "50000000", "-lm"],
#             ["spectral_norm.c",  "5500", "-lm"],
#             ["fft.c", "256", "-std=c99", "-lm"]]

programs = [["nbody.c", "500000", "-lm"],
            ["spectral_norm.c",  "550", "-lm"],
            ["fft.c", "64", "-std=c99", "-lm"]]


clang = "bin/clang"
opt = "bin/opt"
libprofilert = "lib/libprofile_rt.so"

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

def pgo_build_out(info):
    return add_suffix(info, ".pgo_opt." + input_arg(info))

def pgo_bc_build_out(info):
    return add_suffix(info, ".pgo_opt." + input_arg(info) + ".bc")

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

def generate_profile(info):
    try:
        os.remove(default_profileout)
    except FileNotFoundError:
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

def main():
    builddir = "/home/brian/Code/llvm-pgo/build/Release+Asserts/"
    global clang
    global opt
    global libprofilert
    clang = builddir + clang
    opt = builddir + opt
    libprofilert = builddir + libprofilert
    for info in programs:
        name = benchmark_name(info)
        # print("compiling " + name + " to bitcode")
        # compile_bc(info)
        # print("adding profiling information to " + name)
        # build_with_profile(info)
        # print("generating profile for " + name)
        # generate_profile(info)
        print("building reference version of " + name)
        build_reference(info)
        print("timing")
        print(time(["./" + reference_build_out(info), input_args(info)]))

main()
