//===-- BasicBlockPlacement.cpp - Basic Block Code Layout optimization ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a very simple profile guided basic block placement
// algorithm.  The idea is to put frequently executed blocks together at the
// start of the function, and hopefully increase the number of fall-through
// conditional branches.  If there is no profile information for a particular
// function, this pass basically orders blocks in depth-first order
//
// The algorithm implemented here is basically "Algo1" from "Profile Guided Code
// Positioning" by Pettis and Hansen, except that it uses basic block counts
// instead of edge counts.  This should be improved in many ways, but is very
// simple for now.
//
// Basically we "place" the entry block, then loop over all successors in a DFO,
// placing the most frequently executed successor until we run out of blocks.  I
// told you this was _extremely_ simplistic. :) This is also much slower than it
// could be.  When it becomes important, this pass will be rewritten to use a
// better algorithm, and then we can worry about efficiency.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "pgo-pre"
#include "llvm/Transforms/Scalar.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/CFG.h"
#include <set>
#include <vector>
#include <queue>
using namespace llvm;

#include <iostream>
using namespace std;

//STATISTIC(NumMoved, "Number of basic blocks moved");

namespace {
  struct PgoPre : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    PgoPre() : FunctionPass(ID) {
      initializePgoPrePass(*PassRegistry::getPassRegistry());
    }

    virtual bool runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesCFG();
      AU.addRequired<ProfileInfo>();
      //AU.addPreserved<ProfileInfo>();  // Does this work?
    }
  private:
    /// PI - The profile information that is guiding us.
    ///
    ProfileInfo *PI;

    /// NumMovedBlocks - Every time we move a block, increment this counter.
    ///
    //unsigned NumMovedBlocks;
  };
}

char PgoPre::ID = 0;
INITIALIZE_PASS_BEGIN(PgoPre, "pgo-pre", "Profile Guided PRE", false, false)
INITIALIZE_AG_DEPENDENCY(ProfileInfo)
INITIALIZE_PASS_END(PgoPre, "pgo-pre", "Profile Guided PRE", false, false)

FunctionPass *llvm::createPgoPrePass() { return new PgoPre(); }

bool PgoPre::runOnFunction(Function &F) {
  return false;
}
/// // runOnFunction - This is the main transformation entry point for a function.
//   if (!NoLoads)
//     MD = &getAnalysis<MemoryDependenceAnalysis>();
//   DT = &getAnalysis<DominatorTree>();
//   TD = getAnalysisIfAvailable<DataLayout>();
//   TLI = &getAnalysis<TargetLibraryInfo>();
//   VN.setAliasAnalysis(&getAnalysis<AliasAnalysis>());
//   VN.setMemDep(MD);
//   VN.setDomTree(DT);

//   bool Changed = false;
//   bool ShouldContinue = true;

//   // Merge unconditional branches, allowing PRE to catch more
//   // optimization opportunities.
//   for (Function::iterator FI = F.begin(), FE = F.end(); FI != FE; ) {
//     BasicBlock *BB = FI++;

//     bool removedBlock = MergeBlockIntoPredecessor(BB, this);
//     if (removedBlock) ++NumGVNBlocks;

//     Changed |= removedBlock;
//   }

//   unsigned Iteration = 0;
//   while (ShouldContinue) {
//     DEBUG(dbgs() << "GVN iteration: " << Iteration << "\n");
//     ShouldContinue = iterateOnFunction(F);
//     if (splitCriticalEdges())
//       ShouldContinue = true;
//     Changed |= ShouldContinue;
//     ++Iteration;
//   }

//   if (EnablePRE) {
//     bool PREChanged = true;
//     while (PREChanged) {
//       PREChanged = performPRE(F);
//       Changed |= PREChanged;
//     }
//   }
//   // FIXME: Should perform GVN again after PRE does something.  PRE can move
//   // computations into blocks where they become fully redundant.  Note that
//   // we can't do this until PRE's critical edge splitting updates memdep.
//   // Actually, when this happens, we should just fully integrate PRE into GVN.

//   cleanupGlobalSets();

//   return Changed;
// }

// void PgoPre::cleanupGlobalSets() {
//   VN.clear();
//   LeaderTable.clear();
//   TableAllocator.Reset();
// }

// /// performPRE - Perform a purely local form of PRE that looks for diamond
// /// control flow patterns and attempts to perform simple PRE at the join point.
// bool PgoPre::performPRE(Function &F) {
//   bool Changed = false;
//   SmallVector<std::pair<Value*, BasicBlock*>, 8> predMap;
//   for (df_iterator<BasicBlock*> DI = df_begin(&F.getEntryBlock()),
//        DE = df_end(&F.getEntryBlock()); DI != DE; ++DI) {
//     BasicBlock *CurrentBlock = *DI;

//     // Nothing to PRE in the entry block.
//     if (CurrentBlock == &F.getEntryBlock()) continue;

//     // Don't perform PRE on a landing pad.
//     if (CurrentBlock->isLandingPad()) continue;

//     for (BasicBlock::iterator BI = CurrentBlock->begin(),
//          BE = CurrentBlock->end(); BI != BE; ) {
//       Instruction *CurInst = BI++;

//       if (isa<AllocaInst>(CurInst) ||
//           isa<TerminatorInst>(CurInst) || isa<PHINode>(CurInst) ||
//           CurInst->getType()->isVoidTy() ||
//           CurInst->mayReadFromMemory() || CurInst->mayHaveSideEffects() ||
//           isa<DbgInfoIntrinsic>(CurInst))
//         continue;

//       // Don't do PRE on compares. The PHI would prevent CodeGenPrepare from
//       // sinking the compare again, and it would force the code generator to
//       // move the i1 from processor flags or predicate registers into a general
//       // purpose register.
//       if (isa<CmpInst>(CurInst))
//         continue;

//       // We don't currently value number ANY inline asm calls.
//       if (CallInst *CallI = dyn_cast<CallInst>(CurInst))
//         if (CallI->isInlineAsm())
//           continue;

//       uint32_t ValNo = VN.lookup(CurInst);

//       // Look for the predecessors for PRE opportunities.  We're
//       // only trying to solve the basic diamond case, where
//       // a value is computed in the successor and one predecessor,
//       // but not the other.  We also explicitly disallow cases
//       // where the successor is its own predecessor, because they're
//       // more complicated to get right.
//       unsigned NumWith = 0;
//       unsigned NumWithout = 0;
//       BasicBlock *PREPred = 0;
//       predMap.clear();

//       for (pred_iterator PI = pred_begin(CurrentBlock),
//            PE = pred_end(CurrentBlock); PI != PE; ++PI) {
//         BasicBlock *P = *PI;
//         // We're not interested in PRE where the block is its
//         // own predecessor, or in blocks with predecessors
//         // that are not reachable.
//         if (P == CurrentBlock) {
//           NumWithout = 2;
//           break;
//         } else if (!DT->isReachableFromEntry(P))  {
//           NumWithout = 2;
//           break;
//         }

//         Value* predV = findLeader(P, ValNo);
//         if (predV == 0) {
//           predMap.push_back(std::make_pair(static_cast<Value *>(0), P));
//           PREPred = P;
//           ++NumWithout;
//         } else if (predV == CurInst) {
//           /* CurInst dominates this predecessor. */
//           NumWithout = 2;
//           break;
//         } else {
//           predMap.push_back(std::make_pair(predV, P));
//           ++NumWith;
//         }
//       }

//       // Don't do PRE when it might increase code size, i.e. when
//       // we would need to insert instructions in more than one pred.
//       if (NumWithout != 1 || NumWith == 0)
//         continue;

//       // Don't do PRE across indirect branch.
//       if (isa<IndirectBrInst>(PREPred->getTerminator()))
//         continue;

//       // We can't do PRE safely on a critical edge, so instead we schedule
//       // the edge to be split and perform the PRE the next time we iterate
//       // on the function.
//       unsigned SuccNum = GetSuccessorNumber(PREPred, CurrentBlock);
//       if (isCriticalEdge(PREPred->getTerminator(), SuccNum)) {
//         toSplit.push_back(std::make_pair(PREPred->getTerminator(), SuccNum));
//         continue;
//       }

//       // Instantiate the expression in the predecessor that lacked it.
//       // Because we are going top-down through the block, all value numbers
//       // will be available in the predecessor by the time we need them.  Any
//       // that weren't originally present will have been instantiated earlier
//       // in this loop.
//       Instruction *PREInstr = CurInst->clone();
//       bool success = true;
//       for (unsigned i = 0, e = CurInst->getNumOperands(); i != e; ++i) {
//         Value *Op = PREInstr->getOperand(i);
//         if (isa<Argument>(Op) || isa<Constant>(Op) || isa<GlobalValue>(Op))
//           continue;

//         if (Value *V = findLeader(PREPred, VN.lookup(Op))) {
//           PREInstr->setOperand(i, V);
//         } else {
//           success = false;
//           break;
//         }
//       }

//       // Fail out if we encounter an operand that is not available in
//       // the PRE predecessor.  This is typically because of loads which
//       // are not value numbered precisely.
//       if (!success) {
//         DEBUG(verifyRemoved(PREInstr));
//         delete PREInstr;
//         continue;
//       }

//       PREInstr->insertBefore(PREPred->getTerminator());
//       PREInstr->setName(CurInst->getName() + ".pre");
//       PREInstr->setDebugLoc(CurInst->getDebugLoc());
//       VN.add(PREInstr, ValNo);
//       ++NumGVNPRE;

//       // Update the availability map to include the new instruction.
//       addToLeaderTable(ValNo, PREInstr, PREPred);

//       // Create a PHI to make the value available in this block.
//       PHINode* Phi = PHINode::Create(CurInst->getType(), predMap.size(),
//                                      CurInst->getName() + ".pre-phi",
//                                      CurrentBlock->begin());
//       for (unsigned i = 0, e = predMap.size(); i != e; ++i) {
//         if (Value *V = predMap[i].first)
//           Phi->addIncoming(V, predMap[i].second);
//         else
//           Phi->addIncoming(PREInstr, PREPred);
//       }

//       VN.add(Phi, ValNo);
//       addToLeaderTable(ValNo, Phi, CurrentBlock);
//       Phi->setDebugLoc(CurInst->getDebugLoc());
//       CurInst->replaceAllUsesWith(Phi);
//       if (Phi->getType()->getScalarType()->isPointerTy()) {
//         // Because we have added a PHI-use of the pointer value, it has now
//         // "escaped" from alias analysis' perspective.  We need to inform
//         // AA of this.
//         for (unsigned ii = 0, ee = Phi->getNumIncomingValues(); ii != ee;
//              ++ii) {
//           unsigned jj = PHINode::getOperandNumForIncomingValue(ii);
//           VN.getAliasAnalysis()->addEscapingUse(Phi->getOperandUse(jj));
//         }

//         if (MD)
//           MD->invalidateCachedPointerInfo(Phi);
//       }
//       VN.erase(CurInst);
//       removeFromLeaderTable(ValNo, CurInst, CurrentBlock);

//       DEBUG(dbgs() << "GVN PRE removed: " << *CurInst << '\n');
//       if (MD) MD->removeInstruction(CurInst);
//       DEBUG(verifyRemoved(CurInst));
//       CurInst->eraseFromParent();
//       Changed = true;
//     }
//   }

//   if (splitCriticalEdges())
//     Changed = true;

//   return Changed;
// }

// /// iterateOnFunction - Executes one iteration of GVN
// bool PgoPre::iterateOnFunction(Function &F) {
//   cleanupGlobalSets();

//   // Top-down walk of the dominator tree
//   bool Changed = false;
// #if 0
//   // Needed for value numbering with phi construction to work.
//   ReversePostOrderTraversal<Function*> RPOT(&F);
//   for (ReversePostOrderTraversal<Function*>::rpo_iterator RI = RPOT.begin(),
//        RE = RPOT.end(); RI != RE; ++RI)
//     Changed |= processBlock(*RI);
// #else
//   for (df_iterator<DomTreeNode*> DI = df_begin(DT->getRootNode()),
//        DE = df_end(DT->getRootNode()); DI != DE; ++DI)
//     Changed |= processBlock(DI->getBlock());
// #endif

//   return Changed;
// }


// /// splitCriticalEdges - Split critical edges found during the previous
// /// iteration that may enable further optimization.
// bool PgoPre::splitCriticalEdges() {
//   if (toSplit.empty())
//     return false;
//   do {
//     std::pair<TerminatorInst*, unsigned> Edge = toSplit.pop_back_val();
//     SplitCriticalEdge(Edge.first, Edge.second, this);
//   } while (!toSplit.empty());
//   if (MD) MD->invalidateCachedPredecessors();
//   return true;
// }

// /// verifyRemoved - Verify that the specified instruction does not occur in our
// /// internal data structures.
// void PgoPre::verifyRemoved(const Instruction *Inst) const {
//   VN.verifyRemoved(Inst);

//   // Walk through the value number scope to make sure the instruction isn't
//   // ferreted away in it.
//   for (DenseMap<uint32_t, LeaderTableEntry>::const_iterator
//        I = LeaderTable.begin(), E = LeaderTable.end(); I != E; ++I) {
//     const LeaderTableEntry *Node = &I->second;
//     assert(Node->Val != Inst && "Inst still in value numbering scope!");

//     while (Node->Next) {
//       Node = Node->Next;
//       assert(Node->Val != Inst && "Inst still in value numbering scope!");
//     }
//   }
// }
