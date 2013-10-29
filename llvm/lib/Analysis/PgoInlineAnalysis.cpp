#include "llvm/Analysis/PgoInlineAnalysis.h"
#include "llvm/Support/CallSite.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/Pass.h"

#undef DEBUG_TYPE
#define DEBUG_TYPE "pgo"

using namespace llvm;

static RegisterPass<PgoInlineAnalysis> P("pgo-inline-analysis", "Profile guided weighting to inlining information",
					 false, false);

char PgoInlineAnalysis::ID = 0;

PgoInlineAnalysis::PgoInlineAnalysis()
  : ModulePass(ID)
  , totalBBExecutions(0)
  , maxBBExecutions(0)
{}

void PgoInlineAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<ProfileInfo>();
}

bool PgoInlineAnalysis::runOnModule(Module &M) {
  PI = &getAnalysis<ProfileInfo>();
  for (Module::iterator FI = M.begin(), FE = M.end(); FI != FE; ++FI) {
    if (FI->isDeclaration())
      continue;
    for (Function::iterator BB = FI->begin(), BBE = FI->end();
         BB != BBE; ++BB) {
      double e = PI->getExecutionCount(BB);
      if (e == ProfileInfo::MissingValue)
	continue;
      if(e > maxBBExecutions)
	maxBBExecutions = e;
      totalBBExecutions += e;
    }
  }
  DEBUG(dbgs() << "total bb excecution count is: " << totalBBExecutions
	<< "\nmax bb execution count is: " << maxBBExecutions << "\n");
  return false;
}

int PgoInlineAnalysis::pgoInlineCostBonus(const CallSite *CS) const {
  return 0;
}

int PgoInlineAnalysis::pgoInlineThresholdBonus(const CallSite *CS) const {
  return 0;
}

#undef DEBUG_TYPE
