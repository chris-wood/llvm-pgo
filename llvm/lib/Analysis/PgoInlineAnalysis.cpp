#include <cmath>
#include "llvm/Analysis/PgoInlineAnalysis.h"
#include "llvm/Support/CallSite.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"

#undef DEBUG_TYPE
#define DEBUG_TYPE "pgo"

using namespace llvm;

INITIALIZE_PASS_BEGIN(PgoInlineAnalysis, "pgo-inline-analysis", "Profile guided weighting to inlining information",
		      false, false)
INITIALIZE_AG_DEPENDENCY(ProfileInfo)
INITIALIZE_PASS_END(PgoInlineAnalysis, "pgo-inline-analysis", "Profile guided weighting to inlining information",
		    false, false)

static cl::opt<int> pgiMultiplier("pgi-mul", cl::Hidden, cl::init(20000),
				  cl::Optional, cl::desc("multiplyer for computing profile guided inlining"));

static cl::opt<int> pgiOffset("pgi-off",  cl::Hidden, cl::init(-10000),
			      cl::Optional, cl::desc("offset for computing profile guided inlining"));

static cl::opt<bool> pgiLinear("pgi-linear", cl::Hidden, cl::init(true),
				  cl::Optional, cl::desc("If true, use a linear heuristic for assigning the threshold bonus, if false use a logarithmic one instead"));

// exc: execution count for call site
// tex: total execution count of all basic blocks
// mex: maximum execution count of all basic blocks
static int computeThresholdBonus(double exc, double tex, double mex) {
  if(pgiLinear)
    return ((exc / mex * pgiMultiplier) + pgiOffset);
  return ((log(1 + exc) / log(1 + mex) * pgiMultiplier) + pgiOffset);
}

// exc: execution count for call site
// tex: total execution count of all basic blocks
// mex: maximum execution count of all basic blocks
static int computeCostBonus(double exc, double tex, double mex) {
  return 0;
}

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
	<< "\nmax bb execution count is: " << maxBBExecutions << "\n"
	<< "pgi-off = " << pgiOffset << "\npgi-mul = " << pgiMultiplier);
  return false;
}

int PgoInlineAnalysis::pgoInlineCostBonus(const CallSite *CS) const {
  BasicBlock *b = CS->getInstruction()->getParent();
  double e = PI->getExecutionCount(b);
  if (e == ProfileInfo::MissingValue)
    return 0;
  return computeCostBonus(e, totalBBExecutions, maxBBExecutions);
}

int PgoInlineAnalysis::pgoInlineThresholdBonus(const CallSite *CS) const {
  BasicBlock *b = CS->getInstruction()->getParent();
  double e = PI->getExecutionCount(b);
  if (e == ProfileInfo::MissingValue)
    return 0;
  return computeThresholdBonus(e, totalBBExecutions, maxBBExecutions);
}

#undef DEBUG_TYPE
