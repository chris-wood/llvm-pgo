// -*- compile-command: "cd ../../../../build/lib/Transforms/PGO/; make" -*-

#include "llvm/Transforms/IPO/InlinerPass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Support/CommandLine.h"

#undef DEBUG_TYPE
#define DEBUG_TYPE "pgo"

using namespace llvm;

static cl::opt<int>
ExecutionCountInlineThreshold("pgo-inline-threshold", cl::Hidden,
			      cl::init(4), cl::Optional,
			      cl::desc("Exectution count threshold for inlining functions"));

namespace {
  struct PgoFunctionInline : public Inliner {
    static char ID;
    explicit PgoFunctionInline(int threshold = ExecutionCountInlineThreshold)
      : Inliner(ID)
      , InlineThreshold(threshold)
    {}

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<ProfileInfo>();
      AU.addPreserved<ProfileInfo>();
      AU.addRequired<InlineCostAnalysis>();
      Inliner::getAnalysisUsage(AU);
    }

    virtual InlineCost getInlineCost(CallSite CS);
    virtual bool runOnSCC(CallGraphSCC &SCC);
  private:
    InlineCostAnalysis *ICA;
    int InlineThreshold;
  };
}

char PgoFunctionInline::ID = 0;

static RegisterPass<PgoFunctionInline> X("pgo-function-inline", "Profile guided function inlining pass", false, false);

InlineCost PgoFunctionInline::getInlineCost(CallSite CS){
  ProfileInfo *PI = &getAnalysis<ProfileInfo>();
  Function *F = CS.getCalledFunction();
  if(F)
    {
      DEBUG(dbgs().write_escaped(F->getName()) << ": " << PI->getExecutionCount(F) << '\n');
      double executionCount = PI->getExecutionCount(F);
      if (executionCount > InlineThreshold) {
	DEBUG((dbgs() << "Inlining function: ").write_escaped(F->getName())
	      << '\n');
	return InlineCost::getAlways();
      }
	return InlineCost::getNever();
    }
  return ICA->getInlineCost(CS, getInlineThreshold(CS));
}

bool PgoFunctionInline::runOnSCC(CallGraphSCC &SCC) {
  ICA = &getAnalysis<InlineCostAnalysis>();
  return Inliner::runOnSCC(SCC);
}

#undef DEBUG_TYPE
