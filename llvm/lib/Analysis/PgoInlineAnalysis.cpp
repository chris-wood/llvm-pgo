#include "llvm/Analysis/PgoInlineAnalysis.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/Pass.h"

#undef DEBUG_TYPE
#define DEBUG_TYPE "pgo"

using namespace llvm;

static RegisterPass<PgoInlineAnalysis> P("pgo-inline-analysis", "Profile guided weighting to inlining information",
					 false, false);

char PgoInlineAnalysis::ID = 0;

void PgoInlineAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<ProfileInfo>();
  AU.addPreserved<ProfileInfo>();
  AU.setPreservesAll();
}

bool PgoInlineAnalysis::runOnModule(Module &M) {
  DEBUG(dbgs() << "in analysis pass");
  return false;
}

int PgoInlineAnalysis::pgoInlineBonus(CallSite *CS) {
  DEBUG(dbgs() << "pgoInlineBonus");
  return 0;
}

#undef DEBUG_TYPE
