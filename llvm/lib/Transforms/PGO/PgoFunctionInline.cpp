// -*- compile-command: "cd ../../../../build/lib/Transforms/PGO/; make" -*-
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ProfileInfo.h"

using namespace llvm;

namespace {
  struct PgoFunctionInline : public FunctionPass {
    static char ID;
    PgoFunctionInline() : FunctionPass(ID) {}
    virtual bool runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<ProfileInfo>();
    }
  };
}


char PgoFunctionInline::ID = 0;

static RegisterPass<PgoFunctionInline> X("pgo-function-inline", "Profile guided function inlining pass", false, false);

bool PgoFunctionInline::runOnFunction(Function &F) {
  ProfileInfo *PI = &getAnalysis<ProfileInfo>();
  errs().write_escaped(F.getName()) << ": " << PI->getExecutionCount(&F) << '\n';
  return false;
}
