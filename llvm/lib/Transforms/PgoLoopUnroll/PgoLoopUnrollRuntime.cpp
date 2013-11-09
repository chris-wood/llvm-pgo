//===-- UnrollLoopRuntime.cpp - Runtime Loop unrolling utilities ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements some loop unrolling utilities for loops with run-time
// trip counts.  See LoopUnroll.cpp for unrolling loops with compile-time
// trip counts.
//
// The functions in this file are used to generate extra code when the
// run-time trip count modulo the unroll factor is not 0.  When this is the
// case, we need to generate code to execute these 'left over' iterations.
//
// The current strategy generates an if-then-else sequence prior to the
// unrolled loop to execute the 'left over' iterations.  Other strategies
// include generate a loop before or after the unrolled loop.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "pgo-loop-unroll"
#include "PgoLoop.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/SimplifyIndVar.h"
#include <algorithm>

using namespace llvm;

/// RemapInstruction - Convert the instruction operands from referencing the
/// current values into those specified by VMap.
void RemapInstruction(Instruction *I, ValueToValueMapTy &VMap) {
    for (unsigned op = 0, E = I->getNumOperands(); op != E; ++op) {
        Value *Op = I->getOperand(op);
        ValueToValueMapTy::iterator It = VMap.find(Op);
        if (It != VMap.end())
            I->setOperand(op, It->second);
    }
    
    if (PHINode *PN = dyn_cast<PHINode>(I)) {
        for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
            ValueToValueMapTy::iterator It = VMap.find(PN->getIncomingBlock(i));
            if (It != VMap.end())
                PN->setIncomingBlock(i, cast<BasicBlock>(It->second));
        }
    }
}

/// FoldBlockIntoPredecessor - Folds a basic block into its predecessor if it
/// only has one predecessor, and that predecessor only has one successor.
/// The LoopInfo Analysis that is passed will be kept consistent.
/// Returns the new combined block.
static BasicBlock *FoldBlockIntoPredecessor(BasicBlock *BB, LoopInfo* LI,
                                            LPPassManager *LPM) {
  // Merge basic blocks into their predecessor if there is only one distinct
  // pred, and if there is only one distinct successor of the predecessor, and
  // if there are no PHI nodes.
  BasicBlock *OnlyPred = BB->getSinglePredecessor();
  if (!OnlyPred) return 0;
  
  if (OnlyPred->getTerminator()->getNumSuccessors() != 1)
    return 0;
  
  DEBUG(dbgs() << "Merging: " << *BB << "into: " << *OnlyPred);
  
  // Resolve any PHI nodes at the start of the block.  They are all
  // guaranteed to have exactly one entry if they exist, unless there are
  // multiple duplicate (but guaranteed to be equal) entries for the
  // incoming edges.  This occurs when there are multiple edges from
  // OnlyPred to OnlySucc.
  FoldSingleEntryPHINodes(BB);
  
  // Delete the unconditional branch from the predecessor...
  OnlyPred->getInstList().pop_back();
  
  // Make all PHI nodes that referred to BB now refer to Pred as their
  // source...
  BB->replaceAllUsesWith(OnlyPred);
  
  // Move all definitions in the successor to the predecessor...
  OnlyPred->getInstList().splice(OnlyPred->end(), BB->getInstList());
  
  std::string OldName = BB->getName();
  
  // Erase basic block from the function...
  
  // ScalarEvolution holds references to loop exit blocks.
  if (LPM) {
    if (ScalarEvolution *SE = LPM->getAnalysisIfAvailable<ScalarEvolution>()) {
      if (Loop *L = LI->getLoopFor(BB))
        SE->forgetLoop(L);
    }
  }
  LI->removeBlock(BB);
  BB->eraseFromParent();
  
  // Inherit predecessor's name if it exists...
  if (!OldName.empty() && !OnlyPred->hasName())
    OnlyPred->setName(OldName);
  
  return OnlyPred;
}

bool llvm::PgoUnrollLoop(Loop *L, unsigned Count, LoopInfo *LI, LPPassManager *LPM) {

  assert(L != 0);
  assert(Count > 2);
  assert(LI != 0);
  assert(LPM != 0);

  // Ensure preconditions (copied form UnrollLoop.cpp).
  
  BasicBlock *Preheader = L->getLoopPreheader();
  if (!Preheader) {
    DEBUG(dbgs() << "  Can't unroll; loop preheader-insertion failed.\n");
    return false;
  }

  BasicBlock *LatchBlock = L->getLoopLatch();
  if (!LatchBlock) {
    DEBUG(dbgs() << "  Can't unroll; loop exit-block-insertion failed.\n");
    return false;
  }

  // Loops with indirectbr cannot be cloned.
  if (!L->isSafeToClone()) {
    DEBUG(dbgs() << "  Can't unroll; Loop body cannot be cloned.\n");
    return false;
  }

  BasicBlock *Header = L->getHeader();
  BranchInst *BI = dyn_cast<BranchInst>(LatchBlock->getTerminator());

  if (!BI || BI->isUnconditional()) {
    // The loop-rotate pass can be helpful to avoid this in many cases.
    DEBUG(dbgs() << "  Can't unroll; loop not terminated by a conditional branch.\n");
    return false;
  }

  if (Header->hasAddressTaken()) {
    // The loop-rotate pass can be helpful to avoid this in many cases.
    DEBUG(dbgs() << "  Won't unroll loop: address of header block is taken.\n");
    return false;
  }

  if (!UnrollRuntimeLoopProlog(L, Count, LI, LPM))
    return false;

  // Notify ScalarEvolution that the loop will be substantially changed,
  // if not outright eliminated.
  ScalarEvolution *SE = LPM->getAnalysisIfAvailable<ScalarEvolution>();
  if (SE)
    SE->forgetLoop(L);

  // Let's do the real work!
  DEBUG(dbgs() << "UNROLLING loop %" << Header->getName() << " by " << Count);
  
  std::vector<BasicBlock*> LoopBlocks = L->getBlocks();

  bool ContinueOnTrue = L->contains(BI->getSuccessor(0));
  BasicBlock *LoopExit = BI->getSuccessor(ContinueOnTrue);

  // For the first iteration of the loop, we should use the precloned values for
  // PHI nodes.  Insert associations now.
  ValueToValueMapTy LastValueMap;
  std::vector<PHINode*> OrigPHINode;
  for (BasicBlock::iterator I = Header->begin(); isa<PHINode>(I); ++I) {
    OrigPHINode.push_back(cast<PHINode>(I));
  }

  std::vector<BasicBlock*> Headers;
  std::vector<BasicBlock*> Latches;
  Headers.push_back(Header);
  Latches.push_back(LatchBlock);

  // The current on-the-fly SSA update requires blocks to be processed in
  // reverse postorder so that LastValueMap contains the correct value at each
  // exit.
  LoopBlocksDFS DFS(L);
  DFS.perform(LI);

  // Stash the DFS iterators before adding blocks to the loop.
  LoopBlocksDFS::RPOIterator BlockBegin = DFS.beginRPO();
  LoopBlocksDFS::RPOIterator BlockEnd = DFS.endRPO();

  for (unsigned It = 1; It != Count; ++It) {
    std::vector<BasicBlock*> NewBlocks;

    for (LoopBlocksDFS::RPOIterator BB = BlockBegin; BB != BlockEnd; ++BB) {
      ValueToValueMapTy VMap;
      BasicBlock *New = CloneBasicBlock(*BB, VMap, "." + Twine(It));
      Header->getParent()->getBasicBlockList().push_back(New);

      // Loop over all of the PHI nodes in the block, changing them to use the
      // incoming values from the previous block.
      if (*BB == Header)
        for (unsigned i = 0, e = OrigPHINode.size(); i != e; ++i) {
          PHINode *NewPHI = cast<PHINode>(VMap[OrigPHINode[i]]);
          Value *InVal = NewPHI->getIncomingValueForBlock(LatchBlock);
          if (Instruction *InValI = dyn_cast<Instruction>(InVal))
            if (It > 1 && L->contains(InValI))
              InVal = LastValueMap[InValI];
          VMap[OrigPHINode[i]] = InVal;
          New->getInstList().erase(NewPHI);
        }

      // Update our running map of newest clones
      LastValueMap[*BB] = New;
      for (ValueToValueMapTy::iterator VI = VMap.begin(), VE = VMap.end();
           VI != VE; ++VI)
        LastValueMap[VI->first] = VI->second;

      L->addBasicBlockToLoop(New, LI->getBase());

      // Add phi entries for newly created values to all exit blocks.
      for (succ_iterator SI = succ_begin(*BB), SE = succ_end(*BB);
           SI != SE; ++SI) {
        if (L->contains(*SI))
          continue;
        for (BasicBlock::iterator BBI = (*SI)->begin();
             PHINode *phi = dyn_cast<PHINode>(BBI); ++BBI) {
          Value *Incoming = phi->getIncomingValueForBlock(*BB);
          ValueToValueMapTy::iterator It = LastValueMap.find(Incoming);
          if (It != LastValueMap.end())
            Incoming = It->second;
          phi->addIncoming(Incoming, New);
        }
      }
      // Keep track of new headers and latches as we create them, so that
      // we can insert the proper branches later.
      if (*BB == Header)
        Headers.push_back(New);
      if (*BB == LatchBlock)
        Latches.push_back(New);

      NewBlocks.push_back(New);
    }

    // Remap all instructions in the most recent iteration
    for (unsigned i = 0; i < NewBlocks.size(); ++i)
      for (BasicBlock::iterator I = NewBlocks[i]->begin(),
           E = NewBlocks[i]->end(); I != E; ++I)
        ::RemapInstruction(I, LastValueMap);
  }
  
  // Loop over the PHI nodes in the original block, setting incoming values.
  for (unsigned i = 0, e = OrigPHINode.size(); i != e; ++i) {
    PHINode *PN = OrigPHINode[i];
    if (/*CompletelyUnroll*/ false) {
      PN->replaceAllUsesWith(PN->getIncomingValueForBlock(Preheader));
      Header->getInstList().erase(PN);
    }
    else if (Count > 1) {
      Value *InVal = PN->removeIncomingValue(LatchBlock, false);
      // If this value was defined in the loop, take the value defined by the
      // last iteration of the loop.
      if (Instruction *InValI = dyn_cast<Instruction>(InVal)) {
        if (L->contains(InValI))
          InVal = LastValueMap[InVal];
      }
      assert(Latches.back() == LastValueMap[LatchBlock] && "bad last latch");
      PN->addIncoming(InVal, Latches.back());
    }
  }
  
  // Now that all the basic blocks for the unrolled iterations are in place,
  // set up the branches to connect them.
  for (unsigned i = 0, e = Latches.size(); i != e; ++i) {
    // The original branch was replicated in each unrolled iteration.
    BranchInst *Term = cast<BranchInst>(Latches[i]->getTerminator());
    
    // The branch destination.
    unsigned j = (i + 1) % e;
    BasicBlock *Dest = Headers[j];
    bool NeedConditional = true;
    
    // Yes we have a non-constant trip count.
    if (/*RuntimeTripCount*/ true && j != 0) {
      NeedConditional = false;
    }
    
    // For a complete unroll, make the last iteration end with a branch
    // to the exit block.
    if (/*CompletelyUnroll*/ false && j == 0) {
      Dest = LoopExit;
      NeedConditional = false;
    }
    
    // If we know the trip count or a multiple of it, we can safely use an
    // unconditional branch for some iterations.
//  if (j != BreakoutTrip && (TripMultiple == 0 || j % TripMultiple != 0)) {
    if (false) {
      NeedConditional = false;
    }
    
    if (NeedConditional) {
      // Update the conditional branch's successor for the following
      // iteration.
      Term->setSuccessor(!ContinueOnTrue, Dest);
    } else {
      // Remove phi operands at this loop exit
      if (Dest != LoopExit) {
        BasicBlock *BB = Latches[i];
        for (succ_iterator SI = succ_begin(BB), SE = succ_end(BB);
             SI != SE; ++SI) {
          if (*SI == Headers[i])
            continue;
          for (BasicBlock::iterator BBI = (*SI)->begin();
               PHINode *Phi = dyn_cast<PHINode>(BBI); ++BBI) {
            Phi->removeIncomingValue(BB, false);
          }
        }
      }
      // Replace the conditional branch with an unconditional one.
      BranchInst::Create(Dest, Term);
      Term->eraseFromParent();
    }
  }
  
  // Merge adjacent basic blocks, if possible.
  for (unsigned i = 0, e = Latches.size(); i != e; ++i) {
    BranchInst *Term = cast<BranchInst>(Latches[i]->getTerminator());
    if (Term->isUnconditional()) {
      BasicBlock *Dest = Term->getSuccessor(0);
      if (BasicBlock *Fold = FoldBlockIntoPredecessor(Dest, LI, LPM))
        std::replace(Latches.begin(), Latches.end(), Dest, Fold);
    }
  }
  
  if (LPM) {
    // FIXME: Reconstruct dom info, because it is not preserved properly.
    // Incrementally updating domtree after loop unrolling would be easy.
    if (DominatorTree *DT = LPM->getAnalysisIfAvailable<DominatorTree>())
      DT->runOnFunction(*L->getHeader()->getParent());
    
    // Simplify any new induction variables in the partially unrolled loop.
    ScalarEvolution *SE = LPM->getAnalysisIfAvailable<ScalarEvolution>();
    if (SE /*&& !CompletelyUnroll*/) {
      SmallVector<WeakVH, 16> DeadInsts;
      simplifyLoopIVs(L, SE, LPM, DeadInsts);
      
      // Aggressively clean up dead instructions that simplifyLoopIVs already
      // identified. Any remaining should be cleaned up below.
      while (!DeadInsts.empty())
        if (Instruction *Inst =
            dyn_cast_or_null<Instruction>(&*DeadInsts.pop_back_val()))
          RecursivelyDeleteTriviallyDeadInstructions(Inst);
    }
  }
  // At this point, the code is well formed.  We now do a quick sweep over the
  // inserted code, doing constant propagation and dead code elimination as we
  // go.
  const std::vector<BasicBlock*> &NewLoopBlocks = L->getBlocks();
  for (std::vector<BasicBlock*>::const_iterator BB = NewLoopBlocks.begin(),
       BBE = NewLoopBlocks.end(); BB != BBE; ++BB)
    for (BasicBlock::iterator I = (*BB)->begin(), E = (*BB)->end(); I != E; ) {
      Instruction *Inst = I++;
      
      if (isInstructionTriviallyDead(Inst))
        (*BB)->getInstList().erase(Inst);
      else if (Value *V = SimplifyInstruction(Inst))
        if (LI->replacementPreservesLCSSAForm(Inst, V)) {
          Inst->replaceAllUsesWith(V);
          (*BB)->getInstList().erase(Inst);
        }
    }
  
  // Remove the loop from the LoopPassManager if it's completely removed.
//  if (CompletelyUnroll && LPM != NULL)
//    LPM->deleteLoopFromQueue(L);

  DEBUG(dbgs() << "Loop unrolled!");
  return true;
}
