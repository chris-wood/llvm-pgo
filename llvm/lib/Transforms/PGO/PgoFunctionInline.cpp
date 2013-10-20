// -*- compile-command: "cd ../../../../build/lib/Transforms/PGO/; make" -*-

#include "llvm/Transforms/IPO/InlinerPass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/Analysis/InlineCost.h"

using namespace llvm;

namespace {
  struct PgoFunctionInline : public Inliner {
    static char ID;
    PgoFunctionInline() : Inliner(ID) {}
    
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<ProfileInfo>();
      AU.addPreserved<ProfileInfo>();
      AU.addRequired<InlineCostAnalysis>();
      Inliner::getAnalysisUsage(AU);
    }
    
    virtual InlineCost getInlineCost(CallSite CS) {
      return ICA->getInlineCost(CS, getInlineThreshold(CS));
    }
    virtual bool runOnSCC(CallGraphSCC &SCC);
  private:
    InlineCostAnalysis *ICA;
  };
}


char PgoFunctionInline::ID = 0;

static RegisterPass<PgoFunctionInline> X("pgo-function-inline", "Profile guided function inlining pass", false, false);

bool PgoFunctionInline::runOnSCC(CallGraphSCC &SCC) {
  ProfileInfo *PI = &getAnalysis<ProfileInfo>();
  CallGraphSCC::iterator i, e;
  for (i = SCC.begin(), e = SCC.end(); i != e; ++i) {
    Function *F = (*i)->getFunction();
    errs().write_escaped(F->getName()) << ": " << PI->getExecutionCount(F) << '\n';
  }
  return false;
}
