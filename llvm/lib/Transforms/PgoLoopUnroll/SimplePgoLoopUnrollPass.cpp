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

    int staticUnroll(Loop* loop, LPPassManager& LPM);
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

#define DONT_UNROLL -1
#define TRY_UNROLL 0
#define ALREADY_UNROLLED 1

bool PgoLoopUnroll::runOnLoop(Loop *L, LPPassManager &LPM) {
  DEBUG(dbgs() << "Running SIMPLE pgo loop unroll pass.\n");

  int result = staticUnroll(L, LPM);
  if (result == DONT_UNROLL)
    return false;
  if (result == ALREADY_UNROLLED)
    return true;

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


/// ApproximateLoopSize - Approximate the size of the loop.
static unsigned ApproximateLoopSize(const Loop *L, unsigned &NumCalls,
                                    bool &NotDuplicatable,
                                    const TargetTransformInfo &TTI) {
  CodeMetrics Metrics;
  for (Loop::block_iterator I = L->block_begin(), E = L->block_end();
       I != E; ++I)
    Metrics.analyzeBasicBlock(*I, TTI);
  NumCalls = Metrics.NumInlineCandidates;
  NotDuplicatable = Metrics.notDuplicatable;

  unsigned LoopSize = Metrics.NumInsts;

  // Don't allow an estimate of size zero.  This would allows unrolling of loops
  // with huge iteration counts, which is a compile time problem even if it's
  // not a problem for code quality.
  if (LoopSize == 0) LoopSize = 1;

  return LoopSize;
}

int PgoLoopUnroll::staticUnroll(Loop *L, LPPassManager &LPM) {
  LoopInfo *LI = &getAnalysis<LoopInfo>();
  ScalarEvolution *SE = &getAnalysis<ScalarEvolution>();
  const TargetTransformInfo &TTI = getAnalysis<TargetTransformInfo>();

  BasicBlock *Header = L->getHeader();
  DEBUG(dbgs() << "Loop Unroll: F[" << Header->getParent()->getName()
        << "] Loop %" << Header->getName() << "\n");
  (void)Header;

  unsigned Threshold = 150;
  // Find trip count and trip multiple if count is not available
  unsigned TripCount = 0;
  unsigned TripMultiple = 1;
  // Find "latch trip count". UnrollLoop assumes that control cannot exit
  // via the loop latch on any iteration prior to TripCount. The loop may exit
  // early via an earlier branch.
  BasicBlock *LatchBlock = L->getLoopLatch();
  if (LatchBlock) {
    TripCount = SE->getSmallConstantTripCount(L, LatchBlock);
    TripMultiple = SE->getSmallConstantTripMultiple(L, LatchBlock);
  }
  // Use a default unroll-count if the user doesn't specify a value
  // and the trip count is a run-time value.  The default is different
  // for run-time or compile-time trip count loops.
  unsigned CurrentCount = 0;
  bool UnrollRuntime = false;

  unsigned Count = CurrentCount;
  if (UnrollRuntime && CurrentCount == 0 && TripCount == 0)
    Count = 8;

  if (Count == 0) {
    // Conservative heuristic: if we know the trip count, see if we can
    // completely unroll (subject to the threshold, checked below); otherwise
    // try to find greatest modulo of the trip count which is still under
    // threshold value.
    if (TripCount == 0)
      return TRY_UNROLL;
    Count = TripCount;
  }

  // Enforce the threshold.
  if (true) {
    unsigned NumInlineCandidates;
    bool notDuplicatable;
    unsigned LoopSize = ApproximateLoopSize(L, NumInlineCandidates,
                                            notDuplicatable, TTI);
    DEBUG(dbgs() << "  Loop Size = " << LoopSize << "\n");
    if (notDuplicatable) {
      DEBUG(dbgs() << "  Not unrolling loop which contains non duplicatable"
            << " instructions.\n");
      return DONT_UNROLL;
    }
    if (NumInlineCandidates != 0) {
      DEBUG(dbgs() << "  Not unrolling loop with inlinable calls.\n");
      return DONT_UNROLL;
    }
    uint64_t Size = (uint64_t)LoopSize*Count;
    if (TripCount != 1 && Size > Threshold) {
      DEBUG(dbgs() << "  Too large to fully unroll with count: " << Count
            << " because size: " << Size << ">" << Threshold << "\n");
      if (!false && !(UnrollRuntime && TripCount == 0)) {
        DEBUG(dbgs() << "  will not try to unroll partially because "
              << "-unroll-allow-partial not given\n");
        return TRY_UNROLL;
      }
      if (TripCount) {
        // Reduce unroll count to be modulo of TripCount for partial unrolling
        Count = Threshold / LoopSize;
        while (Count != 0 && TripCount%Count != 0)
          Count--;
      }
      else if (UnrollRuntime) {
        // Reduce unroll count to be a lower power-of-two value
        while (Count != 0 && Size > Threshold) {
          Count >>= 1;
          Size = LoopSize*Count;
        }
      }
      if (Count < 2) {
        DEBUG(dbgs() << "  could not unroll partially\n");
        return DONT_UNROLL;
      }
      DEBUG(dbgs() << "  partially unrolling with count: " << Count << "\n");
    }
  }

  // Unroll the loop.
  if (!UnrollLoop(L, Count, TripCount, UnrollRuntime, TripMultiple, LI, &LPM))
    return TRY_UNROLL;

  return ALREADY_UNROLLED;
}
