//===- PgoPre.cpp - Eliminate redundant values and loads ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass performs global value numbering to eliminate fully redundant
// instructions.  It also performs simple dead load elimination.
//
// Note that this pass does the value numbering itself; it does not use the
// ValueNumbering analysis passes.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "gvn"
#include "llvm/Transforms/Scalar.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/PHITransAddr.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Assembly/Writer.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/PatternMatch.h"
#include "llvm/Target/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ValueTracking.h"
// #include "llvm/Analysis/ValueNumbering.h"
#include "llvm/Transforms/Scalar.h"
//#include "Support/Debug.h"
//#include "Support/DepthFirstIterator.h"
#include "llvm/ADT/PostOrderIterator.h"
//#include "Support/Statistic.h"
//#include "Support/hash_set"
#include <vector>
#include <algorithm>
#include <map>
#include <set>
#include <queue>

using namespace llvm;
using namespace PatternMatch;

STATISTIC(NumPgoPreInstr,  "Number of instructions deleted");
// STATISTIC(NumPgoPreLoad,   "Number of loads deleted");
STATISTIC(NumPgoPrePgoPre,    "Number of instructions PgoPre'd");
STATISTIC(NumPgoPreBlocks, "Number of blocks merged");
STATISTIC(NumPgoPreSimpl,  "Number of instructions simplified");
STATISTIC(NumPgoPreEqProp, "Number of equalities propagated");
STATISTIC(NumPgoPreLoad,   "Number of loads PgoPre'd");

static cl::opt<bool> EnablePgoPre("enable-pgopre", cl::init(true), cl::Hidden);
static cl::opt<bool> EnableLoadPgoPre("enable-load-pgopre", cl::init(true));

// Maximum allowed recursion depth.
static cl::opt<uint32_t>
MaxRecurseDepth("max-pgo-recurse-depth", cl::Hidden, cl::init(1000), cl::ZeroOrMore,
                cl::desc("Max pgo recurse depth (default = 1000)"));

// for convenience
#include <iostream>
using namespace std; 

typedef std::pair<const BasicBlock*, const BasicBlock*> GraphEdge;

class DominatorSet 
{
public:

  // Function* function;
  // Instruction* start;
  map<Instruction*, vector<Instruction*> > dominatorMap;

  void buildDominatorSet(Function* function)
  {
    // dominator of the start node is the start itself:
    //
    // Dom(n0) = {n0}
    dominatorMap[&(*(inst_begin(function)))].push_back(&(*(inst_begin(function))));

    // for all other nodes, set all nodes as the dominators:
    // 
    // for each n in N - {n0}
    //  Dom(n) = N;
    for (inst_iterator instItr = inst_begin(function), E = inst_end(function); instItr != E; ++instItr)
    {
      if (instItr != inst_begin(function))
      {
        Instruction* inst = &(*instItr);
        for (inst_iterator allItr = inst_begin(function), E2 = inst_end(function); allItr != E2; ++allItr)
        {
          dominatorMap[inst].push_back(&(*allItr));
        }
      }
    }

    // iteratively eliminate nodes that are not dominators:
    //
    // while changes in any Dom(n)
    //  for each n in N - {n0}:
    //    Dom(n) = {n} union with intersection over Dom(p) for all p in pred(n)
    bool changes = true;
    while (changes == true)
    {
      for (inst_iterator instItr = inst_begin(function), E = inst_end(function); instItr != E; ++instItr)
      {
        if (instItr != inst_begin(function))
        {
          Instruction* inst = &(*instItr);
          vector<Instruction*> predecessors;
          BasicBlock* currBB = inst->getParent();
          BasicBlock* prevBB = (inst->getParent())->getSinglePredecessor();

          // Check this BB and parent BB to find all predecessors
          if (currBB != NULL)
          {
            Instruction* prevInst = NULL;
            for (BasicBlock::iterator i = currBB->begin(), e = currBB->end(); i != e; ++i)
            {
              if (prevInst == NULL)
              {
                prevInst = &(*(i));
              }
              else
              {
                if (inst == &(*(i)))
                {
                  predecessors.push_back(prevInst);
                  break; // don't bother going father since we'll pass the instructon
                }
              }
            } 
          }
          else
          {
            cout << "ERROR: INSTRUCTION PARENT BASIC BLOCK CANNOT BE NULL." << endl;
          }

          if (prevBB != NULL)
          {
            Instruction* prevInst = NULL;
            for (BasicBlock::iterator i = prevBB->begin(), e = prevBB->end(); i != e; ++i)
            {
              if (prevInst == NULL)
              {
                prevInst = &(*(i));
              }
              else
              {
                if (inst == &(*(i)))
                {
                  predecessors.push_back(prevInst);
                  break; // don't bother going father since we'll pass the instructon
                }
              }
            } 
          }

          // Build the the intersection of all dominators of the instructions in predecessor...
          //    Dom(n) = {n} union with intersection over Dom(p) for all p in pred(n)
          vector<Instruction*> temp_intersect;
          vector<Instruction*> intersect;
          Instruction* candPred = *(predecessors.begin());
          for (vector<Instruction*>::iterator setItr = dominatorMap[candPred].begin(); setItr != dominatorMap[candPred].end(); setItr++)
          {
            temp_intersect.push_back(*setItr);
          }
          for (vector<Instruction*>::iterator predItr = temp_intersect.begin(); predItr != temp_intersect.end(); predItr++)
          {
            candPred = *predItr;
            bool inAllSets = true;

            // Traverse all predecessor dominators and see if this instruction is in each...
            for (vector<Instruction*>::iterator otherPredItr = predecessors.begin(); otherPredItr != predecessors.end(); otherPredItr++)
            {
              Instruction* otherCandPred = *otherPredItr;
              if (otherPredItr != predecessors.begin()) 
              {
                bool inThisSet = false;
                // traverse over the domination set of this predecessor
                for (vector<Instruction*>::iterator setItr = dominatorMap[otherCandPred].begin(); setItr != dominatorMap[otherCandPred].end(); setItr++)
                {
                  Instruction* cand = *setItr;
                  if (cand == candPred)
                  {
                    inThisSet = true;
                  }
                }
                inAllSets = inThisSet ? true : false;
              }
            }

            if (inAllSets == true) // def of set intersection
            {
              intersect.push_back(candPred);
            }
          }

          // intersect is the intersection of dominators
          // TODO: set union here and check for change
        }
      }
    }
  }

  bool dominates(Instruction* I, Instruction* BI) // I dominates BI
  {
    for (vector<Instruction*>::iterator itr = dominatorMap[BI].begin(); itr != dominatorMap[BI].end(); itr++)
    {
      Instruction* tmp = *itr;
      if (tmp == I) // I is in the set of nodes that dominate BI, so I dominates BI
      {
        return true;
      }
    }
    return false;
  }
private:
};

// Helpful class/container for CFG subpaths - just a set of ordered pairs/edges
class GraphPath
{
public:
  // containers and fields
  vector<GraphEdge> edges;
  vector<int> weights;
  vector<const BasicBlock*> nodes;
  map<Value*, bool> valueContainsMap;
  map<Value*, int> valueBlockMap;
  bool fromStart;
  // double freq;

  // void addBlock(const BasicBlock* blk)
  // {
    // for (BasicBlock::iterator i = blk->begin(), e = blk->end(); i != e; ++i)
    // {
    //   Instruction* inst = *i;

    // }
  // }

  // methods
  // TODO: buildSubgraph() = returns new instance of this class, or emtpy

  GraphPath* buildSubPath(unsigned int i) // return subpath from index i to end of the path
  {
    if (i < nodes.size())
    {
      GraphPath* subPath = new GraphPath();
      if (i != 0) subPath->fromStart = false;
      cout << "Nodes Edges Weights" << endl;
      cout << nodes.size() << " " << edges.size() << " " << weights.size() << endl;
      for (unsigned int index = i; index < nodes.size(); index++)
      {
        subPath->nodes.push_back(nodes.at(index));
        if (index != nodes.size() - 1)
        {
          subPath->edges.push_back(edges.at(index));
          subPath->weights.push_back(weights.at(index));
        }
      }

      return subPath;
    }
    else
    {
      return NULL;
    }
  }

  GraphPath* concat(GraphPath* p2)
  {
    GraphPath* newPath = new GraphPath();
    const BasicBlock* hinge;

    // append info from path 1
    for (unsigned int i = 0; i < nodes.size(); i++)
    {
      newPath->nodes.push_back(nodes.at(i));
      if (i != nodes.size() - 1)
      {
        newPath->edges.push_back(edges.at(i));
        newPath->weights.push_back(weights.at(i));
      }
      hinge = nodes.at(i);
    }

    // sanity check to make sure that the hinge matches the start of path2
    if (hinge != p2->nodes.at(0))
    {
      return NULL;
    }

    // append info from path 2
    for (unsigned int i = 0; i < p2->nodes.size(); i++)
    {
      newPath->nodes.push_back(p2->nodes.at(i));
      if (i != p2->nodes.size())
      {
        newPath->edges.push_back(p2->edges.at(i));
        newPath->weights.push_back(p2->weights.at(i));
      }
    }

    return newPath;
  }

  bool containsValue(Value* inst)
  {
    return valueContainsMap[inst]; // we shouldn't need to do any error checking since the map is assumed to be initialized correctly
  }

  void checkForValue(Value* val)
  {
    for (unsigned int i = 0; i < nodes.size(); i++)
    {
      const BasicBlock* blk = nodes.at(i);
      for (BasicBlock::const_iterator itr = blk->begin(), e = blk->end(); itr != e; ++itr)
      {
        Instruction* bbinst = const_cast<Instruction*>(&(*itr));
        Value* bbinstValue = dyn_cast<Value>(bbinst);

        // Check for exact match on the instruction result
        if (val == bbinstValue)
        {
          valueContainsMap[val] = true;
          valueBlockMap[val] = i;
          return;
        }

        // Check the operands now...
        for (unsigned int opi = 0; opi < bbinst->getNumOperands(); opi++)
        {
          Value* operandVal = bbinst->getOperand(opi);
          if (val == operandVal)
          {
            valueContainsMap[val] = true;
            valueBlockMap[val] = i;
            return;
          }
        }
      }
    }

    // wasn't in any of the basic blocks above... so set to false and return
    valueContainsMap[val] = false;
    valueBlockMap[val] = -1;
    return;
  }

  int freq() { // to me, using the minimum edge weight as the frequency of this whole path makes the most sense...
    int f = weights.at(0);
    for (unsigned int i = 1; i < weights.size(); i++)
    {
      if (weights.at(i) < f)
      {
        f = weights.at(i);
      }
    }
    return f;
  }

  int totalWeight() {
    int acc = 0;
    for (unsigned int i = 0; i < weights.size(); i++)
    {
      acc = acc + weights.at(i);
    }
    return acc;
  }
};

typedef pair<Value*, const BasicBlock*> ExpressionNodePair;

//===----------------------------------------------------------------------===//
//                         ValueTable Class
//===----------------------------------------------------------------------===//

/// This class holds the mapping between values and value numbers.  It is used
/// as an efficient mechanism to determine the expression-wise equivalence of
/// two values.
namespace {
  struct Expression {
    uint32_t opcode;
    Type *type;
    SmallVector<uint32_t, 4> varargs;

    Expression(uint32_t o = ~2U) : opcode(o) { }

    bool operator==(const Expression &other) const {
      if (opcode != other.opcode)
        return false;
      if (opcode == ~0U || opcode == ~1U)
        return true;
      if (type != other.type)
        return false;
      if (varargs != other.varargs)
        return false;
      return true;
    }

    friend hash_code hash_value(const Expression &Value) {
      return hash_combine(Value.opcode, Value.type,
                          hash_combine_range(Value.varargs.begin(),
                                             Value.varargs.end()));
    }
  };

  class ValueTable {
    DenseMap<Value*, uint32_t> valueNumbering;
    DenseMap<Expression, uint32_t> expressionNumbering;
    AliasAnalysis *AA;
    MemoryDependenceAnalysis *MD;
    DominatorTree *DT;

    uint32_t nextValueNumber;

    Expression create_expression(Instruction* I);
    Expression create_cmp_expression(unsigned Opcode,
                                     CmpInst::Predicate Predicate,
                                     Value *LHS, Value *RHS);
    Expression create_extractvalue_expression(ExtractValueInst* EI);
    uint32_t lookup_or_add_call(CallInst* C);
  public:
    ValueTable() : nextValueNumber(1) { }
    uint32_t lookup_or_add(Value *V);
    uint32_t lookup(Value *V) const;
    uint32_t lookup_or_add_cmp(unsigned Opcode, CmpInst::Predicate Pred,
                               Value *LHS, Value *RHS);
    void add(Value *V, uint32_t num);
    void clear();
    void erase(Value *v);
    void setAliasAnalysis(AliasAnalysis* A) { AA = A; }
    AliasAnalysis *getAliasAnalysis() const { return AA; }
    void setMemDep(MemoryDependenceAnalysis* M) { MD = M; }
    void setDomTree(DominatorTree* D) { DT = D; }
    uint32_t getNextUnusedValueNumber() { return nextValueNumber; }
    void verifyRemoved(const Value *) const;
  };
}

namespace llvm {
template <> struct DenseMapInfo<Expression> {
  static inline Expression getEmptyKey() {
    return ~0U;
  }

  static inline Expression getTombstoneKey() {
    return ~1U;
  }

  static unsigned getHashValue(const Expression e) {
    using llvm::hash_value;
    return static_cast<unsigned>(hash_value(e));
  }
  static bool isEqual(const Expression &LHS, const Expression &RHS) {
    return LHS == RHS;
  }
};

}

//===----------------------------------------------------------------------===//
//                     ValueTable Internal Functions
//===----------------------------------------------------------------------===//

Expression ValueTable::create_expression(Instruction *I) {
  Expression e;
  e.type = I->getType();
  e.opcode = I->getOpcode();
  for (Instruction::op_iterator OI = I->op_begin(), OE = I->op_end();
       OI != OE; ++OI)
    e.varargs.push_back(lookup_or_add(*OI));
  if (I->isCommutative()) {
    // Ensure that commutative instructions that only differ by a permutation
    // of their operands get the same value number by sorting the operand value
    // numbers.  Since all commutative instructions have two operands it is more
    // efficient to sort by hand rather than using, say, std::sort.
    assert(I->getNumOperands() == 2 && "Unsupported commutative instruction!");
    if (e.varargs[0] > e.varargs[1])
      std::swap(e.varargs[0], e.varargs[1]);
  }

  if (CmpInst *C = dyn_cast<CmpInst>(I)) {
    // Sort the operand value numbers so x<y and y>x get the same value number.
    CmpInst::Predicate Predicate = C->getPredicate();
    if (e.varargs[0] > e.varargs[1]) {
      std::swap(e.varargs[0], e.varargs[1]);
      Predicate = CmpInst::getSwappedPredicate(Predicate);
    }
    e.opcode = (C->getOpcode() << 8) | Predicate;
  } else if (InsertValueInst *E = dyn_cast<InsertValueInst>(I)) {
    for (InsertValueInst::idx_iterator II = E->idx_begin(), IE = E->idx_end();
         II != IE; ++II)
      e.varargs.push_back(*II);
  }

  return e;
}

Expression ValueTable::create_cmp_expression(unsigned Opcode,
                                             CmpInst::Predicate Predicate,
                                             Value *LHS, Value *RHS) {
  assert((Opcode == Instruction::ICmp || Opcode == Instruction::FCmp) &&
         "Not a comparison!");
  Expression e;
  e.type = CmpInst::makeCmpResultType(LHS->getType());
  e.varargs.push_back(lookup_or_add(LHS));
  e.varargs.push_back(lookup_or_add(RHS));

  // Sort the operand value numbers so x<y and y>x get the same value number.
  if (e.varargs[0] > e.varargs[1]) {
    std::swap(e.varargs[0], e.varargs[1]);
    Predicate = CmpInst::getSwappedPredicate(Predicate);
  }
  e.opcode = (Opcode << 8) | Predicate;
  return e;
}

Expression ValueTable::create_extractvalue_expression(ExtractValueInst *EI) {
  assert(EI != 0 && "Not an ExtractValueInst?");
  Expression e;
  e.type = EI->getType();
  e.opcode = 0;

  IntrinsicInst *I = dyn_cast<IntrinsicInst>(EI->getAggregateOperand());
  if (I != 0 && EI->getNumIndices() == 1 && *EI->idx_begin() == 0 ) {
    // EI might be an extract from one of our recognised intrinsics. If it
    // is we'll synthesize a semantically equivalent expression instead on
    // an extract value expression.
    switch (I->getIntrinsicID()) {
      case Intrinsic::sadd_with_overflow:
      case Intrinsic::uadd_with_overflow:
        e.opcode = Instruction::Add;
        break;
      case Intrinsic::ssub_with_overflow:
      case Intrinsic::usub_with_overflow:
        e.opcode = Instruction::Sub;
        break;
      case Intrinsic::smul_with_overflow:
      case Intrinsic::umul_with_overflow:
        e.opcode = Instruction::Mul;
        break;
      default:
        break;
    }

    if (e.opcode != 0) {
      // Intrinsic recognized. Grab its args to finish building the expression.
      assert(I->getNumArgOperands() == 2 &&
             "Expect two args for recognised intrinsics.");
      e.varargs.push_back(lookup_or_add(I->getArgOperand(0)));
      e.varargs.push_back(lookup_or_add(I->getArgOperand(1)));
      return e;
    }
  }

  // Not a recognised intrinsic. Fall back to producing an extract value
  // expression.
  e.opcode = EI->getOpcode();
  for (Instruction::op_iterator OI = EI->op_begin(), OE = EI->op_end();
       OI != OE; ++OI)
    e.varargs.push_back(lookup_or_add(*OI));

  for (ExtractValueInst::idx_iterator II = EI->idx_begin(), IE = EI->idx_end();
         II != IE; ++II)
    e.varargs.push_back(*II);

  return e;
}

//===----------------------------------------------------------------------===//
//                     ValueTable External Functions
//===----------------------------------------------------------------------===//

/// add - Insert a value into the table with a specified value number.
void ValueTable::add(Value *V, uint32_t num) {
  valueNumbering.insert(std::make_pair(V, num));
}

uint32_t ValueTable::lookup_or_add_call(CallInst *C) {
  if (AA->doesNotAccessMemory(C)) {
    Expression exp = create_expression(C);
    uint32_t &e = expressionNumbering[exp];
    if (!e) e = nextValueNumber++;
    valueNumbering[C] = e;
    return e;
  } else if (AA->onlyReadsMemory(C)) {
    Expression exp = create_expression(C);
    uint32_t &e = expressionNumbering[exp];
    if (!e) {
      e = nextValueNumber++;
      valueNumbering[C] = e;
      return e;
    }
    if (!MD) {
      e = nextValueNumber++;
      valueNumbering[C] = e;
      return e;
    }

    MemDepResult local_dep = MD->getDependency(C);

    if (!local_dep.isDef() && !local_dep.isNonLocal()) {
      valueNumbering[C] =  nextValueNumber;
      return nextValueNumber++;
    }

    if (local_dep.isDef()) {
      CallInst* local_cdep = cast<CallInst>(local_dep.getInst());

      if (local_cdep->getNumArgOperands() != C->getNumArgOperands()) {
        valueNumbering[C] = nextValueNumber;
        return nextValueNumber++;
      }

      for (unsigned i = 0, e = C->getNumArgOperands(); i < e; ++i) {
        uint32_t c_vn = lookup_or_add(C->getArgOperand(i));
        uint32_t cd_vn = lookup_or_add(local_cdep->getArgOperand(i));
        if (c_vn != cd_vn) {
          valueNumbering[C] = nextValueNumber;
          return nextValueNumber++;
        }
      }

      uint32_t v = lookup_or_add(local_cdep);
      valueNumbering[C] = v;
      return v;
    }

    // Non-local case.
    const MemoryDependenceAnalysis::NonLocalDepInfo &deps =
      MD->getNonLocalCallDependency(CallSite(C));
    // FIXME: Move the checking logic to MemDep!
    CallInst* cdep = 0;

    // Check to see if we have a single dominating call instruction that is
    // identical to C.
    for (unsigned i = 0, e = deps.size(); i != e; ++i) {
      const NonLocalDepEntry *I = &deps[i];
      if (I->getResult().isNonLocal())
        continue;

      // We don't handle non-definitions.  If we already have a call, reject
      // instruction dependencies.
      if (!I->getResult().isDef() || cdep != 0) {
        cdep = 0;
        break;
      }

      CallInst *NonLocalDepCall = dyn_cast<CallInst>(I->getResult().getInst());
      // FIXME: All duplicated with non-local case.
      if (NonLocalDepCall && DT->properlyDominates(I->getBB(), C->getParent())){
        cdep = NonLocalDepCall;
        continue;
      }

      cdep = 0;
      break;
    }

    if (!cdep) {
      valueNumbering[C] = nextValueNumber;
      return nextValueNumber++;
    }

    if (cdep->getNumArgOperands() != C->getNumArgOperands()) {
      valueNumbering[C] = nextValueNumber;
      return nextValueNumber++;
    }
    for (unsigned i = 0, e = C->getNumArgOperands(); i < e; ++i) {
      uint32_t c_vn = lookup_or_add(C->getArgOperand(i));
      uint32_t cd_vn = lookup_or_add(cdep->getArgOperand(i));
      if (c_vn != cd_vn) {
        valueNumbering[C] = nextValueNumber;
        return nextValueNumber++;
      }
    }

    uint32_t v = lookup_or_add(cdep);
    valueNumbering[C] = v;
    return v;

  } else {
    valueNumbering[C] = nextValueNumber;
    return nextValueNumber++;
  }
}

/// lookup_or_add - Returns the value number for the specified value, assigning
/// it a new number if it did not have one before.
uint32_t ValueTable::lookup_or_add(Value *V) {
  DenseMap<Value*, uint32_t>::iterator VI = valueNumbering.find(V);
  if (VI != valueNumbering.end())
    return VI->second;

  if (!isa<Instruction>(V)) {
    valueNumbering[V] = nextValueNumber;
    return nextValueNumber++;
  }

  Instruction* I = cast<Instruction>(V);
  Expression exp;
  switch (I->getOpcode()) {
    case Instruction::Call:
      return lookup_or_add_call(cast<CallInst>(I));
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::ICmp:
    case Instruction::FCmp:
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::PtrToInt:
    case Instruction::IntToPtr:
    case Instruction::BitCast:
    case Instruction::Select:
    case Instruction::ExtractElement:
    case Instruction::InsertElement:
    case Instruction::ShuffleVector:
    case Instruction::InsertValue:
    case Instruction::GetElementPtr:
      exp = create_expression(I);
      break;
    case Instruction::ExtractValue:
      exp = create_extractvalue_expression(cast<ExtractValueInst>(I));
      break;
    default:
      valueNumbering[V] = nextValueNumber;
      return nextValueNumber++;
  }

  uint32_t& e = expressionNumbering[exp];
  if (!e) e = nextValueNumber++;
  valueNumbering[V] = e;
  return e;
}

/// lookup - Returns the value number of the specified value. Fails if
/// the value has not yet been numbered.
uint32_t ValueTable::lookup(Value *V) const {
  DenseMap<Value*, uint32_t>::const_iterator VI = valueNumbering.find(V);
  assert(VI != valueNumbering.end() && "Value not numbered?");
  return VI->second;
}

/// lookup_or_add_cmp - Returns the value number of the given comparison,
/// assigning it a new number if it did not have one before.  Useful when
/// we deduced the result of a comparison, but don't immediately have an
/// instruction realizing that comparison to hand.
uint32_t ValueTable::lookup_or_add_cmp(unsigned Opcode,
                                       CmpInst::Predicate Predicate,
                                       Value *LHS, Value *RHS) {
  Expression exp = create_cmp_expression(Opcode, Predicate, LHS, RHS);
  uint32_t& e = expressionNumbering[exp];
  if (!e) e = nextValueNumber++;
  return e;
}

/// clear - Remove all entries from the ValueTable.
void ValueTable::clear() {
  valueNumbering.clear();
  expressionNumbering.clear();
  nextValueNumber = 1;
}

/// erase - Remove a value from the value numbering.
void ValueTable::erase(Value *V) {
  valueNumbering.erase(V);
}

/// verifyRemoved - Verify that the value is removed from all internal data
/// structures.
void ValueTable::verifyRemoved(const Value *V) const {
  for (DenseMap<Value*, uint32_t>::const_iterator
         I = valueNumbering.begin(), E = valueNumbering.end(); I != E; ++I) {
    assert(I->first != V && "Inst still occurs in value numbering map!");
  }
}

//===----------------------------------------------------------------------===//
//                                PgoPre Pass
//===----------------------------------------------------------------------===//

namespace {
  class PgoPre;
  struct AvailableValueInBlock {
    /// BB - The basic block in question.
    BasicBlock *BB;
    enum ValType {
      SimpleVal,  // A simple offsetted value that is accessed.
      LoadVal,    // A value produced by a load.
      MemIntrin   // A memory intrinsic which is loaded from.
    };
  
    /// V - The value that is live out of the block.
    PointerIntPair<Value *, 2, ValType> Val;
  
    /// Offset - The byte offset in Val that is interesting for the load query.
    unsigned Offset;
  
    static AvailableValueInBlock get(BasicBlock *BB, Value *V,
                                     unsigned Offset = 0) {
      AvailableValueInBlock Res;
      Res.BB = BB;
      Res.Val.setPointer(V);
      Res.Val.setInt(SimpleVal);
      Res.Offset = Offset;
      return Res;
    }
  
    static AvailableValueInBlock getMI(BasicBlock *BB, MemIntrinsic *MI,
                                       unsigned Offset = 0) {
      AvailableValueInBlock Res;
      Res.BB = BB;
      Res.Val.setPointer(MI);
      Res.Val.setInt(MemIntrin);
      Res.Offset = Offset;
      return Res;
    }
  
    static AvailableValueInBlock getLoad(BasicBlock *BB, LoadInst *LI,
                                         unsigned Offset = 0) {
      AvailableValueInBlock Res;
      Res.BB = BB;
      Res.Val.setPointer(LI);
      Res.Val.setInt(LoadVal);
      Res.Offset = Offset;
      return Res;
    }
  
    bool isSimpleValue() const { return Val.getInt() == SimpleVal; }
    bool isCoercedLoadValue() const { return Val.getInt() == LoadVal; }
    bool isMemIntrinValue() const { return Val.getInt() == MemIntrin; }
  
    Value *getSimpleValue() const {
      assert(isSimpleValue() && "Wrong accessor");
      return Val.getPointer();
    }
  
    LoadInst *getCoercedLoadValue() const {
      assert(isCoercedLoadValue() && "Wrong accessor");
      return cast<LoadInst>(Val.getPointer());
    }
  
    MemIntrinsic *getMemIntrinValue() const {
      assert(isMemIntrinValue() && "Wrong accessor");
      return cast<MemIntrinsic>(Val.getPointer());
    }
  
    /// MaterializeAdjustedValue - Emit code into this block to adjust the value
    /// defined here to the specified type.  This handles various coercion cases.
    Value *MaterializeAdjustedValue(Type *LoadTy, PgoPre &gvn) const;
  };

  class PgoPre : public FunctionPass {
    bool NoLoads;
    MemoryDependenceAnalysis *MD;
    DominatorTree *DT;
    const DataLayout *TD;
    const TargetLibraryInfo *TLI;

    ValueTable VN;

    /// LeaderTable - A mapping from value numbers to lists of Value*'s that
    /// have that value number.  Use findLeader to query it.
    struct LeaderTableEntry {
      Value *Val;
      const BasicBlock *BB;
      LeaderTableEntry *Next;
    };
    DenseMap<uint32_t, LeaderTableEntry> LeaderTable;
    BumpPtrAllocator TableAllocator;

    SmallVector<Instruction*, 8> InstrsToErase;

    typedef SmallVector<NonLocalDepResult, 64> LoadDepVect;
    typedef SmallVector<AvailableValueInBlock, 64> AvailValInBlkVect;
    typedef SmallVector<BasicBlock*, 64> UnavailBlkVect;

  public:

    // New content fancy shmancy
    ProfileInfo *PI;
    vector<GraphPath*> paths;
    DominatorSet *DS;

    // Block information - Map basic blocks in a function back and forth to
    // unsigned integers.
    std::vector<const BasicBlock*> BlockMapping;
    map<const BasicBlock*, unsigned> BlockNumbering;

     // Containers of CFG paths (and subpaths)
    // TODO: this should really be a map from pairs of (expressions/instructions exp, block n) to a path
    map<ExpressionNodePair, vector<GraphPath*> > AvailableSubPaths;
    map<ExpressionNodePair, vector<GraphPath*> > UnAvailableSubPaths;
    map<ExpressionNodePair, vector<GraphPath*> > AnticipableSubPaths;
    map<ExpressionNodePair, vector<GraphPath*> > UnAnticipableSubPaths;
    map<ExpressionNodePair, vector<GraphPath*> > BenefitPaths;
    map<ExpressionNodePair, vector<GraphPath*> > CostPaths;
    map<const BasicBlock*, int> BlockFreqMap;

    bool ProcessBlock(const BasicBlock *BB);

    // Functions to compute the cost and benefit of a path
    // it is assume profile information is in scope and can be used for these calculations
    int Cost(Value* val, const BasicBlock* n);
    int Benefit(Value* val, const BasicBlock* n);

    // Functions for enabling/disabling speculation at certain spots in the function CFG
    double ProbCost(Value* val, const BasicBlock* n);
    double ProbBenefit(Value* val, const BasicBlock* n);
    bool EnableSpec(Value* val, const BasicBlock* n);

    static char ID; // Pass identification, replacement for typeid
    explicit PgoPre() : FunctionPass(ID) { //, NoLoads(false), MD(0) 
      initializePgoPrePass(*PassRegistry::getPassRegistry());
    }

    //   struct PgoPre : public FunctionPass {
    // static char ID; // Pass identification, replacement for typeid
    // PgoPre() : FunctionPass(ID) {
    //   initializePgoPrePass(*PassRegistry::getPassRegistry());
    // }

    bool runOnFunction(Function &F);

    /// markInstructionForDeletion - This removes the specified instruction from
    /// our various maps and marks it for deletion.
    void markInstructionForDeletion(Instruction *I) {
      VN.erase(I);
      InstrsToErase.push_back(I);
    }

    const DataLayout *getDataLayout() const { return TD; }
    DominatorTree &getDominatorTree() const { return *DT; }
    AliasAnalysis *getAliasAnalysis() const { return VN.getAliasAnalysis(); }
    MemoryDependenceAnalysis &getMemDep() const { return *MD; }
  private:
    /// addToLeaderTable - Push a new Value to the LeaderTable onto the list for
    /// its value number.
    void addToLeaderTable(uint32_t N, Value *V, const BasicBlock *BB) {
      LeaderTableEntry &Curr = LeaderTable[N];
      if (!Curr.Val) {
        Curr.Val = V;
        Curr.BB = BB;
        return;
      }

      LeaderTableEntry *Node = TableAllocator.Allocate<LeaderTableEntry>();
      Node->Val = V;
      Node->BB = BB;
      Node->Next = Curr.Next;
      Curr.Next = Node;
    }

    /// removeFromLeaderTable - Scan the list of values corresponding to a given
    /// value number, and remove the given instruction if encountered.
    void removeFromLeaderTable(uint32_t N, Instruction *I, BasicBlock *BB) {
      LeaderTableEntry* Prev = 0;
      LeaderTableEntry* Curr = &LeaderTable[N];

      while (Curr->Val != I || Curr->BB != BB) {
        Prev = Curr;
        Curr = Curr->Next;
      }

      if (Prev) {
        Prev->Next = Curr->Next;
      } else {
        if (!Curr->Next) {
          Curr->Val = 0;
          Curr->BB = 0;
        } else {
          LeaderTableEntry* Next = Curr->Next;
          Curr->Val = Next->Val;
          Curr->BB = Next->BB;
          Curr->Next = Next->Next;
        }
      }
    }

    // List of critical edges to be split between iterations.
    SmallVector<std::pair<TerminatorInst*, unsigned>, 4> toSplit;

    // This transformation requires dominator postdominator info
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<DominatorTree>();
      AU.addRequired<TargetLibraryInfo>();
      if (!NoLoads)
        AU.addRequired<MemoryDependenceAnalysis>();
      AU.addRequired<AliasAnalysis>();

      AU.addPreserved<DominatorTree>();
      AU.addPreserved<AliasAnalysis>();

      // AU.setPreservesCFG();
      AU.addRequired<ProfileInfo>();
    }


    // Helper fuctions of redundant load elimination 
    bool processLoad(LoadInst *L);
    bool processNonLocalLoad(LoadInst *L);
    void AnalyzeLoadAvailability(LoadInst *LI, LoadDepVect &Deps, 
                                 AvailValInBlkVect &ValuesPerBlock,
                                 UnavailBlkVect &UnavailableBlocks);
    bool PerformLoadPgoPre(LoadInst *LI, AvailValInBlkVect &ValuesPerBlock, 
                        UnavailBlkVect &UnavailableBlocks);

    // Other helper routines
    bool processInstruction(Instruction *I);
    bool processBlock(BasicBlock *BB);
    void dump(DenseMap<uint32_t, Value*> &d);
    bool iterateOnFunction(Function &F);
    bool performPgoPre(Function &F);
    Value *findLeader(const BasicBlock *BB, uint32_t num);
    void cleanupGlobalSets();
    void verifyRemoved(const Instruction *I) const;
    bool splitCriticalEdges();
    unsigned replaceAllDominatedUsesWith(Value *From, Value *To,
                                         const BasicBlockEdge &Root);
    bool propagateEquality(Value *LHS, Value *RHS, const BasicBlockEdge &Root);
  };

  char PgoPre::ID = 0;
}

// createPgoPrePass - The public interface to this file...
FunctionPass *llvm::createPgoPrePass() {
  return new PgoPre();
}

INITIALIZE_PASS_BEGIN(PgoPre, "pgo-pre", "PgoPre", false, false)
INITIALIZE_PASS_DEPENDENCY(MemoryDependenceAnalysis)
INITIALIZE_PASS_DEPENDENCY(DominatorTree)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfo)
INITIALIZE_AG_DEPENDENCY(ProfileInfo) // don't forget to mark profile data as dependency
INITIALIZE_AG_DEPENDENCY(AliasAnalysis)
INITIALIZE_PASS_END(PgoPre, "pgo-pre", "PgoPre", false, false)

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void PgoPre::dump(DenseMap<uint32_t, Value*>& d) {
  errs() << "{\n";
  for (DenseMap<uint32_t, Value*>::iterator I = d.begin(),
       E = d.end(); I != E; ++I) {
      errs() << I->first << "\n";
      I->second->dump();
  }
  errs() << "}\n";
}
#endif

/// IsValueFullyAvailableInBlock - Return true if we can prove that the value
/// we're analyzing is fully available in the specified block.  As we go, keep
/// track of which blocks we know are fully alive in FullyAvailableBlocks.  This
/// map is actually a tri-state map with the following values:
///   0) we know the block *is not* fully available.
///   1) we know the block *is* fully available.
///   2) we do not know whether the block is fully available or not, but we are
///      currently speculating that it will be.
///   3) we are speculating for this block and have used that to speculate for
///      other blocks.
static bool IsValueFullyAvailableInBlock(BasicBlock *BB,
                            DenseMap<BasicBlock*, char> &FullyAvailableBlocks,
                            uint32_t RecurseDepth) {
  if (RecurseDepth > MaxRecurseDepth)
    return false;

  // Optimistically assume that the block is fully available and check to see
  // if we already know about this block in one lookup.
  std::pair<DenseMap<BasicBlock*, char>::iterator, char> IV =
    FullyAvailableBlocks.insert(std::make_pair(BB, 2));

  // If the entry already existed for this block, return the precomputed value.
  if (!IV.second) {
    // If this is a speculative "available" value, mark it as being used for
    // speculation of other blocks.
    if (IV.first->second == 2)
      IV.first->second = 3;
    return IV.first->second != 0;
  }

  // Otherwise, see if it is fully available in all predecessors.
  pred_iterator PI = pred_begin(BB), PE = pred_end(BB);

  // If this block has no predecessors, it isn't live-in here.
  if (PI == PE)
    goto SpeculationFailure;

  for (; PI != PE; ++PI)
    // If the value isn't fully available in one of our predecessors, then it
    // isn't fully available in this block either.  Undo our previous
    // optimistic assumption and bail out.
    if (!IsValueFullyAvailableInBlock(*PI, FullyAvailableBlocks,RecurseDepth+1))
      goto SpeculationFailure;

  return true;

// SpeculationFailure - If we get here, we found out that this is not, after
// all, a fully-available block.  We have a problem if we speculated on this and
// used the speculation to mark other blocks as available.
SpeculationFailure:
  char &BBVal = FullyAvailableBlocks[BB];

  // If we didn't speculate on this, just return with it set to false.
  if (BBVal == 2) {
    BBVal = 0;
    return false;
  }

  // If we did speculate on this value, we could have blocks set to 1 that are
  // incorrect.  Walk the (transitive) successors of this block and mark them as
  // 0 if set to one.
  SmallVector<BasicBlock*, 32> BBWorklist;
  BBWorklist.push_back(BB);

  do {
    BasicBlock *Entry = BBWorklist.pop_back_val();
    // Note that this sets blocks to 0 (unavailable) if they happen to not
    // already be in FullyAvailableBlocks.  This is safe.
    char &EntryVal = FullyAvailableBlocks[Entry];
    if (EntryVal == 0) continue;  // Already unavailable.

    // Mark as unavailable.
    EntryVal = 0;

    for (succ_iterator I = succ_begin(Entry), E = succ_end(Entry); I != E; ++I)
      BBWorklist.push_back(*I);
  } while (!BBWorklist.empty());

  return false;
}


/// CanCoerceMustAliasedValueToLoad - Return true if
/// CoerceAvailableValueToLoadType will succeed.
static bool CanCoerceMustAliasedValueToLoad(Value *StoredVal,
                                            Type *LoadTy,
                                            const DataLayout &TD) {
  // If the loaded or stored value is an first class array or struct, don't try
  // to transform them.  We need to be able to bitcast to integer.
  if (LoadTy->isStructTy() || LoadTy->isArrayTy() ||
      StoredVal->getType()->isStructTy() ||
      StoredVal->getType()->isArrayTy())
    return false;

  // The store has to be at least as big as the load.
  if (TD.getTypeSizeInBits(StoredVal->getType()) <
        TD.getTypeSizeInBits(LoadTy))
    return false;

  return true;
}

/// CoerceAvailableValueToLoadType - If we saw a store of a value to memory, and
/// then a load from a must-aliased pointer of a different type, try to coerce
/// the stored value.  LoadedTy is the type of the load we want to replace and
/// InsertPt is the place to insert new instructions.
///
/// If we can't do it, return null.
static Value *CoerceAvailableValueToLoadType(Value *StoredVal,
                                             Type *LoadedTy,
                                             Instruction *InsertPt,
                                             const DataLayout &TD) {
  if (!CanCoerceMustAliasedValueToLoad(StoredVal, LoadedTy, TD))
    return 0;

  // If this is already the right type, just return it.
  Type *StoredValTy = StoredVal->getType();

  uint64_t StoreSize = TD.getTypeSizeInBits(StoredValTy);
  uint64_t LoadSize = TD.getTypeSizeInBits(LoadedTy);

  // If the store and reload are the same size, we can always reuse it.
  if (StoreSize == LoadSize) {
    // Pointer to Pointer -> use bitcast.
    if (StoredValTy->getScalarType()->isPointerTy() &&
        LoadedTy->getScalarType()->isPointerTy())
      return new BitCastInst(StoredVal, LoadedTy, "", InsertPt);

    // Convert source pointers to integers, which can be bitcast.
    if (StoredValTy->getScalarType()->isPointerTy()) {
      StoredValTy = TD.getIntPtrType(StoredValTy);
      StoredVal = new PtrToIntInst(StoredVal, StoredValTy, "", InsertPt);
    }

    Type *TypeToCastTo = LoadedTy;
    if (TypeToCastTo->getScalarType()->isPointerTy())
      TypeToCastTo = TD.getIntPtrType(TypeToCastTo);

    if (StoredValTy != TypeToCastTo)
      StoredVal = new BitCastInst(StoredVal, TypeToCastTo, "", InsertPt);

    // Cast to pointer if the load needs a pointer type.
    if (LoadedTy->getScalarType()->isPointerTy())
      StoredVal = new IntToPtrInst(StoredVal, LoadedTy, "", InsertPt);

    return StoredVal;
  }

  // If the loaded value is smaller than the available value, then we can
  // extract out a piece from it.  If the available value is too small, then we
  // can't do anything.
  assert(StoreSize >= LoadSize && "CanCoerceMustAliasedValueToLoad fail");

  // Convert source pointers to integers, which can be manipulated.
  if (StoredValTy->getScalarType()->isPointerTy()) {
    StoredValTy = TD.getIntPtrType(StoredValTy);
    StoredVal = new PtrToIntInst(StoredVal, StoredValTy, "", InsertPt);
  }

  // Convert vectors and fp to integer, which can be manipulated.
  if (!StoredValTy->isIntegerTy()) {
    StoredValTy = IntegerType::get(StoredValTy->getContext(), StoreSize);
    StoredVal = new BitCastInst(StoredVal, StoredValTy, "", InsertPt);
  }

  // If this is a big-endian system, we need to shift the value down to the low
  // bits so that a truncate will work.
  if (TD.isBigEndian()) {
    Constant *Val = ConstantInt::get(StoredVal->getType(), StoreSize-LoadSize);
    StoredVal = BinaryOperator::CreateLShr(StoredVal, Val, "tmp", InsertPt);
  }

  // Truncate the integer to the right size now.
  Type *NewIntTy = IntegerType::get(StoredValTy->getContext(), LoadSize);
  StoredVal = new TruncInst(StoredVal, NewIntTy, "trunc", InsertPt);

  if (LoadedTy == NewIntTy)
    return StoredVal;

  // If the result is a pointer, inttoptr.
  if (LoadedTy->getScalarType()->isPointerTy())
    return new IntToPtrInst(StoredVal, LoadedTy, "inttoptr", InsertPt);

  // Otherwise, bitcast.
  return new BitCastInst(StoredVal, LoadedTy, "bitcast", InsertPt);
}

/// AnalyzeLoadFromClobberingWrite - This function is called when we have a
/// memdep query of a load that ends up being a clobbering memory write (store,
/// memset, memcpy, memmove).  This means that the write *may* provide bits used
/// by the load but we can't be sure because the pointers don't mustalias.
///
/// Check this case to see if there is anything more we can do before we give
/// up.  This returns -1 if we have to give up, or a byte number in the stored
/// value of the piece that feeds the load.
static int AnalyzeLoadFromClobberingWrite(Type *LoadTy, Value *LoadPtr,
                                          Value *WritePtr,
                                          uint64_t WriteSizeInBits,
                                          const DataLayout &TD) {
  // If the loaded or stored value is a first class array or struct, don't try
  // to transform them.  We need to be able to bitcast to integer.
  if (LoadTy->isStructTy() || LoadTy->isArrayTy())
    return -1;

  int64_t StoreOffset = 0, LoadOffset = 0;
  Value *StoreBase = GetPointerBaseWithConstantOffset(WritePtr,StoreOffset,&TD);
  Value *LoadBase = GetPointerBaseWithConstantOffset(LoadPtr, LoadOffset, &TD);
  if (StoreBase != LoadBase)
    return -1;

  // If the load and store are to the exact same address, they should have been
  // a must alias.  AA must have gotten confused.
  // FIXME: Study to see if/when this happens.  One case is forwarding a memset
  // to a load from the base of the memset.
#if 0
  if (LoadOffset == StoreOffset) {
    dbgs() << "STORE/LOAD DEP WITH COMMON POINTER MISSED:\n"
    << "Base       = " << *StoreBase << "\n"
    << "Store Ptr  = " << *WritePtr << "\n"
    << "Store Offs = " << StoreOffset << "\n"
    << "Load Ptr   = " << *LoadPtr << "\n";
    abort();
  }
#endif

  // If the load and store don't overlap at all, the store doesn't provide
  // anything to the load.  In this case, they really don't alias at all, AA
  // must have gotten confused.
  uint64_t LoadSize = TD.getTypeSizeInBits(LoadTy);

  if ((WriteSizeInBits & 7) | (LoadSize & 7))
    return -1;
  uint64_t StoreSize = WriteSizeInBits >> 3;  // Convert to bytes.
  LoadSize >>= 3;


  bool isAAFailure = false;
  if (StoreOffset < LoadOffset)
    isAAFailure = StoreOffset+int64_t(StoreSize) <= LoadOffset;
  else
    isAAFailure = LoadOffset+int64_t(LoadSize) <= StoreOffset;

  if (isAAFailure) {
#if 0
    dbgs() << "STORE LOAD DEP WITH COMMON BASE:\n"
    << "Base       = " << *StoreBase << "\n"
    << "Store Ptr  = " << *WritePtr << "\n"
    << "Store Offs = " << StoreOffset << "\n"
    << "Load Ptr   = " << *LoadPtr << "\n";
    abort();
#endif
    return -1;
  }

  // If the Load isn't completely contained within the stored bits, we don't
  // have all the bits to feed it.  We could do something crazy in the future
  // (issue a smaller load then merge the bits in) but this seems unlikely to be
  // valuable.
  if (StoreOffset > LoadOffset ||
      StoreOffset+StoreSize < LoadOffset+LoadSize)
    return -1;

  // Okay, we can do this transformation.  Return the number of bytes into the
  // store that the load is.
  return LoadOffset-StoreOffset;
}

/// AnalyzeLoadFromClobberingStore - This function is called when we have a
/// memdep query of a load that ends up being a clobbering store.
static int AnalyzeLoadFromClobberingStore(Type *LoadTy, Value *LoadPtr,
                                          StoreInst *DepSI,
                                          const DataLayout &TD) {
  // Cannot handle reading from store of first-class aggregate yet.
  if (DepSI->getValueOperand()->getType()->isStructTy() ||
      DepSI->getValueOperand()->getType()->isArrayTy())
    return -1;

  Value *StorePtr = DepSI->getPointerOperand();
  uint64_t StoreSize =TD.getTypeSizeInBits(DepSI->getValueOperand()->getType());
  return AnalyzeLoadFromClobberingWrite(LoadTy, LoadPtr,
                                        StorePtr, StoreSize, TD);
}

/// AnalyzeLoadFromClobberingLoad - This function is called when we have a
/// memdep query of a load that ends up being clobbered by another load.  See if
/// the other load can feed into the second load.
static int AnalyzeLoadFromClobberingLoad(Type *LoadTy, Value *LoadPtr,
                                         LoadInst *DepLI, const DataLayout &TD){
  // Cannot handle reading from store of first-class aggregate yet.
  if (DepLI->getType()->isStructTy() || DepLI->getType()->isArrayTy())
    return -1;

  Value *DepPtr = DepLI->getPointerOperand();
  uint64_t DepSize = TD.getTypeSizeInBits(DepLI->getType());
  int R = AnalyzeLoadFromClobberingWrite(LoadTy, LoadPtr, DepPtr, DepSize, TD);
  if (R != -1) return R;

  // If we have a load/load clobber an DepLI can be widened to cover this load,
  // then we should widen it!
  int64_t LoadOffs = 0;
  const Value *LoadBase =
    GetPointerBaseWithConstantOffset(LoadPtr, LoadOffs, &TD);
  unsigned LoadSize = TD.getTypeStoreSize(LoadTy);

  unsigned Size = MemoryDependenceAnalysis::
    getLoadLoadClobberFullWidthSize(LoadBase, LoadOffs, LoadSize, DepLI, TD);
  if (Size == 0) return -1;

  return AnalyzeLoadFromClobberingWrite(LoadTy, LoadPtr, DepPtr, Size*8, TD);
}



static int AnalyzeLoadFromClobberingMemInst(Type *LoadTy, Value *LoadPtr,
                                            MemIntrinsic *MI,
                                            const DataLayout &TD) {
  // If the mem operation is a non-constant size, we can't handle it.
  ConstantInt *SizeCst = dyn_cast<ConstantInt>(MI->getLength());
  if (SizeCst == 0) return -1;
  uint64_t MemSizeInBits = SizeCst->getZExtValue()*8;

  // If this is memset, we just need to see if the offset is valid in the size
  // of the memset..
  if (MI->getIntrinsicID() == Intrinsic::memset)
    return AnalyzeLoadFromClobberingWrite(LoadTy, LoadPtr, MI->getDest(),
                                          MemSizeInBits, TD);

  // If we have a memcpy/memmove, the only case we can handle is if this is a
  // copy from constant memory.  In that case, we can read directly from the
  // constant memory.
  MemTransferInst *MTI = cast<MemTransferInst>(MI);

  Constant *Src = dyn_cast<Constant>(MTI->getSource());
  if (Src == 0) return -1;

  GlobalVariable *GV = dyn_cast<GlobalVariable>(GetUnderlyingObject(Src, &TD));
  if (GV == 0 || !GV->isConstant()) return -1;

  // See if the access is within the bounds of the transfer.
  int Offset = AnalyzeLoadFromClobberingWrite(LoadTy, LoadPtr,
                                              MI->getDest(), MemSizeInBits, TD);
  if (Offset == -1)
    return Offset;

  // Otherwise, see if we can constant fold a load from the constant with the
  // offset applied as appropriate.
  Src = ConstantExpr::getBitCast(Src,
                                 llvm::Type::getInt8PtrTy(Src->getContext()));
  Constant *OffsetCst =
    ConstantInt::get(Type::getInt64Ty(Src->getContext()), (unsigned)Offset);
  Src = ConstantExpr::getGetElementPtr(Src, OffsetCst);
  Src = ConstantExpr::getBitCast(Src, PointerType::getUnqual(LoadTy));
  if (ConstantFoldLoadFromConstPtr(Src, &TD))
    return Offset;
  return -1;
}


/// GetStoreValueForLoad - This function is called when we have a
/// memdep query of a load that ends up being a clobbering store.  This means
/// that the store provides bits used by the load but we the pointers don't
/// mustalias.  Check this case to see if there is anything more we can do
/// before we give up.
static Value *GetStoreValueForLoad(Value *SrcVal, unsigned Offset,
                                   Type *LoadTy,
                                   Instruction *InsertPt, const DataLayout &TD){
  LLVMContext &Ctx = SrcVal->getType()->getContext();

  uint64_t StoreSize = (TD.getTypeSizeInBits(SrcVal->getType()) + 7) / 8;
  uint64_t LoadSize = (TD.getTypeSizeInBits(LoadTy) + 7) / 8;

  IRBuilder<> Builder(InsertPt->getParent(), InsertPt);

  // Compute which bits of the stored value are being used by the load.  Convert
  // to an integer type to start with.
  if (SrcVal->getType()->getScalarType()->isPointerTy())
    SrcVal = Builder.CreatePtrToInt(SrcVal,
        TD.getIntPtrType(SrcVal->getType()));
  if (!SrcVal->getType()->isIntegerTy())
    SrcVal = Builder.CreateBitCast(SrcVal, IntegerType::get(Ctx, StoreSize*8));

  // Shift the bits to the least significant depending on endianness.
  unsigned ShiftAmt;
  if (TD.isLittleEndian())
    ShiftAmt = Offset*8;
  else
    ShiftAmt = (StoreSize-LoadSize-Offset)*8;

  if (ShiftAmt)
    SrcVal = Builder.CreateLShr(SrcVal, ShiftAmt);

  if (LoadSize != StoreSize)
    SrcVal = Builder.CreateTrunc(SrcVal, IntegerType::get(Ctx, LoadSize*8));

  return CoerceAvailableValueToLoadType(SrcVal, LoadTy, InsertPt, TD);
}

/// GetLoadValueForLoad - This function is called when we have a
/// memdep query of a load that ends up being a clobbering load.  This means
/// that the load *may* provide bits used by the load but we can't be sure
/// because the pointers don't mustalias.  Check this case to see if there is
/// anything more we can do before we give up.
static Value *GetLoadValueForLoad(LoadInst *SrcVal, unsigned Offset,
                                  Type *LoadTy, Instruction *InsertPt,
                                  PgoPre &gvn) {
  const DataLayout &TD = *gvn.getDataLayout();
  // If Offset+LoadTy exceeds the size of SrcVal, then we must be wanting to
  // widen SrcVal out to a larger load.
  unsigned SrcValSize = TD.getTypeStoreSize(SrcVal->getType());
  unsigned LoadSize = TD.getTypeStoreSize(LoadTy);
  if (Offset+LoadSize > SrcValSize) {
    assert(SrcVal->isSimple() && "Cannot widen volatile/atomic load!");
    assert(SrcVal->getType()->isIntegerTy() && "Can't widen non-integer load");
    // If we have a load/load clobber an DepLI can be widened to cover this
    // load, then we should widen it to the next power of 2 size big enough!
    unsigned NewLoadSize = Offset+LoadSize;
    if (!isPowerOf2_32(NewLoadSize))
      NewLoadSize = NextPowerOf2(NewLoadSize);

    Value *PtrVal = SrcVal->getPointerOperand();

    // Insert the new load after the old load.  This ensures that subsequent
    // memdep queries will find the new load.  We can't easily remove the old
    // load completely because it is already in the value numbering table.
    IRBuilder<> Builder(SrcVal->getParent(), ++BasicBlock::iterator(SrcVal));
    Type *DestPTy =
      IntegerType::get(LoadTy->getContext(), NewLoadSize*8);
    DestPTy = PointerType::get(DestPTy,
                       cast<PointerType>(PtrVal->getType())->getAddressSpace());
    Builder.SetCurrentDebugLocation(SrcVal->getDebugLoc());
    PtrVal = Builder.CreateBitCast(PtrVal, DestPTy);
    LoadInst *NewLoad = Builder.CreateLoad(PtrVal);
    NewLoad->takeName(SrcVal);
    NewLoad->setAlignment(SrcVal->getAlignment());

    DEBUG(dbgs() << "PgoPre WIDENED LOAD: " << *SrcVal << "\n");
    DEBUG(dbgs() << "TO: " << *NewLoad << "\n");

    // Replace uses of the original load with the wider load.  On a big endian
    // system, we need to shift down to get the relevant bits.
    Value *RV = NewLoad;
    if (TD.isBigEndian())
      RV = Builder.CreateLShr(RV,
                    NewLoadSize*8-SrcVal->getType()->getPrimitiveSizeInBits());
    RV = Builder.CreateTrunc(RV, SrcVal->getType());
    SrcVal->replaceAllUsesWith(RV);

    // We would like to use gvn.markInstructionForDeletion here, but we can't
    // because the load is already memoized into the leader map table that PgoPre
    // tracks.  It is potentially possible to remove the load from the table,
    // but then there all of the operations based on it would need to be
    // rehashed.  Just leave the dead load around.
    gvn.getMemDep().removeInstruction(SrcVal);
    SrcVal = NewLoad;
  }

  return GetStoreValueForLoad(SrcVal, Offset, LoadTy, InsertPt, TD);
}


/// GetMemInstValueForLoad - This function is called when we have a
/// memdep query of a load that ends up being a clobbering mem intrinsic.
static Value *GetMemInstValueForLoad(MemIntrinsic *SrcInst, unsigned Offset,
                                     Type *LoadTy, Instruction *InsertPt,
                                     const DataLayout &TD){
  LLVMContext &Ctx = LoadTy->getContext();
  uint64_t LoadSize = TD.getTypeSizeInBits(LoadTy)/8;

  IRBuilder<> Builder(InsertPt->getParent(), InsertPt);

  // We know that this method is only called when the mem transfer fully
  // provides the bits for the load.
  if (MemSetInst *MSI = dyn_cast<MemSetInst>(SrcInst)) {
    // memset(P, 'x', 1234) -> splat('x'), even if x is a variable, and
    // independently of what the offset is.
    Value *Val = MSI->getValue();
    if (LoadSize != 1)
      Val = Builder.CreateZExt(Val, IntegerType::get(Ctx, LoadSize*8));

    Value *OneElt = Val;

    // Splat the value out to the right number of bits.
    for (unsigned NumBytesSet = 1; NumBytesSet != LoadSize; ) {
      // If we can double the number of bytes set, do it.
      if (NumBytesSet*2 <= LoadSize) {
        Value *ShVal = Builder.CreateShl(Val, NumBytesSet*8);
        Val = Builder.CreateOr(Val, ShVal);
        NumBytesSet <<= 1;
        continue;
      }

      // Otherwise insert one byte at a time.
      Value *ShVal = Builder.CreateShl(Val, 1*8);
      Val = Builder.CreateOr(OneElt, ShVal);
      ++NumBytesSet;
    }

    return CoerceAvailableValueToLoadType(Val, LoadTy, InsertPt, TD);
  }

  // Otherwise, this is a memcpy/memmove from a constant global.
  MemTransferInst *MTI = cast<MemTransferInst>(SrcInst);
  Constant *Src = cast<Constant>(MTI->getSource());

  // Otherwise, see if we can constant fold a load from the constant with the
  // offset applied as appropriate.
  Src = ConstantExpr::getBitCast(Src,
                                 llvm::Type::getInt8PtrTy(Src->getContext()));
  Constant *OffsetCst =
  ConstantInt::get(Type::getInt64Ty(Src->getContext()), (unsigned)Offset);
  Src = ConstantExpr::getGetElementPtr(Src, OffsetCst);
  Src = ConstantExpr::getBitCast(Src, PointerType::getUnqual(LoadTy));
  return ConstantFoldLoadFromConstPtr(Src, &TD);
}


/// ConstructSSAForLoadSet - Given a set of loads specified by ValuesPerBlock,
/// construct SSA form, allowing us to eliminate LI.  This returns the value
/// that should be used at LI's definition site.
static Value *ConstructSSAForLoadSet(LoadInst *LI,
                         SmallVectorImpl<AvailableValueInBlock> &ValuesPerBlock,
                                     PgoPre &gvn) {
  // Check for the fully redundant, dominating load case.  In this case, we can
  // just use the dominating value directly.
  if (ValuesPerBlock.size() == 1 &&
      gvn.getDominatorTree().properlyDominates(ValuesPerBlock[0].BB,
                                               LI->getParent()))
    return ValuesPerBlock[0].MaterializeAdjustedValue(LI->getType(), gvn);

  // Otherwise, we have to construct SSA form.
  SmallVector<PHINode*, 8> NewPHIs;
  SSAUpdater SSAUpdate(&NewPHIs);
  SSAUpdate.Initialize(LI->getType(), LI->getName());

  Type *LoadTy = LI->getType();

  for (unsigned i = 0, e = ValuesPerBlock.size(); i != e; ++i) {
    const AvailableValueInBlock &AV = ValuesPerBlock[i];
    BasicBlock *BB = AV.BB;

    if (SSAUpdate.HasValueForBlock(BB))
      continue;

    SSAUpdate.AddAvailableValue(BB, AV.MaterializeAdjustedValue(LoadTy, gvn));
  }

  // Perform PHI construction.
  Value *V = SSAUpdate.GetValueInMiddleOfBlock(LI->getParent());

  // If new PHI nodes were created, notify alias analysis.
  if (V->getType()->getScalarType()->isPointerTy()) {
    AliasAnalysis *AA = gvn.getAliasAnalysis();

    for (unsigned i = 0, e = NewPHIs.size(); i != e; ++i)
      AA->copyValue(LI, NewPHIs[i]);

    // Now that we've copied information to the new PHIs, scan through
    // them again and inform alias analysis that we've added potentially
    // escaping uses to any values that are operands to these PHIs.
    for (unsigned i = 0, e = NewPHIs.size(); i != e; ++i) {
      PHINode *P = NewPHIs[i];
      for (unsigned ii = 0, ee = P->getNumIncomingValues(); ii != ee; ++ii) {
        unsigned jj = PHINode::getOperandNumForIncomingValue(ii);
        AA->addEscapingUse(P->getOperandUse(jj));
      }
    }
  }

  return V;
}

Value *AvailableValueInBlock::MaterializeAdjustedValue(Type *LoadTy, PgoPre &gvn) const {
  Value *Res;
  if (isSimpleValue()) {
    Res = getSimpleValue();
    if (Res->getType() != LoadTy) {
      const DataLayout *TD = gvn.getDataLayout();
      assert(TD && "Need target data to handle type mismatch case");
      Res = GetStoreValueForLoad(Res, Offset, LoadTy, BB->getTerminator(),
                                 *TD);
  
      DEBUG(dbgs() << "PgoPre COERCED NONLOCAL VAL:\nOffset: " << Offset << "  "
                   << *getSimpleValue() << '\n'
                   << *Res << '\n' << "\n\n\n");
    }
  } else if (isCoercedLoadValue()) {
    LoadInst *Load = getCoercedLoadValue();
    if (Load->getType() == LoadTy && Offset == 0) {
      Res = Load;
    } else {
      Res = GetLoadValueForLoad(Load, Offset, LoadTy, BB->getTerminator(),
                                gvn);
  
      DEBUG(dbgs() << "PgoPre COERCED NONLOCAL LOAD:\nOffset: " << Offset << "  "
                   << *getCoercedLoadValue() << '\n'
                   << *Res << '\n' << "\n\n\n");
    }
  } else {
    const DataLayout *TD = gvn.getDataLayout();
    assert(TD && "Need target data to handle type mismatch case");
    Res = GetMemInstValueForLoad(getMemIntrinValue(), Offset,
                                 LoadTy, BB->getTerminator(), *TD);
    DEBUG(dbgs() << "PgoPre COERCED NONLOCAL MEM INTRIN:\nOffset: " << Offset
                 << "  " << *getMemIntrinValue() << '\n'
                 << *Res << '\n' << "\n\n\n");
  }
  return Res;
}

static bool isLifetimeStart(const Instruction *Inst) {
  if (const IntrinsicInst* II = dyn_cast<IntrinsicInst>(Inst))
    return II->getIntrinsicID() == Intrinsic::lifetime_start;
  return false;
}

void PgoPre::AnalyzeLoadAvailability(LoadInst *LI, LoadDepVect &Deps, 
                                  AvailValInBlkVect &ValuesPerBlock,
                                  UnavailBlkVect &UnavailableBlocks) {

  // Filter out useless results (non-locals, etc).  Keep track of the blocks
  // where we have a value available in repl, also keep track of whether we see
  // dependencies that produce an unknown value for the load (such as a call
  // that could potentially clobber the load).
  unsigned NumDeps = Deps.size();
  for (unsigned i = 0, e = NumDeps; i != e; ++i) {
    BasicBlock *DepBB = Deps[i].getBB();
    MemDepResult DepInfo = Deps[i].getResult();

    if (!DepInfo.isDef() && !DepInfo.isClobber()) {
      UnavailableBlocks.push_back(DepBB);
      continue;
    }

    if (DepInfo.isClobber()) {
      // The address being loaded in this non-local block may not be the same as
      // the pointer operand of the load if PHI translation occurs.  Make sure
      // to consider the right address.
      Value *Address = Deps[i].getAddress();

      // If the dependence is to a store that writes to a superset of the bits
      // read by the load, we can extract the bits we need for the load from the
      // stored value.
      if (StoreInst *DepSI = dyn_cast<StoreInst>(DepInfo.getInst())) {
        if (TD && Address) {
          int Offset = AnalyzeLoadFromClobberingStore(LI->getType(), Address,
                                                      DepSI, *TD);
          if (Offset != -1) {
            ValuesPerBlock.push_back(AvailableValueInBlock::get(DepBB,
                                                       DepSI->getValueOperand(),
                                                                Offset));
            continue;
          }
        }
      }

      // Check to see if we have something like this:
      //    load i32* P
      //    load i8* (P+1)
      // if we have this, replace the later with an extraction from the former.
      if (LoadInst *DepLI = dyn_cast<LoadInst>(DepInfo.getInst())) {
        // If this is a clobber and L is the first instruction in its block, then
        // we have the first instruction in the entry block.
        if (DepLI != LI && Address && TD) {
          int Offset = AnalyzeLoadFromClobberingLoad(LI->getType(),
                                                     LI->getPointerOperand(),
                                                     DepLI, *TD);

          if (Offset != -1) {
            ValuesPerBlock.push_back(AvailableValueInBlock::getLoad(DepBB,DepLI,
                                                                    Offset));
            continue;
          }
        }
      }

      // If the clobbering value is a memset/memcpy/memmove, see if we can
      // forward a value on from it.
      if (MemIntrinsic *DepMI = dyn_cast<MemIntrinsic>(DepInfo.getInst())) {
        if (TD && Address) {
          int Offset = AnalyzeLoadFromClobberingMemInst(LI->getType(), Address,
                                                        DepMI, *TD);
          if (Offset != -1) {
            ValuesPerBlock.push_back(AvailableValueInBlock::getMI(DepBB, DepMI,
                                                                  Offset));
            continue;
          }
        }
      }

      UnavailableBlocks.push_back(DepBB);
      continue;
    }

    // DepInfo.isDef() here

    Instruction *DepInst = DepInfo.getInst();

    // Loading the allocation -> undef.
    if (isa<AllocaInst>(DepInst) || isMallocLikeFn(DepInst, TLI) ||
        // Loading immediately after lifetime begin -> undef.
        isLifetimeStart(DepInst)) {
      ValuesPerBlock.push_back(AvailableValueInBlock::get(DepBB,
                                             UndefValue::get(LI->getType())));
      continue;
    }

    if (StoreInst *S = dyn_cast<StoreInst>(DepInst)) {
      // Reject loads and stores that are to the same address but are of
      // different types if we have to.
      if (S->getValueOperand()->getType() != LI->getType()) {
        // If the stored value is larger or equal to the loaded value, we can
        // reuse it.
        if (TD == 0 || !CanCoerceMustAliasedValueToLoad(S->getValueOperand(),
                                                        LI->getType(), *TD)) {
          UnavailableBlocks.push_back(DepBB);
          continue;
        }
      }

      ValuesPerBlock.push_back(AvailableValueInBlock::get(DepBB,
                                                         S->getValueOperand()));
      continue;
    }

    if (LoadInst *LD = dyn_cast<LoadInst>(DepInst)) {
      // If the types mismatch and we can't handle it, reject reuse of the load.
      if (LD->getType() != LI->getType()) {
        // If the stored value is larger or equal to the loaded value, we can
        // reuse it.
        if (TD == 0 || !CanCoerceMustAliasedValueToLoad(LD, LI->getType(),*TD)){
          UnavailableBlocks.push_back(DepBB);
          continue;
        }
      }
      ValuesPerBlock.push_back(AvailableValueInBlock::getLoad(DepBB, LD));
      continue;
    }

    UnavailableBlocks.push_back(DepBB);
  }
}

bool PgoPre::PerformLoadPgoPre(LoadInst *LI, AvailValInBlkVect &ValuesPerBlock, 
                         UnavailBlkVect &UnavailableBlocks) {
  // Okay, we have *some* definitions of the value.  This means that the value
  // is available in some of our (transitive) predecessors.  Lets think about
  // doing PgoPre of this load.  This will involve inserting a new load into the
  // predecessor when it's not available.  We could do this in general, but
  // prefer to not increase code size.  As such, we only do this when we know
  // that we only have to insert *one* load (which means we're basically moving
  // the load, not inserting a new one).

  SmallPtrSet<BasicBlock *, 4> Blockers;
  for (unsigned i = 0, e = UnavailableBlocks.size(); i != e; ++i)
    Blockers.insert(UnavailableBlocks[i]);

  // Let's find the first basic block with more than one predecessor.  Walk
  // backwards through predecessors if needed.
  BasicBlock *LoadBB = LI->getParent();
  BasicBlock *TmpBB = LoadBB;

  while (TmpBB->getSinglePredecessor()) {
    TmpBB = TmpBB->getSinglePredecessor();
    if (TmpBB == LoadBB) // Infinite (unreachable) loop.
      return false;
    if (Blockers.count(TmpBB))
      return false;

    // If any of these blocks has more than one successor (i.e. if the edge we
    // just traversed was critical), then there are other paths through this
    // block along which the load may not be anticipated.  Hoisting the load
    // above this block would be adding the load to execution paths along
    // which it was not previously executed.
    if (TmpBB->getTerminator()->getNumSuccessors() != 1)
      return false;
  }

  assert(TmpBB);
  LoadBB = TmpBB;

  // Check to see how many predecessors have the loaded value fully
  // available.
  DenseMap<BasicBlock*, Value*> PredLoads;
  DenseMap<BasicBlock*, char> FullyAvailableBlocks;
  for (unsigned i = 0, e = ValuesPerBlock.size(); i != e; ++i)
    FullyAvailableBlocks[ValuesPerBlock[i].BB] = true;
  for (unsigned i = 0, e = UnavailableBlocks.size(); i != e; ++i)
    FullyAvailableBlocks[UnavailableBlocks[i]] = false;

  SmallVector<std::pair<TerminatorInst*, unsigned>, 4> NeedToSplit;
  for (pred_iterator PI = pred_begin(LoadBB), E = pred_end(LoadBB);
       PI != E; ++PI) {
    BasicBlock *Pred = *PI;
    if (IsValueFullyAvailableInBlock(Pred, FullyAvailableBlocks, 0)) {
      continue;
    }
    PredLoads[Pred] = 0;

    if (Pred->getTerminator()->getNumSuccessors() != 1) {
      if (isa<IndirectBrInst>(Pred->getTerminator())) {
        DEBUG(dbgs() << "COULD NOT PgoPre LOAD BECAUSE OF INDBR CRITICAL EDGE '"
              << Pred->getName() << "': " << *LI << '\n');
        return false;
      }

      if (LoadBB->isLandingPad()) {
        DEBUG(dbgs()
              << "COULD NOT PgoPre LOAD BECAUSE OF LANDING PAD CRITICAL EDGE '"
              << Pred->getName() << "': " << *LI << '\n');
        return false;
      }

      unsigned SuccNum = GetSuccessorNumber(Pred, LoadBB);
      NeedToSplit.push_back(std::make_pair(Pred->getTerminator(), SuccNum));
    }
  }

  if (!NeedToSplit.empty()) {
    toSplit.append(NeedToSplit.begin(), NeedToSplit.end());
    return false;
  }

  // Decide whether PgoPre is profitable for this load.
  unsigned NumUnavailablePreds = PredLoads.size();
  assert(NumUnavailablePreds != 0 &&
         "Fully available value should be eliminated above!");

  // If this load is unavailable in multiple predecessors, reject it.
  // FIXME: If we could restructure the CFG, we could make a common pred with
  // all the preds that don't have an available LI and insert a new load into
  // that one block.
  if (NumUnavailablePreds != 1)
      return false;

  // Check if the load can safely be moved to all the unavailable predecessors.
  bool CanDoPgoPre = true;
  SmallVector<Instruction*, 8> NewInsts;
  for (DenseMap<BasicBlock*, Value*>::iterator I = PredLoads.begin(),
         E = PredLoads.end(); I != E; ++I) {
    BasicBlock *UnavailablePred = I->first;

    // Do PHI translation to get its value in the predecessor if necessary.  The
    // returned pointer (if non-null) is guaranteed to dominate UnavailablePred.

    // If all preds have a single successor, then we know it is safe to insert
    // the load on the pred (?!?), so we can insert code to materialize the
    // pointer if it is not available.
    PHITransAddr Address(LI->getPointerOperand(), TD);
    Value *LoadPtr = 0;
    LoadPtr = Address.PHITranslateWithInsertion(LoadBB, UnavailablePred,
                                                *DT, NewInsts);

    // If we couldn't find or insert a computation of this phi translated value,
    // we fail PgoPre.
    if (LoadPtr == 0) {
      DEBUG(dbgs() << "COULDN'T INSERT PHI TRANSLATED VALUE OF: "
            << *LI->getPointerOperand() << "\n");
      CanDoPgoPre = false;
      break;
    }

    I->second = LoadPtr;
  }

  if (!CanDoPgoPre) {
    while (!NewInsts.empty()) {
      Instruction *I = NewInsts.pop_back_val();
      if (MD) MD->removeInstruction(I);
      I->eraseFromParent();
    }
    return false;
  }

  // Okay, we can eliminate this load by inserting a reload in the predecessor
  // and using PHI construction to get the value in the other predecessors, do
  // it.
  DEBUG(dbgs() << "PgoPre REMOVING PgoPre LOAD: " << *LI << '\n');
  DEBUG(if (!NewInsts.empty())
          dbgs() << "INSERTED " << NewInsts.size() << " INSTS: "
                 << *NewInsts.back() << '\n');

  // Assign value numbers to the new instructions.
  for (unsigned i = 0, e = NewInsts.size(); i != e; ++i) {
    // FIXME: We really _ought_ to insert these value numbers into their
    // parent's availability map.  However, in doing so, we risk getting into
    // ordering issues.  If a block hasn't been processed yet, we would be
    // marking a value as AVAIL-IN, which isn't what we intend.
    VN.lookup_or_add(NewInsts[i]);
  }

  for (DenseMap<BasicBlock*, Value*>::iterator I = PredLoads.begin(),
         E = PredLoads.end(); I != E; ++I) {
    BasicBlock *UnavailablePred = I->first;
    Value *LoadPtr = I->second;

    Instruction *NewLoad = new LoadInst(LoadPtr, LI->getName()+".pre", false,
                                        LI->getAlignment(),
                                        UnavailablePred->getTerminator());

    // Transfer the old load's TBAA tag to the new load.
    if (MDNode *Tag = LI->getMetadata(LLVMContext::MD_tbaa))
      NewLoad->setMetadata(LLVMContext::MD_tbaa, Tag);

    // Transfer DebugLoc.
    NewLoad->setDebugLoc(LI->getDebugLoc());

    // Add the newly created load.
    ValuesPerBlock.push_back(AvailableValueInBlock::get(UnavailablePred,
                                                        NewLoad));
    MD->invalidateCachedPointerInfo(LoadPtr);
    DEBUG(dbgs() << "PgoPre INSERTED " << *NewLoad << '\n');
  }

  // Perform PHI construction.
  Value *V = ConstructSSAForLoadSet(LI, ValuesPerBlock, *this);
  LI->replaceAllUsesWith(V);
  if (isa<PHINode>(V))
    V->takeName(LI);
  if (V->getType()->getScalarType()->isPointerTy())
    MD->invalidateCachedPointerInfo(V);
  markInstructionForDeletion(LI);
  ++NumPgoPreLoad;
  return true;
}

/// processNonLocalLoad - Attempt to eliminate a load whose dependencies are
/// non-local by performing PHI construction.
bool PgoPre::processNonLocalLoad(LoadInst *LI) {
  // Step 1: Find the non-local dependencies of the load.
  LoadDepVect Deps;
  AliasAnalysis::Location Loc = VN.getAliasAnalysis()->getLocation(LI);
  MD->getNonLocalPointerDependency(Loc, true, LI->getParent(), Deps);

  // If we had to process more than one hundred blocks to find the
  // dependencies, this load isn't worth worrying about.  Optimizing
  // it will be too expensive.
  unsigned NumDeps = Deps.size();
  if (NumDeps > 100)
    return false;

  // If we had a phi translation failure, we'll have a single entry which is a
  // clobber in the current block.  Reject this early.
  if (NumDeps == 1 &&
      !Deps[0].getResult().isDef() && !Deps[0].getResult().isClobber()) {
    DEBUG(
      dbgs() << "PgoPre: non-local load ";
      WriteAsOperand(dbgs(), LI);
      dbgs() << " has unknown dependencies\n";
    );
    return false;
  }

  // Step 2: Analyze the availability of the load
  AvailValInBlkVect ValuesPerBlock;
  UnavailBlkVect UnavailableBlocks;
  AnalyzeLoadAvailability(LI, Deps, ValuesPerBlock, UnavailableBlocks);

  // If we have no predecessors that produce a known value for this load, exit
  // early.
  if (ValuesPerBlock.empty())
    return false;

  // Step 3: Eliminate fully redundancy.
  //
  // If all of the instructions we depend on produce a known value for this
  // load, then it is fully redundant and we can use PHI insertion to compute
  // its value.  Insert PHIs and remove the fully redundant value now.
  if (UnavailableBlocks.empty()) {
    DEBUG(dbgs() << "PgoPre REMOVING NONLOCAL LOAD: " << *LI << '\n');

    // Perform PHI construction.
    Value *V = ConstructSSAForLoadSet(LI, ValuesPerBlock, *this);
    LI->replaceAllUsesWith(V);

    if (isa<PHINode>(V))
      V->takeName(LI);
    if (V->getType()->getScalarType()->isPointerTy())
      MD->invalidateCachedPointerInfo(V);
    markInstructionForDeletion(LI);
    ++NumPgoPreLoad;
    return true;
  }

  // Step 4: Eliminate partial redundancy.
  if (!EnablePgoPre || !EnableLoadPgoPre)
    return false;

  return PerformLoadPgoPre(LI, ValuesPerBlock, UnavailableBlocks);
}


static void patchReplacementInstruction(Instruction *I, Value *Repl) {
  // Patch the replacement so that it is not more restrictive than the value
  // being replaced.
  BinaryOperator *Op = dyn_cast<BinaryOperator>(I);
  BinaryOperator *ReplOp = dyn_cast<BinaryOperator>(Repl);
  if (Op && ReplOp && isa<OverflowingBinaryOperator>(Op) &&
      isa<OverflowingBinaryOperator>(ReplOp)) {
    if (ReplOp->hasNoSignedWrap() && !Op->hasNoSignedWrap())
      ReplOp->setHasNoSignedWrap(false);
    if (ReplOp->hasNoUnsignedWrap() && !Op->hasNoUnsignedWrap())
      ReplOp->setHasNoUnsignedWrap(false);
  }
  if (Instruction *ReplInst = dyn_cast<Instruction>(Repl)) {
    SmallVector<std::pair<unsigned, MDNode*>, 4> Metadata;
    ReplInst->getAllMetadataOtherThanDebugLoc(Metadata);
    for (int i = 0, n = Metadata.size(); i < n; ++i) {
      unsigned Kind = Metadata[i].first;
      MDNode *IMD = I->getMetadata(Kind);
      MDNode *ReplMD = Metadata[i].second;
      switch(Kind) {
      default:
        ReplInst->setMetadata(Kind, NULL); // Remove unknown metadata
        break;
      case LLVMContext::MD_dbg:
        llvm_unreachable("getAllMetadataOtherThanDebugLoc returned a MD_dbg");
      case LLVMContext::MD_tbaa:
        ReplInst->setMetadata(Kind, MDNode::getMostGenericTBAA(IMD, ReplMD));
        break;
      case LLVMContext::MD_range:
        ReplInst->setMetadata(Kind, MDNode::getMostGenericRange(IMD, ReplMD));
        break;
      case LLVMContext::MD_prof:
        llvm_unreachable("MD_prof in a non terminator instruction");
        break;
      case LLVMContext::MD_fpmath:
        ReplInst->setMetadata(Kind, MDNode::getMostGenericFPMath(IMD, ReplMD));
        break;
      }
    }
  }
}

static void patchAndReplaceAllUsesWith(Instruction *I, Value *Repl) {
  patchReplacementInstruction(I, Repl);
  I->replaceAllUsesWith(Repl);
}

/// processLoad - Attempt to eliminate a load, first by eliminating it
/// locally, and then attempting non-local elimination if that fails.
bool PgoPre::processLoad(LoadInst *L) {
  if (!MD)
    return false;

  if (!L->isSimple())
    return false;

  if (L->use_empty()) {
    markInstructionForDeletion(L);
    return true;
  }

  // ... to a pointer that has been loaded from before...
  MemDepResult Dep = MD->getDependency(L);

  // If we have a clobber and target data is around, see if this is a clobber
  // that we can fix up through code synthesis.
  if (Dep.isClobber() && TD) {
    // Check to see if we have something like this:
    //   store i32 123, i32* %P
    //   %A = bitcast i32* %P to i8*
    //   %B = gep i8* %A, i32 1
    //   %C = load i8* %B
    //
    // We could do that by recognizing if the clobber instructions are obviously
    // a common base + constant offset, and if the previous store (or memset)
    // completely covers this load.  This sort of thing can happen in bitfield
    // access code.
    Value *AvailVal = 0;
    if (StoreInst *DepSI = dyn_cast<StoreInst>(Dep.getInst())) {
      int Offset = AnalyzeLoadFromClobberingStore(L->getType(),
                                                  L->getPointerOperand(),
                                                  DepSI, *TD);
      if (Offset != -1)
        AvailVal = GetStoreValueForLoad(DepSI->getValueOperand(), Offset,
                                        L->getType(), L, *TD);
    }

    // Check to see if we have something like this:
    //    load i32* P
    //    load i8* (P+1)
    // if we have this, replace the later with an extraction from the former.
    if (LoadInst *DepLI = dyn_cast<LoadInst>(Dep.getInst())) {
      // If this is a clobber and L is the first instruction in its block, then
      // we have the first instruction in the entry block.
      if (DepLI == L)
        return false;

      int Offset = AnalyzeLoadFromClobberingLoad(L->getType(),
                                                 L->getPointerOperand(),
                                                 DepLI, *TD);
      if (Offset != -1)
        AvailVal = GetLoadValueForLoad(DepLI, Offset, L->getType(), L, *this);
    }

    // If the clobbering value is a memset/memcpy/memmove, see if we can forward
    // a value on from it.
    if (MemIntrinsic *DepMI = dyn_cast<MemIntrinsic>(Dep.getInst())) {
      int Offset = AnalyzeLoadFromClobberingMemInst(L->getType(),
                                                    L->getPointerOperand(),
                                                    DepMI, *TD);
      if (Offset != -1)
        AvailVal = GetMemInstValueForLoad(DepMI, Offset, L->getType(), L, *TD);
    }

    if (AvailVal) {
      DEBUG(dbgs() << "PgoPre COERCED INST:\n" << *Dep.getInst() << '\n'
            << *AvailVal << '\n' << *L << "\n\n\n");

      // Replace the load!
      L->replaceAllUsesWith(AvailVal);
      if (AvailVal->getType()->getScalarType()->isPointerTy())
        MD->invalidateCachedPointerInfo(AvailVal);
      markInstructionForDeletion(L);
      ++NumPgoPreLoad;
      return true;
    }
  }

  // If the value isn't available, don't do anything!
  if (Dep.isClobber()) {
    DEBUG(
      // fast print dep, using operator<< on instruction is too slow.
      dbgs() << "PgoPre: load ";
      WriteAsOperand(dbgs(), L);
      Instruction *I = Dep.getInst();
      dbgs() << " is clobbered by " << *I << '\n';
    );
    return false;
  }

  // If it is defined in another block, try harder.
  if (Dep.isNonLocal())
    return processNonLocalLoad(L);

  if (!Dep.isDef()) {
    DEBUG(
      // fast print dep, using operator<< on instruction is too slow.
      dbgs() << "PgoPre: load ";
      WriteAsOperand(dbgs(), L);
      dbgs() << " has unknown dependence\n";
    );
    return false;
  }

  Instruction *DepInst = Dep.getInst();
  if (StoreInst *DepSI = dyn_cast<StoreInst>(DepInst)) {
    Value *StoredVal = DepSI->getValueOperand();

    // The store and load are to a must-aliased pointer, but they may not
    // actually have the same type.  See if we know how to reuse the stored
    // value (depending on its type).
    if (StoredVal->getType() != L->getType()) {
      if (TD) {
        StoredVal = CoerceAvailableValueToLoadType(StoredVal, L->getType(),
                                                   L, *TD);
        if (StoredVal == 0)
          return false;

        DEBUG(dbgs() << "PgoPre COERCED STORE:\n" << *DepSI << '\n' << *StoredVal
                     << '\n' << *L << "\n\n\n");
      }
      else
        return false;
    }

    // Remove it!
    L->replaceAllUsesWith(StoredVal);
    if (StoredVal->getType()->getScalarType()->isPointerTy())
      MD->invalidateCachedPointerInfo(StoredVal);
    markInstructionForDeletion(L);
    ++NumPgoPreLoad;
    return true;
  }

  if (LoadInst *DepLI = dyn_cast<LoadInst>(DepInst)) {
    Value *AvailableVal = DepLI;

    // The loads are of a must-aliased pointer, but they may not actually have
    // the same type.  See if we know how to reuse the previously loaded value
    // (depending on its type).
    if (DepLI->getType() != L->getType()) {
      if (TD) {
        AvailableVal = CoerceAvailableValueToLoadType(DepLI, L->getType(),
                                                      L, *TD);
        if (AvailableVal == 0)
          return false;

        DEBUG(dbgs() << "PgoPre COERCED LOAD:\n" << *DepLI << "\n" << *AvailableVal
                     << "\n" << *L << "\n\n\n");
      }
      else
        return false;
    }

    // Remove it!
    patchAndReplaceAllUsesWith(L, AvailableVal);
    if (DepLI->getType()->getScalarType()->isPointerTy())
      MD->invalidateCachedPointerInfo(DepLI);
    markInstructionForDeletion(L);
    ++NumPgoPreLoad;
    return true;
  }

  // If this load really doesn't depend on anything, then we must be loading an
  // undef value.  This can happen when loading for a fresh allocation with no
  // intervening stores, for example.
  if (isa<AllocaInst>(DepInst) || isMallocLikeFn(DepInst, TLI)) {
    L->replaceAllUsesWith(UndefValue::get(L->getType()));
    markInstructionForDeletion(L);
    ++NumPgoPreLoad;
    return true;
  }

  // If this load occurs either right after a lifetime begin,
  // then the loaded value is undefined.
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(DepInst)) {
    if (II->getIntrinsicID() == Intrinsic::lifetime_start) {
      L->replaceAllUsesWith(UndefValue::get(L->getType()));
      markInstructionForDeletion(L);
      ++NumPgoPreLoad;
      return true;
    }
  }

  return false;
}

// findLeader - In order to find a leader for a given value number at a
// specific basic block, we first obtain the list of all Values for that number,
// and then scan the list to find one whose block dominates the block in
// question.  This is fast because dominator tree queries consist of only
// a few comparisons of DFS numbers.
Value *PgoPre::findLeader(const BasicBlock *BB, uint32_t num) {
  LeaderTableEntry Vals = LeaderTable[num];
  if (!Vals.Val) return 0;

  Value *Val = 0;
  if (DT->dominates(Vals.BB, BB)) {
    Val = Vals.Val;
    if (isa<Constant>(Val)) return Val;
  }

  LeaderTableEntry* Next = Vals.Next;
  while (Next) {
    if (DT->dominates(Next->BB, BB)) {
      if (isa<Constant>(Next->Val)) return Next->Val;
      if (!Val) Val = Next->Val;
    }

    Next = Next->Next;
  }

  return Val;
}

/// replaceAllDominatedUsesWith - Replace all uses of 'From' with 'To' if the
/// use is dominated by the given basic block.  Returns the number of uses that
/// were replaced.
unsigned PgoPre::replaceAllDominatedUsesWith(Value *From, Value *To,
                                          const BasicBlockEdge &Root) {
  unsigned Count = 0;
  for (Value::use_iterator UI = From->use_begin(), UE = From->use_end();
       UI != UE; ) {
    Use &U = (UI++).getUse();

    if (DT->dominates(Root, U)) {
      U.set(To);
      ++Count;
    }
  }
  return Count;
}

/// isOnlyReachableViaThisEdge - There is an edge from 'Src' to 'Dst'.  Return
/// true if every path from the entry block to 'Dst' passes via this edge.  In
/// particular 'Dst' must not be reachable via another edge from 'Src'.
static bool isOnlyReachableViaThisEdge(const BasicBlockEdge &E,
                                       DominatorTree *DT) {
  // While in theory it is interesting to consider the case in which Dst has
  // more than one predecessor, because Dst might be part of a loop which is
  // only reachable from Src, in practice it is pointless since at the time
  // PgoPre runs all such loops have preheaders, which means that Dst will have
  // been changed to have only one predecessor, namely Src.
  const BasicBlock *Pred = E.getEnd()->getSinglePredecessor();
  const BasicBlock *Src = E.getStart();
  assert((!Pred || Pred == Src) && "No edge between these basic blocks!");
  (void)Src;
  return Pred != 0;
}

/// propagateEquality - The given values are known to be equal in every block
/// dominated by 'Root'.  Exploit this, for example by replacing 'LHS' with
/// 'RHS' everywhere in the scope.  Returns whether a change was made.
bool PgoPre::propagateEquality(Value *LHS, Value *RHS,
                            const BasicBlockEdge &Root) {
  SmallVector<std::pair<Value*, Value*>, 4> Worklist;
  Worklist.push_back(std::make_pair(LHS, RHS));
  bool Changed = false;
  // For speed, compute a conservative fast approximation to
  // DT->dominates(Root, Root.getEnd());
  bool RootDominatesEnd = isOnlyReachableViaThisEdge(Root, DT);

  while (!Worklist.empty()) {
    std::pair<Value*, Value*> Item = Worklist.pop_back_val();
    LHS = Item.first; RHS = Item.second;

    if (LHS == RHS) continue;
    assert(LHS->getType() == RHS->getType() && "Equality but unequal types!");

    // Don't try to propagate equalities between constants.
    if (isa<Constant>(LHS) && isa<Constant>(RHS)) continue;

    // Prefer a constant on the right-hand side, or an Argument if no constants.
    if (isa<Constant>(LHS) || (isa<Argument>(LHS) && !isa<Constant>(RHS)))
      std::swap(LHS, RHS);
    assert((isa<Argument>(LHS) || isa<Instruction>(LHS)) && "Unexpected value!");

    // If there is no obvious reason to prefer the left-hand side over the right-
    // hand side, ensure the longest lived term is on the right-hand side, so the
    // shortest lived term will be replaced by the longest lived.  This tends to
    // expose more simplifications.
    uint32_t LVN = VN.lookup_or_add(LHS);
    if ((isa<Argument>(LHS) && isa<Argument>(RHS)) ||
        (isa<Instruction>(LHS) && isa<Instruction>(RHS))) {
      // Move the 'oldest' value to the right-hand side, using the value number as
      // a proxy for age.
      uint32_t RVN = VN.lookup_or_add(RHS);
      if (LVN < RVN) {
        std::swap(LHS, RHS);
        LVN = RVN;
      }
    }

    // If value numbering later sees that an instruction in the scope is equal
    // to 'LHS' then ensure it will be turned into 'RHS'.  In order to preserve
    // the invariant that instructions only occur in the leader table for their
    // own value number (this is used by removeFromLeaderTable), do not do this
    // if RHS is an instruction (if an instruction in the scope is morphed into
    // LHS then it will be turned into RHS by the next PgoPre iteration anyway, so
    // using the leader table is about compiling faster, not optimizing better).
    // The leader table only tracks basic blocks, not edges. Only add to if we
    // have the simple case where the edge dominates the end.
    if (RootDominatesEnd && !isa<Instruction>(RHS))
      addToLeaderTable(LVN, RHS, Root.getEnd());

    // Replace all occurrences of 'LHS' with 'RHS' everywhere in the scope.  As
    // LHS always has at least one use that is not dominated by Root, this will
    // never do anything if LHS has only one use.
    if (!LHS->hasOneUse()) {
      unsigned NumReplacements = replaceAllDominatedUsesWith(LHS, RHS, Root);
      Changed |= NumReplacements > 0;
      NumPgoPreEqProp += NumReplacements;
    }

    // Now try to deduce additional equalities from this one.  For example, if the
    // known equality was "(A != B)" == "false" then it follows that A and B are
    // equal in the scope.  Only boolean equalities with an explicit true or false
    // RHS are currently supported.
    if (!RHS->getType()->isIntegerTy(1))
      // Not a boolean equality - bail out.
      continue;
    ConstantInt *CI = dyn_cast<ConstantInt>(RHS);
    if (!CI)
      // RHS neither 'true' nor 'false' - bail out.
      continue;
    // Whether RHS equals 'true'.  Otherwise it equals 'false'.
    bool isKnownTrue = CI->isAllOnesValue();
    bool isKnownFalse = !isKnownTrue;

    // If "A && B" is known true then both A and B are known true.  If "A || B"
    // is known false then both A and B are known false.
    Value *A, *B;
    if ((isKnownTrue && match(LHS, m_And(m_Value(A), m_Value(B)))) ||
        (isKnownFalse && match(LHS, m_Or(m_Value(A), m_Value(B))))) {
      Worklist.push_back(std::make_pair(A, RHS));
      Worklist.push_back(std::make_pair(B, RHS));
      continue;
    }

    // If we are propagating an equality like "(A == B)" == "true" then also
    // propagate the equality A == B.  When propagating a comparison such as
    // "(A >= B)" == "true", replace all instances of "A < B" with "false".
    if (ICmpInst *Cmp = dyn_cast<ICmpInst>(LHS)) {
      Value *Op0 = Cmp->getOperand(0), *Op1 = Cmp->getOperand(1);

      // If "A == B" is known true, or "A != B" is known false, then replace
      // A with B everywhere in the scope.
      if ((isKnownTrue && Cmp->getPredicate() == CmpInst::ICMP_EQ) ||
          (isKnownFalse && Cmp->getPredicate() == CmpInst::ICMP_NE))
        Worklist.push_back(std::make_pair(Op0, Op1));

      // If "A >= B" is known true, replace "A < B" with false everywhere.
      CmpInst::Predicate NotPred = Cmp->getInversePredicate();
      Constant *NotVal = ConstantInt::get(Cmp->getType(), isKnownFalse);
      // Since we don't have the instruction "A < B" immediately to hand, work out
      // the value number that it would have and use that to find an appropriate
      // instruction (if any).
      uint32_t NextNum = VN.getNextUnusedValueNumber();
      uint32_t Num = VN.lookup_or_add_cmp(Cmp->getOpcode(), NotPred, Op0, Op1);
      // If the number we were assigned was brand new then there is no point in
      // looking for an instruction realizing it: there cannot be one!
      if (Num < NextNum) {
        Value *NotCmp = findLeader(Root.getEnd(), Num);
        if (NotCmp && isa<Instruction>(NotCmp)) {
          unsigned NumReplacements =
            replaceAllDominatedUsesWith(NotCmp, NotVal, Root);
          Changed |= NumReplacements > 0;
          NumPgoPreEqProp += NumReplacements;
        }
      }
      // Ensure that any instruction in scope that gets the "A < B" value number
      // is replaced with false.
      // The leader table only tracks basic blocks, not edges. Only add to if we
      // have the simple case where the edge dominates the end.
      if (RootDominatesEnd)
        addToLeaderTable(Num, NotVal, Root.getEnd());

      continue;
    }
  }

  return Changed;
}

/// processInstruction - When calculating availability, handle an instruction
/// by inserting it into the appropriate sets
bool PgoPre::processInstruction(Instruction *I) {
  // Ignore dbg info intrinsics.
  if (isa<DbgInfoIntrinsic>(I))
    return false;

  // If the instruction can be easily simplified then do so now in preference
  // to value numbering it.  Value numbering often exposes redundancies, for
  // example if it determines that %y is equal to %x then the instruction
  // "%z = and i32 %x, %y" becomes "%z = and i32 %x, %x" which we now simplify.
  if (Value *V = SimplifyInstruction(I, TD, TLI, DT)) {
    I->replaceAllUsesWith(V);
    if (MD && V->getType()->getScalarType()->isPointerTy())
      MD->invalidateCachedPointerInfo(V);
    markInstructionForDeletion(I);
    ++NumPgoPreSimpl;
    return true;
  }

  if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
    if (processLoad(LI))
      return true;

    unsigned Num = VN.lookup_or_add(LI);
    addToLeaderTable(Num, LI, LI->getParent());
    return false;
  }

  // For conditional branches, we can perform simple conditional propagation on
  // the condition value itself.
  if (BranchInst *BI = dyn_cast<BranchInst>(I)) {
    if (!BI->isConditional() || isa<Constant>(BI->getCondition()))
      return false;

    Value *BranchCond = BI->getCondition();

    BasicBlock *TrueSucc = BI->getSuccessor(0);
    BasicBlock *FalseSucc = BI->getSuccessor(1);
    // Avoid multiple edges early.
    if (TrueSucc == FalseSucc)
      return false;

    BasicBlock *Parent = BI->getParent();
    bool Changed = false;

    Value *TrueVal = ConstantInt::getTrue(TrueSucc->getContext());
    BasicBlockEdge TrueE(Parent, TrueSucc);
    Changed |= propagateEquality(BranchCond, TrueVal, TrueE);

    Value *FalseVal = ConstantInt::getFalse(FalseSucc->getContext());
    BasicBlockEdge FalseE(Parent, FalseSucc);
    Changed |= propagateEquality(BranchCond, FalseVal, FalseE);

    return Changed;
  }

  // For switches, propagate the case values into the case destinations.
  if (SwitchInst *SI = dyn_cast<SwitchInst>(I)) {
    Value *SwitchCond = SI->getCondition();
    BasicBlock *Parent = SI->getParent();
    bool Changed = false;

    // Remember how many outgoing edges there are to every successor.
    SmallDenseMap<BasicBlock *, unsigned, 16> SwitchEdges;
    for (unsigned i = 0, n = SI->getNumSuccessors(); i != n; ++i)
      ++SwitchEdges[SI->getSuccessor(i)];

    for (SwitchInst::CaseIt i = SI->case_begin(), e = SI->case_end();
         i != e; ++i) {
      BasicBlock *Dst = i.getCaseSuccessor();
      // If there is only a single edge, propagate the case value into it.
      if (SwitchEdges.lookup(Dst) == 1) {
        BasicBlockEdge E(Parent, Dst);
        Changed |= propagateEquality(SwitchCond, i.getCaseValue(), E);
      }
    }
    return Changed;
  }

  // Instructions with void type don't return a value, so there's
  // no point in trying to find redundancies in them.
  if (I->getType()->isVoidTy()) return false;

  uint32_t NextNum = VN.getNextUnusedValueNumber();
  unsigned Num = VN.lookup_or_add(I);

  // Allocations are always uniquely numbered, so we can save time and memory
  // by fast failing them.
  if (isa<AllocaInst>(I) || isa<TerminatorInst>(I) || isa<PHINode>(I)) {
    addToLeaderTable(Num, I, I->getParent());
    return false;
  }

  // If the number we were assigned was a brand new VN, then we don't
  // need to do a lookup to see if the number already exists
  // somewhere in the domtree: it can't!
  if (Num >= NextNum) {
    addToLeaderTable(Num, I, I->getParent());
    return false;
  }

  // Perform fast-path value-number based elimination of values inherited from
  // dominators.
  Value *repl = findLeader(I->getParent(), Num);
  if (repl == 0) {
    // Failure, just remember this instance for future use.
    addToLeaderTable(Num, I, I->getParent());
    return false;
  }

  // Remove it!
  patchAndReplaceAllUsesWith(I, repl);
  if (MD && repl->getType()->getScalarType()->isPointerTy())
    MD->invalidateCachedPointerInfo(repl);
  markInstructionForDeletion(I);
  return true;
}

/// runOnFunction - This is the main transformation entry point for a function.
bool PgoPre::runOnFunction(Function& F) {

  // Gather the profile information.
  PI = &getAnalysis<ProfileInfo>();

  cout << endl << "*** Running PgoPre on func " << endl;

  // Fetch start node to use in all calculations
  Function::iterator startItr = F.begin();
  const BasicBlock* startNode = &(*(F.begin()));

  // Build reverse post-order traversal of the graph and build the block mapping
  BlockMapping.reserve(F.size());
  ReversePostOrderTraversal<Function*> RPOT(&F);
  for (ReversePostOrderTraversal<Function*>::rpo_iterator I = RPOT.begin(),
         E = RPOT.end(); I != E; ++I) {
    BasicBlock *BB = *I;
    BlockNumbering.insert(std::make_pair(BB, BlockMapping.size()));
    BlockMapping.push_back(BB);
  }

  /* need to initialize the subpath collections with their frequencies here
  hash_map<const BasicBlock*, GraphPath*> AvailableSubPaths;
  hash_map<const BasicBlock*, GraphPath*> UnAvailableSubPaths;
  hash_map<const BasicBlock*, GraphPath*> AnticipableSubPaths;
  hash_map<const BasicBlock*, GraphPath*> UnAnticipableSubPaths;
  */

  // Build all paths through the graph using BFS
  // getSinglePredecessor().
  queue<const BasicBlock*> bfsQueue;
  bfsQueue.push(startNode);
  set<const BasicBlock*> visited;

  // Maintain tails of all paths constructed so far...
  GraphPath* startPath = new GraphPath();
  startPath->nodes.push_back(startNode);
  paths.push_back(startPath);

  // Now BFS walk
  while (bfsQueue.empty() == false)
  {
    const BasicBlock* curr = bfsQueue.front();
    bfsQueue.pop();
    visited.insert(curr);

    // Find a way to extend all known paths by this given path
    const BasicBlock* parent = curr->getSinglePredecessor();
    if (parent != NULL)
    {
      for (unsigned int i = 0; i < paths.size(); i++)
      {
        unsigned int numNodes = paths.at(i)->nodes.size();
        if (paths.at(i)->nodes.at(numNodes - 1) == parent)
        {
          // We found a way to EXTEND this path, so create an entirely 
          //  new graph path and add it to the set of tails.
          GraphPath* newPath = new GraphPath();
          newPath->fromStart = true;
          for (unsigned int j = 0; j < numNodes; j++) // save old paths
          {
            newPath->nodes.push_back(paths.at(i)->nodes.at(j));
          }
          for (unsigned int j = 0; j < numNodes - 1; j++) // #edges = (numNodes - 1) by properties of path
          {
            newPath->edges.push_back(paths.at(i)->edges.at(j));
            newPath->weights.push_back(paths.at(i)->weights.at(j));
          }

          cout << "New path edges   = " << newPath->edges.size() << endl;
          cout << "New path weights = " << newPath->weights.size() << endl;
          cout << "New path nodes   = " << numNodes << endl;

          // Fetch the weight of this new edge
          std::pair<const BasicBlock*, const BasicBlock*> e = PI->getEdge(parent, curr);
          double newWeight = PI->getEdgeWeight(e);

          // Build the new edge
          // typedef GraphEdge std::pair<const BasicBlock*, const BasicBlock*>;
          GraphEdge newEdge = GraphEdge(parent, curr);

          // Append the new path vertex, edge, and weight of the edge, resp
          newPath->nodes.push_back(curr);
          newPath->edges.push_back(newEdge);
          newPath->weights.push_back(newWeight);

          ///////////////////////////////////
          // Update the BlockFreqMap based on the in-degree to this block/node
          ///////////////////////////////////
          map<const BasicBlock*, int>::iterator findItr = BlockFreqMap.find(curr);
          if (findItr == BlockFreqMap.end())
          {
            BlockFreqMap[curr] = newWeight; // initialize to the in-degree
          }
          else
          {
            BlockFreqMap[curr] += newWeight; // else, increment the value that's already tgere 
          }

          // Finally, append this new path to our collection obtained so far...
          paths.push_back(newPath);
        }
      }
    } 

    // Push this block's successors into the queue as per BFS
    for (llvm::succ_const_iterator itr = succ_begin(curr); itr != succ_end(curr); itr++)
    {
      if (visited.find(*itr) == visited.end()) // not in the set of visited blocks
      {
        bfsQueue.push(*itr);
      }
      else
      {
        cout << "Skipping block (already visited) @" << *itr << endl;
      }
    }
  }

  // Need to now make subpaths from the set of paths above (needed for unanticipable sets)
  for (unsigned int pId = 0; pId < paths.size(); pId++)
  {
    GraphPath* path = paths.at(pId);
    for (unsigned int n = 1; n < path->nodes.size(); n++)
    {
      GraphPath* subPath = path->buildSubPath(n);
      paths.push_back(subPath);
    }
  }

  // Set the start node for the dominator set
  // DS = new DominatorSet();
  // DS->function = &F;
  // DS->start = &(inst_begin(&F)); // pull out first instruction in the function CFG
  // DS->buildDominatorSet(&F);

  // Initialize the instructionContainsMap for each instruction for each graph path
  // instructionContainsMap
  for (inst_iterator instItr = inst_begin(&F), E = inst_end(&F); instItr != E; ++instItr)
  {
    Instruction& inst = *instItr;
    Value* instValue = dyn_cast<Value>(&inst); // cast instruction to value
    for (unsigned int i = 0; i < paths.size(); i++)
    {
      // paths.at(i)->checkForInstruction(&inst);
      paths.at(i)->checkForValue(instValue);
    }
  }

  // Walk the instructions in the function to build up the available, unavailable, anticipable, unanticipable sets  
  startItr = F.begin();
  for (inst_iterator instItr = inst_begin(&F), E = inst_end(&F); instItr != E; ++instItr)
  {
    Instruction& inst = *instItr; // this is the expression (exp) used in all of the sets
    Value* instValue = dyn_cast<Value>(&inst); // cast instruction to value
    cout << "Calculating sets for value: " << instValue << endl;

    // Extract operands for this instruction
    vector<Value*> operands;
    vector<StringRef> operandNames;
    for (unsigned int opi = 0; opi < inst.getNumOperands(); opi++)
    {
      operands.push_back(inst.getOperand(opi));
      operandNames.push_back(inst.getOperand(opi)->getName());
    }

    // 1. Build AvailableSubPaths for all blocks that are not the start, using BFS of basic blocks to build paths
    // 2. Build UnavailableSubPaths at the same time...
    for (unsigned int pId = 0; pId < paths.size(); pId++) // check every path...
    {
      bool killed = false;
      if (paths.at(pId)->fromStart == true && paths.at(pId)->containsValue(instValue)) // only check this path if it contains the instruction somewhere
      {
        int bId = paths.at(pId)->valueBlockMap[instValue]; // the block ID in this path that contains the instruction

        for (int prevBlockId = 0; prevBlockId < bId; prevBlockId++) // check each instruction UP to the previous block to see if the expression is contained...
        {
          // check each instruction in this block
          BasicBlock* blk = const_cast<BasicBlock*>(paths.at(pId)->nodes.at(bId));
          for (BasicBlock::iterator prevBlockInstItr = blk->begin(), prevBlockInstItrEnd = blk->end(); prevBlockInstItr != prevBlockInstItrEnd; ++prevBlockInstItr)
          {
            Value* instValue = dyn_cast<Value>(&(*prevBlockInstItr)); // cast instruction to value
            for (unsigned int vId = 0; vId < operands.size(); vId++)
            {
              if (instValue == operands.at(vId)) // match in the operand list (one of the operands was re-evaluated by a previous instruction)
              {
                killed = true;
              }
            }
          }
        }

        // Now check the instructions current block 
        // TODO: is it even necessary to even check the current block?
        // for (BasicBlock::iterator prevBlockInstItr = blk->begin(), prevBlockInstItrEnd = blk->end(); prevBlockInstItr != prevBlockInstItrEnd; ++prevBlockInstItr)
        // {
        // }

        // was not killed along some path from start to this node n
        if (killed == false)
        {
          ExpressionNodePair enpair = std::make_pair(instValue, paths.at(pId)->nodes.at(bId));
          AvailableSubPaths[enpair].push_back(paths.at(pId)); // save this (exp, node/block, path) pair in the available subpaths
        }
        else // it was killed, so it belongs in the unavailable subpaths thing
        {
          ExpressionNodePair enpair = std::make_pair(instValue, paths.at(pId)->nodes.at(bId));
          UnAvailableSubPaths[enpair].push_back(paths.at(pId)); // save this (exp, node/block, path) pair in the available subpaths
        }
      } 
      // else // this instruction isn't even available on this path... so it belongs in the unavailable subpaths group
      // {
      //   ExpressionNodePair enpair = std::make_pair(instValue, NULL);
      //   UnAvailableSubPaths[enpair].push_back(paths.at(pId)); // save this (exp, node/block, path) pair in the available subpaths
      // }
    }

    // 3/4. Build Unanticipable sets
    // Need to walk paths STARTING at n and going to the end, and do something similar to the above
    for (unsigned int pId = 0; pId < paths.size(); pId++) // check every path...
    {
      bool opEvaluated = false;
      bool valEvaluated = false;
      bool includeInSet = true;
      if (paths.at(pId)->fromStart == false && paths.at(pId)->containsValue(instValue)) // only check this path if it contains the instruction somewhere
      {
        int bId = paths.at(pId)->valueBlockMap[instValue]; // the block ID in this path that contains the instruction

        for (int prevBlockId = 0; prevBlockId < bId; prevBlockId++) // check each instruction UP to the previous block to see if the expression is contained...
        {
          // check each instruction in this block
          BasicBlock* blk = const_cast<BasicBlock*>(paths.at(pId)->nodes.at(bId));
          for (BasicBlock::iterator prevBlockInstItr = blk->begin(), prevBlockInstItrEnd = blk->end(); prevBlockInstItr != prevBlockInstItrEnd; ++prevBlockInstItr)
          {
            Value* instValue = dyn_cast<Value>(&(*prevBlockInstItr)); // cast instruction to value
            if (valEvaluated == false) //instValue
            {
              if (opEvaluated == true)
              {
                includeInSet = false;
              }
              valEvaluated = true;
            }
            for (unsigned int vId = 0; vId < operands.size(); vId++)
            {
              if (instValue == operands.at(vId)) // match in the operand list (one of the operands was re-evaluated by a previous instruction)
              {
                opEvaluated = true;
              }
            }
          }
        }

        // was not killed along some path from start to this node n
        if (includeInSet == false)
        {
          ExpressionNodePair enpair = std::make_pair(instValue, paths.at(pId)->nodes.at(bId));
          AnticipableSubPaths[enpair].push_back(paths.at(pId)); // save this (exp, node/block, path) pair in the available subpaths
        }
        else // it was killed, so it belongs in the unavailable subpaths thing
        {
          ExpressionNodePair enpair = std::make_pair(instValue, paths.at(pId)->nodes.at(bId));
          UnAnticipableSubPaths[enpair].push_back(paths.at(pId)); // save this (exp, node/block, path) pair in the available subpaths
        }
      } 
      // else // this instruction isn't even available on this path... so it belongs in the unavailable subpaths group
      // {
      //   ExpressionNodePair enpair = std::make_pair(instValue, NULL);
      //   UnAnticipableSubPaths[enpair].push_back(paths.at(pId)); // save this (exp, node/block, path) pair in the available subpaths
      // }
    }

    // https://groups.google.com/forum/#!topic/llvm-dev/SJXFz0-Ck6A
    // cast instruction to value, and compare against operands... if any are equal, then we assume they were KILLED, and yeah.... so on and so forth
  }

  ////////////////////////////////////////////////////////////
  // Now build the benefit and cost paths
  ////////////////////////////////////////////////////////////
  // cost path: paths obtained by concatenating unavailable paths with unanticipable paths
  ////////////////////////////////////////////////////////////
  for(std::map<ExpressionNodePair, vector<GraphPath*> >::iterator iter1 = UnAvailableSubPaths.begin(); iter1 != UnAvailableSubPaths.end(); ++iter1)
  {
    ExpressionNodePair enpair1 = iter1->first;
    for(std::map<ExpressionNodePair, vector<GraphPath*> >::iterator iter2 = UnAnticipableSubPaths.begin(); iter2 != UnAnticipableSubPaths.end(); ++iter2)
    {
      ExpressionNodePair enpair2 = iter2->first;

      // If these ExpressionNode pairs correspond to the same thing, then the paths can obviously be joined
      if (enpair1.first == enpair2.first && enpair1.second == enpair2.second)
      {
        // join each of the paths together now...
        vector<GraphPath*> p1s = iter1->second;
        vector<GraphPath*> p2s = iter2->second;
        for (unsigned int p1i = 0; p1i < p1s.size(); p1i++)
        {
          for (unsigned int p2i = 0; p2i < p2s.size(); p2i++) 
          {
            GraphPath* join = p1s.at(p1i)->concat(p2s.at(p2i));
            CostPaths[enpair1].push_back(join);
          }
        }
      }

      // // check to see if these guys can be joined...
      // // if the last node on path1 == first node on path2
      // unsigned int n1 = iter1->second->nodes.size();
      // unsigned int n2 = iter2->second->nodes.size();
      // if (iter1->second->nodes.at(n1 - 1) == iter2->second->nodes.at(n2 - 1))
      // {
        // GraphPath* join = iter1->second->concat(iter2->second);
        // CostPaths.push_back(join);
      // }
    }
  }

  ////////////////////////////////////////////////////////////
  // benefit path: paths obtained by concatenating available paths with anticipable paths
  ////////////////////////////////////////////////////////////
  for(std::map<ExpressionNodePair, vector<GraphPath*> >::iterator iter1 = AvailableSubPaths.begin(); iter1 != AvailableSubPaths.end(); ++iter1)
  {
    ExpressionNodePair enpair1 = iter1->first;
    for(std::map<ExpressionNodePair, vector<GraphPath*> >::iterator iter2 = AnticipableSubPaths.begin(); iter2 != AnticipableSubPaths.end(); ++iter2)
    {
      ExpressionNodePair enpair2 = iter2->first;
      // If these ExpressionNode pairs correspond to the same thing, then the paths can obviously be joined
      if (enpair1.first == enpair2.first && enpair1.second == enpair2.second)
      {
        // join each of the paths together now...
        vector<GraphPath*> p1s = iter1->second;
        vector<GraphPath*> p2s = iter2->second;
        for (unsigned int p1i = 0; p1i < p1s.size(); p1i++)
        {
          for (unsigned int p2i = 0; p2i < p2s.size(); p2i++) 
          {
            GraphPath* join = p1s.at(p1i)->concat(p2s.at(p2i));
            BenefitPaths[enpair1].push_back(join);
          }
        }
      }
    }
  }

  cout << "Done calculating sets and whatnot - preparing to run PRE" << endl;

  if (!NoLoads)
    MD = &getAnalysis<MemoryDependenceAnalysis>();
  DT = &getAnalysis<DominatorTree>();
  TD = getAnalysisIfAvailable<DataLayout>();
  TLI = &getAnalysis<TargetLibraryInfo>();
  VN.setAliasAnalysis(&getAnalysis<AliasAnalysis>());
  VN.setMemDep(MD);
  VN.setDomTree(DT);

  bool Changed = false;
  bool ShouldContinue = true;

  // Merge unconditional branches, allowing PgoPre to catch more
  // optimization opportunities.
  for (Function::iterator FI = F.begin(), FE = F.end(); FI != FE; ) {
    BasicBlock *BB = FI++;

    bool removedBlock = MergeBlockIntoPredecessor(BB, this);
    if (removedBlock) ++NumPgoPreBlocks;

    Changed |= removedBlock;
  }

  unsigned Iteration = 0;
  while (ShouldContinue) {
    DEBUG(dbgs() << "PgoPre iteration: " << Iteration << "\n");
    ShouldContinue = iterateOnFunction(F);
    if (splitCriticalEdges())
      ShouldContinue = true;
    Changed |= ShouldContinue;
    ++Iteration;
  }

  EnablePgoPre = true; // force PRE to happen for the purposes of testing PGO-PRE
  if (EnablePgoPre) {
    bool PgoPreChanged = true;
    while (PgoPreChanged) {
      PgoPreChanged = performPgoPre(F);
      Changed |= PgoPreChanged;
    }
  }
  // FIXME: Should perform PgoPre again after PgoPre does something.  PgoPre can move
  // computations into blocks where they become fully redundant.  Note that
  // we can't do this until PgoPre's critical edge splitting updates memdep.
  // Actually, when this happens, we should just fully integrate PgoPre into PgoPre.

  cleanupGlobalSets();

  return Changed;
}


bool PgoPre::processBlock(BasicBlock *BB) {
  // FIXME: Kill off InstrsToErase by doing erasing eagerly in a helper function
  // (and incrementing BI before processing an instruction).
  assert(InstrsToErase.empty() &&
         "We expect InstrsToErase to be empty across iterations");
  bool ChangedFunction = false;

  for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
       BI != BE;) {
    ChangedFunction |= processInstruction(BI);
    if (InstrsToErase.empty()) {
      ++BI;
      continue;
    }

    // If we need some instructions deleted, do it now.
    NumPgoPreInstr += InstrsToErase.size();

    // Avoid iterator invalidation.
    bool AtStart = BI == BB->begin();
    if (!AtStart)
      --BI;

    for (SmallVector<Instruction*, 4>::iterator I = InstrsToErase.begin(),
         E = InstrsToErase.end(); I != E; ++I) {
      DEBUG(dbgs() << "PgoPre removed: " << **I << '\n');
      if (MD) MD->removeInstruction(*I);
      DEBUG(verifyRemoved(*I));
      (*I)->eraseFromParent();
    }
    InstrsToErase.clear();

    if (AtStart)
      BI = BB->begin();
    else
      ++BI;
  }

  return ChangedFunction;
}

/// performPgoPre - Perform a purely local form of PgoPre that looks for diamond
/// control flow patterns and attempts to perform simple PgoPre at the join point.
bool PgoPre::performPgoPre(Function &F) {
  cout << "Trying to perform PGO-PRE" << endl;
  bool Changed = false;
  SmallVector<std::pair<Value*, BasicBlock*>, 8> predMap;
  for (df_iterator<BasicBlock*> DI = df_begin(&F.getEntryBlock()),
       DE = df_end(&F.getEntryBlock()); DI != DE; ++DI) {
    BasicBlock *CurrentBlock = *DI;

    // Nothing to PgoPre in the entry block.
    cout << "(CurrentBlock == &F.getEntryBlock())" << endl;
    if (CurrentBlock == &F.getEntryBlock()) continue;

    // Don't perform PgoPre on a landing pad.
    cout << "(CurrentBlock->isLandingPad())" << endl;
    if (CurrentBlock->isLandingPad()) continue;

    for (BasicBlock::iterator BI = CurrentBlock->begin(),
         BE = CurrentBlock->end(); BI != BE; ) {
      Instruction *CurInst = BI++;

      cout << "checking instruction type" << endl;
      if (isa<AllocaInst>(CurInst) ||
          isa<TerminatorInst>(CurInst) || isa<PHINode>(CurInst) ||
          CurInst->getType()->isVoidTy() ||
          CurInst->mayReadFromMemory() || CurInst->mayHaveSideEffects() ||
          isa<DbgInfoIntrinsic>(CurInst))
        continue;

      // Don't do PgoPre on compares. The PHI would prevent CodeGenPrepare from
      // sinking the compare again, and it would force the code generator to
      // move the i1 from processor flags or predicate registers into a general
      // purpose register.
      cout << "(isa<CmpInst>(CurInst))" << endl;
      if (isa<CmpInst>(CurInst))
        continue;

      // We don't currently value number ANY inline asm calls.
      cout << "(CallInst *CallI = dyn_cast<CallInst>(CurInst))" << endl;
      if (CallInst *CallI = dyn_cast<CallInst>(CurInst))
        if (CallI->isInlineAsm())
          continue;

      uint32_t ValNo = VN.lookup(CurInst);

      // Look for the predecessors for PgoPre opportunities.  We're
      // only trying to solve the basic diamond case, where
      // a value is computed in the successor and one predecessor,
      // but not the other.  We also explicitly disallow cases
      // where the successor is its own predecessor, because they're
      // more complicated to get right.
      unsigned NumWith = 0;
      unsigned NumWithout = 0;
      BasicBlock *PgoPrePred = 0;
      predMap.clear();

      for (pred_iterator PI = pred_begin(CurrentBlock),
           PE = pred_end(CurrentBlock); PI != PE; ++PI) {
        BasicBlock *P = *PI;
        // We're not interested in PgoPre where the block is its
        // own predecessor, or in blocks with predecessors
        // that are not reachable.
        if (P == CurrentBlock) {
          NumWithout = 2;
          break;
        } else if (!DT->isReachableFromEntry(P))  {
          NumWithout = 2;
          break;
        }

        Value* predV = findLeader(P, ValNo);
        if (predV == 0) {
          predMap.push_back(std::make_pair(static_cast<Value *>(0), P));
          PgoPrePred = P;
          ++NumWithout;
        } else if (predV == CurInst) {
          /* CurInst dominates this predecessor. */
          NumWithout = 2;
          break;
        } else {
          predMap.push_back(std::make_pair(predV, P));
          ++NumWith;
        }
      }

      // Don't do PgoPre when it might increase code size, i.e. when
      // we would need to insert instructions in more than one pred.
      cout << "(NumWithout != 1 || NumWith == 0)" << endl;
      if (NumWithout != 1 || NumWith == 0)
        continue;

      // Don't do PgoPre across indirect branch.
      cout << "(isa<IndirectBrInst>(PgoPrePred->getTerminator()))" << endl;
      if (isa<IndirectBrInst>(PgoPrePred->getTerminator()))
        continue;

      // We can't do PgoPre safely on a critical edge, so instead we schedule
      // the edge to be split and perform the PgoPre the next time we iterate
      // on the function.
      cout << "(isCriticalEdge(PgoPrePred->getTerminator(), SuccNum))" << endl;
      unsigned SuccNum = GetSuccessorNumber(PgoPrePred, CurrentBlock);
      if (isCriticalEdge(PgoPrePred->getTerminator(), SuccNum)) {
        toSplit.push_back(std::make_pair(PgoPrePred->getTerminator(), SuccNum));
        continue;
      }

      // Instantiate the expression in the predecessor that lacked it.
      // Because we are going top-down through the block, all value numbers
      // will be available in the predecessor by the time we need them.  Any
      // that weren't originally present will have been instantiated earlier
      // in this loop.
      Instruction *PgoPreInstr = CurInst->clone();
      bool success = true;
      for (unsigned i = 0, e = CurInst->getNumOperands(); i != e; ++i) {
        Value *Op = PgoPreInstr->getOperand(i);
        if (isa<Argument>(Op) || isa<Constant>(Op) || isa<GlobalValue>(Op))
          continue;

        if (Value *V = findLeader(PgoPrePred, VN.lookup(Op))) {
          PgoPreInstr->setOperand(i, V);
        } else {
          cout << "success = false" << endl;
          success = false;
          break;
        }
      }

      // Fail out if we encounter an operand that is not available in
      // the PgoPre predecessor.  This is typically because of loads which
      // are not value numbered precisely.
      if (!success) {
        DEBUG(verifyRemoved(PgoPreInstr));
        delete PgoPreInstr;
        continue;
      }

      // Perform PRE instruction insertion here... if we have enabled speculation up to this point
      // bool PgoPre::EnableSpec(Value* val, const BasicBlock* n)
      cout << "EnableSpec?" << endl;
      Value* instVal = (Value*)(PgoPreInstr);
      if (EnableSpec(instVal, PgoPrePred))
      {
        PgoPreInstr->insertBefore(PgoPrePred->getTerminator());
        PgoPreInstr->setName(CurInst->getName() + ".pre");
        PgoPreInstr->setDebugLoc(CurInst->getDebugLoc());
        VN.add(PgoPreInstr, ValNo);
        ++NumPgoPrePgoPre;
      }
      else
      {
        cout << "Speculation not enabled at predecessor, so we skip PRE injection." << endl;
        cout << "BAILOUT" << endl;
        delete PgoPreInstr;
        continue;
      }

      // Update the availability map to include the new instruction.
      addToLeaderTable(ValNo, PgoPreInstr, PgoPrePred);

      // Create a PHI to make the value available in this block.
      PHINode* Phi = PHINode::Create(CurInst->getType(), predMap.size(),
                                     CurInst->getName() + ".pre-phi",
                                     CurrentBlock->begin());
      for (unsigned i = 0, e = predMap.size(); i != e; ++i) {
        if (Value *V = predMap[i].first)
          Phi->addIncoming(V, predMap[i].second);
        else
          Phi->addIncoming(PgoPreInstr, PgoPrePred);
      }

      VN.add(Phi, ValNo);
      addToLeaderTable(ValNo, Phi, CurrentBlock);
      Phi->setDebugLoc(CurInst->getDebugLoc());
      CurInst->replaceAllUsesWith(Phi);
      if (Phi->getType()->getScalarType()->isPointerTy()) {
        // Because we have added a PHI-use of the pointer value, it has now
        // "escaped" from alias analysis' perspective.  We need to inform
        // AA of this.
        for (unsigned ii = 0, ee = Phi->getNumIncomingValues(); ii != ee;
             ++ii) {
          unsigned jj = PHINode::getOperandNumForIncomingValue(ii);
          VN.getAliasAnalysis()->addEscapingUse(Phi->getOperandUse(jj));
        }

        if (MD)
          MD->invalidateCachedPointerInfo(Phi);
      }
      VN.erase(CurInst);
      removeFromLeaderTable(ValNo, CurInst, CurrentBlock);

      DEBUG(dbgs() << "PgoPre PgoPre removed: " << *CurInst << '\n');
      if (MD) MD->removeInstruction(CurInst);
      DEBUG(verifyRemoved(CurInst));
      CurInst->eraseFromParent();
      Changed = true;
    }
  }

  if (splitCriticalEdges())
    Changed = true;

  return Changed;
}

/// splitCriticalEdges - Split critical edges found during the previous
/// iteration that may enable further optimization.
bool PgoPre::splitCriticalEdges() {
  if (toSplit.empty())
    return false;
  do {
    std::pair<TerminatorInst*, unsigned> Edge = toSplit.pop_back_val();
    SplitCriticalEdge(Edge.first, Edge.second, this);
  } while (!toSplit.empty());
  if (MD) MD->invalidateCachedPredecessors();
  return true;
}

/// iterateOnFunction - Executes one iteration of PgoPre
bool PgoPre::iterateOnFunction(Function &F) {
  cleanupGlobalSets();

  // Top-down walk of the dominator tree
  bool Changed = false;
#if 0
  // Needed for value numbering with phi construction to work.
  ReversePostOrderTraversal<Function*> RPOT(&F);
  for (ReversePostOrderTraversal<Function*>::rpo_iterator RI = RPOT.begin(),
       RE = RPOT.end(); RI != RE; ++RI)
    Changed |= processBlock(*RI);
#else
  for (df_iterator<DomTreeNode*> DI = df_begin(DT->getRootNode()),
       DE = df_end(DT->getRootNode()); DI != DE; ++DI)
    Changed |= processBlock(DI->getBlock());
#endif

  return Changed;
}

void PgoPre::cleanupGlobalSets() {
  VN.clear();
  LeaderTable.clear();
  TableAllocator.Reset();
}

/// verifyRemoved - Verify that the specified instruction does not occur in our
/// internal data structures.
void PgoPre::verifyRemoved(const Instruction *Inst) const {
  VN.verifyRemoved(Inst);

  // Walk through the value number scope to make sure the instruction isn't
  // ferreted away in it.
  for (DenseMap<uint32_t, LeaderTableEntry>::const_iterator
       I = LeaderTable.begin(), E = LeaderTable.end(); I != E; ++I) {
    const LeaderTableEntry *Node = &I->second;
    assert(Node->Val != Inst && "Inst still in value numbering scope!");

    while (Node->Next) {
      Node = Node->Next;
      assert(Node->Val != Inst && "Inst still in value numbering scope!");
    }
  }
}

int PgoPre::Benefit(Value* val, const BasicBlock* n)
{
  int benefit = 0; 

  for (std::map<ExpressionNodePair, vector<GraphPath*> >::iterator iter1 = BenefitPaths.begin(); iter1 != BenefitPaths.end(); ++iter1)
  {
    ExpressionNodePair enpair1 = iter1->first;
    vector<GraphPath*> paths = iter1->second;
    if (enpair1.first == val && enpair1.second == n)
    {
      for (unsigned int i = 0; i < paths.size(); i++)
      {
        benefit += paths.at(i)->freq();
      }
    }
  }
  
  return benefit;
}

int PgoPre::Cost(Value* val, const BasicBlock* n)
{
  int cost = 0;

  for (std::map<ExpressionNodePair, vector<GraphPath*> >::iterator iter1 = CostPaths.begin(); iter1 != CostPaths.end(); ++iter1)
  {
    ExpressionNodePair enpair1 = iter1->first;
    vector<GraphPath*> paths = iter1->second;
    if (enpair1.first == val && enpair1.second == n)
    {
      for (unsigned int i = 0; i < paths.size(); i++)
      {
        cost += paths.at(i)->freq();
      }
    }
  }
  
  // for (unsigned int i = 0; i < CostPaths.size(); i++)
  // {
  //   cost += CostPaths.at(i)->freq();
  // }

  return cost;
}

double PgoPre::ProbCost(Value* val, const BasicBlock* n)
{
  if (BlockFreqMap[n] == 0)
  {
    cout << "ERROR: BlockFreqMap[n] == 0 for cost" << endl;
    return 0;
  }
  return (double)Cost(val, n) / (double)BlockFreqMap[n];
}

double PgoPre::ProbBenefit(Value* val, const BasicBlock* n)
{
  if (BlockFreqMap[n] == 0)
  {
    cout << "ERROR: BlockFreqMap[n] == 0 for benefit" << endl;
    return 0;
  }
  return (double)Benefit(val, n) / (double)BlockFreqMap[n];
}

bool PgoPre::EnableSpec(Value* val, const BasicBlock* n)
{
  return (ProbCost(val, n) < ProbBenefit(val, n));
}
