//===-- PgoInlineAnalysis.h --------------------------------------*- C++ -*-===//
//
// This file declares an analysis pass which determines a bonus to
// apply to the inline cost threshold based on profile information.
// This analysis is utilized by the inliner as one of the factors used
// to make inlining decisions.
//
// Author: Brian Belleville
//===----------------------------------------------------------------------===//

#ifndef _PGOINLINEANALYSIS_H_
#define _PGOINLINEANALYSIS_H_

#include "llvm/Pass.h"
#include "llvm/Analysis/ProfileInfo.h"
#include <map>

namespace llvm {
  class CallSite;

  class PgoInlineAnalysis : public ModulePass
  {
  public:
    static char ID;
    PgoInlineAnalysis();
    int pgoInlineCostBonus(const CallSite *CS) const;
    int pgoInlineThresholdBonus(const CallSite *CS) const;
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
    virtual bool runOnModule(Module &M);
  private:
    ProfileInfo *PI;
    double totalBBExecutions;
    double maxBBExecutions;
  };
}

#endif /* _PGOINLINEANALYSIS_H_ */
