// -*- mode: c++; -*-
#ifndef _PGOINLINEANALYSIS_H_
#define _PGOINLINEANALYSIS_H_

#include "llvm/Pass.h"

namespace llvm {
  
  class CallSite;

  class PgoInlineAnalysis : public ModulePass
  {
  public:
    static char ID;
    PgoInlineAnalysis() : ModulePass(ID) {}
    virtual int pgoInlineBonus(CallSite *CS);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
    virtual bool runOnModule(Module &M);
  };
}

#endif /* _PGOINLINEANALYSIS_H_ */
