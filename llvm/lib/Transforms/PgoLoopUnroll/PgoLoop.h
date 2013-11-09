#ifndef LLVM_PGO_LOOP_H
#define LLVM_PGO_LOOO_H

namespace llvm {

class Loop;
class LoopInfo;
class LPPassManager;

bool PgoUnrollLoop(Loop *L, unsigned Count, LoopInfo *LI, LPPassManager *LPM);

}

#endif
