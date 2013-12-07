//===-- PgoLoop.h - PGO unrolling utilities -------------------------------===//
//
// This file defines a loop unrolling utilitiy function. It does not define any
// actual pass or policy, but provides a single function to perform PGO loop
// unrolling based on the "offset jumping" idea described in the project
// report.
// 
// Author: Julian Lettner
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_PGO_LOOP_H
#define LLVM_PGO_LOOP_H

namespace llvm {

class Loop;
class LoopInfo;
class LPPassManager;

bool PgoUnrollLoop(Loop *L, unsigned Count, LoopInfo *LI, LPPassManager *LPM);

}

#endif
