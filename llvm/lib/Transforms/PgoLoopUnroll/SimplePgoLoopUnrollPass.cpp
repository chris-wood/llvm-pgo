//===-- PgoLoopUnroll.cpp - Loop unroller pass -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass implements a simple loop unroller.  It works best when loops have
// been canonicalized by the -indvars pass, allowing it to determine the trip
// counts of loops easily.
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "pgo-loop-unroll"

#include "llvm/Transforms/Scalar.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
#include <climits>
#include <algorithm>

using namespace llvm;

namespace {
  class PgoLoopUnroll : public LoopPass {
  public:
    static char ID; // Pass ID, replacement for typeid
    PgoLoopUnroll() : LoopPass(ID) { }

    virtual bool runOnLoop(Loop *L, LPPassManager &LPM);

    /// This transformation requires natural loop information & requires that
    /// loop preheaders be inserted into the CFG...
    ///
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<ProfileInfo>();
      AU.addPreserved<ProfileInfo>();

      AU.addRequired<LoopInfo>();
      AU.addPreserved<LoopInfo>();

      // The following passes did invalidate the ProfileInfo.
      // I have hacked them to inlcude "AU.addPreserved<ProfileInfo>();".
      AU.addRequiredID(LoopSimplifyID);
      AU.addPreservedID(LoopSimplifyID);
      AU.addRequiredID(LCSSAID);
      AU.addPreservedID(LCSSAID);

      AU.addRequired<ScalarEvolution>();
      AU.addPreserved<ScalarEvolution>();
      AU.addRequired<TargetTransformInfo>();
      
      // FIXME: Loop unroll requires LCSSA. And LCSSA requires dom info.
      // If loop unroll does not preserve dom info then LCSSA pass on next
      // loop will receive invalid dom info.
      // For now, recreate dom info, if loop is unrolled.
      AU.addPreserved<DominatorTree>();
    }

  private:
    unsigned getExecutionCount(Loop* loop);
    unsigned computeUnrollCount(unsigned executionCount);
    unsigned getNextHighestPowerOf2(unsigned number);
    unsigned isPowerOf2(unsigned number);
  };
}

// Compute the maximum execution count of all basic blocks within the loop.
unsigned PgoLoopUnroll::getExecutionCount(Loop* loop) {
  ProfileInfo *PI = &getAnalysis<ProfileInfo>();
  unsigned count = 0;

  for (Loop::block_iterator I = loop->block_begin(), E = loop->block_end();
       I !=E; ++I) {
    count = std::max(count, (unsigned) (PI->getExecutionCount(*I) + 0.5));
  }

  return count;
}

unsigned PgoLoopUnroll::isPowerOf2(unsigned number) {
  return (number & (number - 1)) == 0;
}

unsigned PgoLoopUnroll::getNextHighestPowerOf2(unsigned number) {
  // Return if number is already a power of 2.
  if (isPowerOf2(number))
    return number;

  int pow;
  for (pow = 0; number != 0; pow++)
    number >>= 1;

  return 1 << pow;
}

unsigned PgoLoopUnroll::computeUnrollCount(unsigned executionCount) {
  const unsigned maxUnrollCount = 128;
  unsigned count = getNextHighestPowerOf2(executionCount);

  return std::min(count, maxUnrollCount);
}

char PgoLoopUnroll::ID = 0;
static RegisterPass<PgoLoopUnroll> X("simple-pgo-loop-unroll", "Unroll loops", false, false);

bool PgoLoopUnroll::runOnLoop(Loop *L, LPPassManager &LPM) {
  DEBUG(dbgs() << "Running SIMPLE pgo loop unroll pass.\n");

  LoopInfo *LI = &getAnalysis<LoopInfo>();

  unsigned executionCount = getExecutionCount(L);
  DEBUG(dbgs() << "Execution count: " << executionCount << "\n");

  unsigned unrollCount = computeUnrollCount(executionCount);
  DEBUG(dbgs() << "Unroll count: " << unrollCount << "\n");

  unsigned TripCount = 0;
  unsigned TripMultiple = 1;
  bool AllowRuntime = false;

  if (UnrollLoop(L, unrollCount, TripCount, AllowRuntime, TripMultiple, LI, &LPM)) {
    DEBUG(dbgs() << "Loop unrolled!\n");
    return true;
  }

  DEBUG(dbgs() << "Loop NOT unrolled!\n");

  return false;
}
