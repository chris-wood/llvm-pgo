//===- PgoPre.cpp - Partial Redundancy Elimination ---------------------------===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements the well-known Partial Redundancy Elimination
// optimization, using an SSA formulation based on e-paths.  See this paper for
// more information:
//
//  E-path_PgoPre: partial redundancy elimination made easy
//  By: Dhananjay M. Dhamdhere   In: ACM SIGPLAN Notices. Vol 37, #8, 2002
//    http://doi.acm.org/10.1145/596992.597004
//
// This file actually implements a sparse version of the algorithm, using SSA
// and CFG properties instead of bit-vectors.
//
//===----------------------------------------------------------------------===//asd

#include "llvm/ADT/Statistic.h"
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

// for convenience
#include <iostream>
using namespace std; 

typedef std::pair<const BasicBlock*, const BasicBlock*> GraphEdge;

class DominatorSet 
{
public:

  // Function* function;
  // Instruction* start;
  map<Instruction, vector<Instruction> > dominatorMap;

  void buildDominatorSet(Function* function)
  {
    // dominator of the start node is the start itself:
    //
    // Dom(n0) = {n0}
    dominatorMap[*(inst_begin(&function))].push_back(*(inst_begin(&function)));

    // for all other nodes, set all nodes as the dominators:
    // 
    // for each n in N - {n0}
    //  Dom(n) = N;
    for (inst_iterator instItr = inst_begin(&function), E = inst_end(&function); instItr != E; ++instItr)
    {
      if (instItr != inst_begin(&function))
      {
        Instruction& inst = *instItr;
        for (inst_iterator allItr = inst_begin(&function), E2 = inst_end(&function); allItr != E2; ++allItr)
        {
          dominatorMap[inst].push_back(*allItr)
        }
      }
    }

    // iteratively eliminate nodes that are not dominators:
    //
    // while changes in any Dom(n)
    //  for each n in N - {n0}:
    //    Dom(n) = {n} union with intersection over Dom(p) for all p in pred(n)
    bool changes = false;
    for (inst_iterator instItr = inst_begin(&function), E = inst_end(&function); instItr != E; ++instItr)
    {
      if (instItr != inst_begin(&function))
      {
        Instruction& inst = *instItr;
        vector<Instruction> predecessors;
        BasicBlock* currBB = inst.getParent();
        BasicBlock* prevBB = (inst.getParent())->getSinglePredecessor();

        // Check this BB and parent BB to find all predecessors
        if (currBB != null)
        {
          Instruction& prevInst = null;
          for (BasicBlock::iterator i = currBB->begin(), e = currBB->end(); i != e; ++i)
          {
            if (prevInst == null)
            {
              prevInst = *(i);
            }
            else
            {
              if (inst == *(i))
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

        if (prevBB != null)
        {
          Instruction& prevInst = null;
          for (BasicBlock::iterator i = prevBB->begin(), e = prevBB->end(); i != e; ++i)
          {
            if (prevInst == null)
            {
              prevInst = *(i);
            }
            else
            {
              if (inst == *(i))
              {
                predecessors.push_back(prevInst);
                break; // don't bother going father since we'll pass the instructon
              }
            }
          } 
        }

        // Build the the intersection of all dominators of the instructions in predecessor...
        //    Dom(n) = {n} union with intersection over Dom(p) for all p in pred(n)
        vector<Instruction> temp_intersect;
        vector<Instruction> intersect;
        Instruction& candPred = *(predecessors.begin());
        for (Instruction::iterator setItr = dominatorMap[candPred].begin(); setItr != dominatorMap[candPred].end(); setItr++)
        {
          temp_intersect.push_back(*setItr);
        }
        for (Instruction::iterator predItr = temp_intersect.begin(); predItr != temp_intersect.end(); predItr++)
        {
          candPred = *predItr;
          bool inAllSets = true;

          // Traverse all predecessor dominators and see if this instruction is in each...
          for (Intruction::iterator otherPredItr = predecessors.begin(); otherPredItr != predecessors.end(); otherPredItr++)
          {
            Instruction& otherCandPred = *otherPredItr;
            if (otherPredItr != predecessors.begin()) 
            {
              bool inThisSet = false;
              // traverse over the domination set of this predecessor
              for (Instruction::iterator setItr = dominatorMap[otherCandPred].begin(); setItr != dominatorMap[otherCandPred].end(); setItr++)
              {
                Instruction& cand = *setItr;
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
      }
    }
  }

  bool dominates(Instruction I, Instruction BI) // I dominates BI
  {
    for (Instruction::iterator itr = dominatorMap[BI].begin(); itr != dominatorMap[BI].end(); itr++)
    {
      Instruction& tmp = *itr;
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

namespace {
  //Statistic<> NumExprsEliminated("pgo-pre", "Number of expressions constantified");
  //Statistic<> NumRedundant      ("pgo-pre", "Number of redundant exprs eliminated");
  //Statistic<> NumInserted       ("pgo-pre", "Number of expressions inserted");

  struct PgoPre : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    PgoPre() : FunctionPass(ID) {
      initializePgoPrePass(*PassRegistry::getPassRegistry());
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequiredID(BreakCriticalEdgesID);  // No critical edges for now!
      AU.addRequired<PostDominatorTree>();
      //AU.addRequired<PostDominanceFrontier>();
      AU.addRequired<DominatorSet>();
      AU.addRequired<DominatorTree>();
      //AU.addRequired<DominanceFrontier>();
      //AU.addRequired<ValueNumbering>();

      // PGO additions
      AU.addRequired<ProfileInfo>();
      //AU.addPreserved<ProfileInfo>();  // Does this work?
    }
    virtual bool runOnFunction(Function &F);

  private:
    // New content fancy shmancy
    ProfileInfo *PI;
    vector<GraphPath*> paths;

    // Block information - Map basic blocks in a function back and forth to
    // unsigned integers.
    std::vector<const BasicBlock*> BlockMapping;
    map<const BasicBlock*, unsigned> BlockNumbering;

    // ProcessedExpressions - Keep track of which expressions have already been
    // processed.
    set<Instruction*> ProcessedExpressions;

    // Provide access to the various analyses used...
    // DominatorSet      *DS;
    DominatorSet      *DS;
    DominatorTree     *DT; 
    PostDominatorTree *PDT;
    //DominanceFrontier *DF; 
    //PostDominanceFrontier *PDF;
    // ValueTracking    *VN;

    // AvailableBlocks - Contain a mapping of blocks with available expression
    // values to the expression value itself.  This can be used as an efficient
    // way to find out if the expression is available in the block, and if so,
    // which version to use.  This map is only used while processing a single
    // expression.
    //
    typedef map<BasicBlock*, Instruction*> AvailableBlocksTy;
    AvailableBlocksTy AvailableBlocks;

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
    
    // // Anticipatibility calculation...
    void MarkPostDominatingBlocksAnticipatible(DomTreeNode *N,
                                               std::vector<char> &AntBlocks,
                                               Instruction *Occurrence);
    // void CalculateAnticipatiblityForOccurrence(unsigned BlockNo,
    //                                           std::vector<char> &AntBlocks,
    //                                           Instruction *Occurrence
    //                                           );
    // void CalculateAnticipatibleBlocks(const std::map<unsigned, Instruction*> &D,
    //                                   std::vector<char> &AnticipatibleBlocks);

    // void MarkOccurrenceAvailableInAllDominatedBlocks(Instruction *Occurrence,
    //                                                  BasicBlock *StartBlock);
    // void ReplaceDominatedAvailableOccurrencesWith(Instruction *NewOcc,
    //                                               DominatorTree::Node *N);
    bool ProcessExpression(const Instruction *I);

    // // PgoPre for an expression
    // bool PgoPre::EnabSpec(Instruction *I);

    void BuildReversePostOrderBlockMap(vector<const BasicBlock*> &visited, const BasicBlock* curr);
  };

  // RegisterOpt<PgoPre> Z("pgo-pre", "PGO-Based Partial Redundancy Elimination");
}

void PgoPre::BuildReversePostOrderBlockMap(vector<const BasicBlock*> &visited, const BasicBlock* curr)
{
  if (curr == NULL)
    return;
  for (llvm::succ_const_iterator itr = succ_begin(curr); itr != succ_end(curr); itr++)
  {
    BuildReversePostOrderBlockMap(visited, *itr);
  }
  visited.push_back(curr);
}

// register the pass information
char PgoPre::ID = 0;
INITIALIZE_PASS_BEGIN(PgoPre, "pgo-pre", "Profile Guided PRE", false, false)
INITIALIZE_AG_DEPENDENCY(ProfileInfo)
INITIALIZE_PASS_END(PgoPre, "pgo-pre", "Profile Guided PRE", false, false)

FunctionPass *llvm::createPgoPrePass() { return new PgoPre(); }


bool PgoPre::runOnFunction(Function &F) {
  //VN  = &getAnalysis<ValueNumbering>();
  //DS  = &getAnalysis<DominatorSet>();
  DT  = &getAnalysis<DominatorTree>();
  //DF  = &getAnalysis<DominanceFrontier>();
  PDT = &getAnalysis<PostDominatorTree>();
  //PDF = &getAnalysis<PostDominanceFrontier>();

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
  DS = new DominatorSet();
  DS->function = &F;
  DS->start = &(inst_begin(&F)); // pull out first instruction in the function CFG

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
  ////////////////////////////////////////////////////////////

  //// OLD PRE CODE BELOW

  // // Traverse the current function depth-first in dominator-tree order.  This
  // // ensures that we see all definitions before their uses (except for PHI
  // // nodes), allowing us to hoist dependent expressions correctly.
  bool Changed = false;
  for (unsigned i = 0, e = BlockMapping.size(); i != e; ++i)
    Changed |= ProcessBlock(BlockMapping[i]);

  // // Free memory
  // BlockMapping.clear();
  // BlockNumbering.clear();
  // ProcessedExpressions.clear();
  // return Changed;

  return false;
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
  return (double)Cost(val, n) / (double)BlockFreqMap[n];
}

double PgoPre::ProbBenefit(Value* val, const BasicBlock* n)
{
  return (double)Benefit(val, n) / (double)BlockFreqMap[n];
}

bool PgoPre::EnableSpec(Value* val, const BasicBlock* n)
{
  return (ProbCost(val, n) < ProbBenefit(val, n));
}

// ProcessBlock - Process any expressions first seen in this block...
//
bool PgoPre::ProcessBlock(const BasicBlock *BB) 
{
  bool Changed = false;

  // PgoPre expressions first defined in this block...
  const Instruction *PrevInst = 0;
  for (BasicBlock::const_iterator I = BB->begin(); I != BB->end(); )
  {
    if (ProcessExpression(I)) {
      // The current instruction may have been deleted, make sure to back up to
      // PrevInst instead.
      if (PrevInst)
        I = PrevInst;
      else
        I = BB->begin();
      Changed = true;
    } else {
      PrevInst = I++;
    }
  }

  return Changed;
}

void PgoPre::MarkPostDominatingBlocksAnticipatible(DomTreeNode *N,
                                                std::vector<char> &AntBlocks,
                                                Instruction *Occurrence) {

/*  unsigned BlockNo = BlockNumbering[N->getBlock()];

  if (AntBlocks[BlockNo]) return;  // Already known to be anticipatible??

  // Check to see if any of the operands are defined in this block, if so, the
  // entry of this block does not anticipate the expression.  This computes
  // "transparency".
  for (unsigned i = 0, e = Occurrence->getNumOperands(); i != e; ++i)
    if (Instruction *I = dyn_cast<Instruction>(Occurrence->getOperand(i)))
      if (I->getParent() == N->getBlock())  // Operand is defined in this block!
        return;

  if (isa<LoadInst>(Occurrence))
    return;        // FIXME: compute transparency for load instructions using AA

  // Insert block into AntBlocks list...
  AntBlocks[BlockNo] = true;

  for (DomTreeNode::iterator I = N->begin(), E = N->end(); I != E; ++I)
    MarkPostDominatingBlocksAnticipatible(*I, AntBlocks, Occurrence);
*/
}

// /// caw: this is where profile-guided data is actually leveraged

// bool PgoPre::EnabSpec(Instruction *I)
// {

//   // EnabSpec is true iff:
//   // (cost(exp, n) / freq(n)) < (benefit(exp, n) / freq(n)),
//   // cost(exp, n) = sum_{paths p in CostPaths} p.frequency,
//   // cost(exp, n) = sum_{paths p in BenefitPaths} p.frequency,

//   return false;
// }

// void PgoPre::CalculateAnticipatiblityForOccurrence(unsigned BlockNo, std::vector<char> &AntBlocks, Instruction *Occurrence) {
//   if (AntBlocks[BlockNo]) return;  // Block already anticipatible!

//   const BasicBlock *BB = BlockMapping[BlockNo];

//   // For each occurrence, mark all post-dominated blocks as anticipatible...
//   MarkPostDominatingBlocksAnticipatible(PDT->getNode(const_cast<BasicBlock*>(BB)), AntBlocks, Occurrence);

//   // Next, mark any blocks in the post-dominance frontier as anticipatible iff
//   // all successors are anticipatible.
//   //
//   PostDominanceFrontier::iterator PDFI = PDF->find(BB);
//   if (PDFI != DF->end())
//     for (std::set<BasicBlock*>::iterator DI = PDFI->second.begin();
//          DI != PDFI->second.end(); ++DI) {
//       BasicBlock *PDFBlock = *DI;
//       bool AllSuccessorsAnticipatible = true;
//       for (succ_iterator SI = succ_begin(PDFBlock), SE = succ_end(PDFBlock);
//            SI != SE; ++SI)
//         if (!AntBlocks[BlockNumbering[*SI]]) {
//           AllSuccessorsAnticipatible = false;
//           break;
//         }

//       if (AllSuccessorsAnticipatible)
//       {
//         CalculateAnticipatiblityForOccurrence(BlockNumbering[PDFBlock], AntBlocks, Occurrence);
//       }
//     }
// }


// void PgoPre::CalculateAnticipatibleBlocks(const std::map<unsigned, Instruction*> &Defs,
//                                        std::vector<char> &AntBlocks) {
//   // Initialize to zeros...
//   AntBlocks.resize(BlockMapping.size());

//   // Loop over all of the expressions...
//   for (std::map<unsigned, Instruction*>::const_iterator I = Defs.begin(), E = Defs.end(); I != E; ++I)
//     CalculateAnticipatiblityForOccurrence(I->first, AntBlocks, I->second);
// }

// /// MarkOccurrenceAvailableInAllDominatedBlocks - Add entries to AvailableBlocks
// /// for all nodes dominated by the occurrence to indicate that it is now the
// /// available occurrence to use in any of these blocks.
// ///
// void PgoPre::MarkOccurrenceAvailableInAllDominatedBlocks(Instruction *Occurrence,
//                                                       BasicBlock *BB) {
//   // FIXME: There are much more efficient ways to get the blocks dominated
//   // by a block.  Use them.
//   //
//   DominatorTree::Node *N = DT->getNode(Occurrence->getParent());
//   for (df_iterator<DominatorTree::Node*> DI = df_begin(N), E = df_end(N);
//        DI != E; ++DI)
//     AvailableBlocks[(*DI)->getBlock()] = Occurrence;
// }

// /// ReplaceDominatedAvailableOccurrencesWith - This loops over the region
// /// dominated by N, replacing any available expressions with NewOcc.
// void PgoPre::ReplaceDominatedAvailableOccurrencesWith(Instruction *NewOcc,
//                                                    DominatorTree::Node *N) {
//   BasicBlock *BB = N->getBlock();
//   Instruction *&ExistingAvailableVal = AvailableBlocks[BB];

//   // If there isn't a definition already active in this node, make this the new
//   // active definition...
//   if (ExistingAvailableVal == 0) {
//     ExistingAvailableVal = NewOcc;
    
//     for (DominatorTree::Node::iterator I = N->begin(), E = N->end(); I != E;++I)
//       ReplaceDominatedAvailableOccurrencesWith(NewOcc, *I);
//   } else {
//     // If there is already an active definition in this block, replace it with
//     // NewOcc, and force it into all dominated blocks.
//     DEBUG(std::cerr << "  Replacing dominated occ %"
//           << ExistingAvailableVal->getName() << " with %" << NewOcc->getName()
//           << "\n");
//     assert(ExistingAvailableVal != NewOcc && "NewOcc already inserted??");
//     ExistingAvailableVal->replaceAllUsesWith(NewOcc);
//     ++NumRedundant;

//     assert(ExistingAvailableVal->getParent() == BB &&
//            "OldOcc not defined in current block?");
//     BB->getInstList().erase(ExistingAvailableVal);

//     // Mark NewOCC as the Available expression in all blocks dominated by BB
//     for (df_iterator<DominatorTree::Node*> DI = df_begin(N), E = df_end(N);
//          DI != E; ++DI)
//       AvailableBlocks[(*DI)->getBlock()] = NewOcc;
//   }  
// }


/// ProcessExpression - Given an expression (instruction) process the
/// instruction to remove any partial redundancies induced by equivalent
/// computations.  Note that we only need to PgoPre each expression once, so we
/// keep track of whether an expression has been PgoPre'd already, and don't PgoPre an
/// expression again.  Expressions may be seen multiple times because process
/// the entire equivalence class at once, which may leave expressions later in
/// the control path.
///
bool PgoPre::ProcessExpression(const Instruction *Expr) 
{
  // Check to see if we should even try to deal with this expression (SSA instruction)
  if (Expr->mayWriteToMemory() || Expr->getType() == Type::VoidTy || isa<PHINode>(Expr))
    return false;         // Cannot move expression
  if (ProcessedExpressions.count(Expr)) return false; // Already processed.

  // Ok, this is the first time we have seen the expression.  Build a set of
  // equivalent expressions using SSA def/use information.  We consider
  // expressions to be equivalent if they are the same opcode and have
  // equivalent operands.  As a special case for SSA, values produced by PHI
  // nodes are considered to be equivalent to all of their operands.
  //
  std::vector<Value*> Values;
  VN->getEqualNumberNodes(Expr, Values);

#if 0
  // FIXME: This should handle PHI nodes correctly.  To do this, we need to
  // consider expressions of the following form equivalent to this set of
  // expressions:
  //
  // If an operand is a PHI node, add any occurrences of the expression with the
  // PHI operand replaced with the PHI node operands.  This is only valid if the
  // PHI operand occurrences exist in blocks post-dominated by the incoming edge
  // of the PHI node.
#endif

  // We have to be careful to handle expression definitions which dominated by
  // other expressions.  These can be directly eliminated in favor of their
  // dominating value.  Keep track of which blocks contain definitions (the key)
  // and if a block contains a definition, which instruction it is.
  //
  std::map<unsigned, Instruction*> Definitions;
  Definitions.insert(std::make_pair(BlockNumbering[Expr->getParent()], Expr));

  bool Changed = false;

  // Look at all of the equal values.  If any of the values is not an
  // instruction, replace all other expressions immediately with it (it must be
  // an argument or a constant or something). Otherwise, convert the list of
  // values into a list of expression (instruction) definitions ordering
  // according to their dominator tree ordering.
  //
  Value *NonInstValue = 0;
  for (unsigned i = 0, e = Values.size(); i != e; ++i)
  {
    if (Instruction *I = dyn_cast<Instruction>(Values[i])) 
    {
      Instruction *&BlockInst = Definitions[BlockNumbering[I->getParent()]];
      if (BlockInst && BlockInst != I)  // Eliminate direct redundancy
      {    

        if (DS->dominates(I, BlockInst)) // I dom BlockInst
        {  
          BlockInst->replaceAllUsesWith(I);
          BlockInst->getParent()->getInstList().erase(BlockInst);
        } 
        else // BlockInst dom I
        {                            
          I->replaceAllUsesWith(BlockInst);
          I->getParent()->getInstList().erase(I);
          I = BlockInst;
        }
        ++NumRedundant;
      }
      BlockInst = I;
    } else {
      NonInstValue = Values[i];
    }
  }

  /// TODO: left off here...

//   std::vector<Value*>().swap(Values);  // Done with the values list

//   if (NonInstValue) {
//     // This is the good, though unlikely, case where we find out that this
//     // expression is equal to a constant or argument directly.  We can replace
//     // this and all of the other equivalent instructions with the value
//     // directly.
//     //
//     for (std::map<unsigned, Instruction*>::iterator I = Definitions.begin(),
//            E = Definitions.end(); I != E; ++I) {
//       Instruction *Inst = I->second;
//       // Replace the value with the specified non-instruction value.
//       Inst->replaceAllUsesWith(NonInstValue);       // Fixup any uses
//       Inst->getParent()->getInstList().erase(Inst); // Erase the instruction
//     }
//     NumExprsEliminated += Definitions.size();
//     return true;   // Program modified!
//   }

//   // There are no expressions equal to this one.  Exit early.
//   assert(!Definitions.empty() && "no equal expressions??");
// #if 0
//   if (Definitions.size() == 1) {
//     ProcessedExpressions.insert(Definitions.begin()->second);
//     return Changed;
//   }
// #endif
//   DEBUG(std::cerr << "\n====--- Expression: " << *Expr);
//   const Type *ExprType = Expr->getType();

//   // AnticipatibleBlocks - Blocks where the current expression is anticipatible.
//   // This is logically std::vector<bool> but using 'char' for performance.
//   std::vector<char> AnticipatibleBlocks;

//   // Calculate all of the blocks which the current expression is anticipatible.
//   CalculateAnticipatibleBlocks(Definitions, AnticipatibleBlocks);

//   // Print out anticipatible blocks...
//   DEBUG(std::cerr << "AntBlocks: ";
//         for (unsigned i = 0, e = AnticipatibleBlocks.size(); i != e; ++i)
//           if (AnticipatibleBlocks[i])
//             std::cerr << BlockMapping[i]->getName() <<" ";
//         std::cerr << "\n";);
  


//   // AvailabilityFrontier - Calculates the availability frontier for the current
//   // expression.  The availability frontier contains the blocks on the dominance
//   // frontier of the current available expressions, iff they anticipate a
//   // definition of the expression.
//   hash_set<unsigned> AvailabilityFrontier;

//   Instruction *NonPHIOccurrence = 0;

//   while (!Definitions.empty() || !AvailabilityFrontier.empty()) {
//     if (!Definitions.empty() &&
//         (AvailabilityFrontier.empty() ||
//          Definitions.begin()->first < *AvailabilityFrontier.begin())) {
//       Instruction *Occurrence = Definitions.begin()->second;
//       BasicBlock *BB = Occurrence->getParent();
//       Definitions.erase(Definitions.begin());

//       DEBUG(std::cerr << "PROCESSING Occurrence: " << *Occurrence);

//       // Check to see if there is already an incoming value for this block...
//       AvailableBlocksTy::iterator LBI = AvailableBlocks.find(BB);
//       if (LBI != AvailableBlocks.end()) {
//         // Yes, there is a dominating definition for this block.  Replace this
//         // occurrence with the incoming value.
//         if (LBI->second != Occurrence) {
//           DEBUG(std::cerr << "  replacing with: " << *LBI->second);
//           Occurrence->replaceAllUsesWith(LBI->second);
//           BB->getInstList().erase(Occurrence);   // Delete instruction
//           ++NumRedundant;
//         }
//       } else {
//         ProcessedExpressions.insert(Occurrence);
//         if (!isa<PHINode>(Occurrence))
//           NonPHIOccurrence = Occurrence;  // Keep an occurrence of this expr

//         // Okay, there is no incoming value for this block, so this expression
//         // is a new definition that is good for this block and all blocks
//         // dominated by it.  Add this information to the AvailableBlocks map.
//         //
//         MarkOccurrenceAvailableInAllDominatedBlocks(Occurrence, BB);

//         // Update the dominance frontier for the definitions so far... if a node
//         // in the dominator frontier now has all of its predecessors available,
//         // and the block is in an anticipatible region, we can insert a PHI node
//         // in that block.
//         DominanceFrontier::iterator DFI = DF->find(BB);
//         if (DFI != DF->end()) {
//           for (std::set<BasicBlock*>::iterator DI = DFI->second.begin();
//                DI != DFI->second.end(); ++DI) {
//             BasicBlock *DFBlock = *DI;
//             unsigned DFBlockID = BlockNumbering[DFBlock];
//             if (AnticipatibleBlocks[DFBlockID]) {
//               // Check to see if any of the predecessors of this block on the
//               // frontier are not available...
//               bool AnyNotAvailable = false;
//               for (pred_iterator PI = pred_begin(DFBlock),
//                      PE = pred_end(DFBlock); PI != PE; ++PI)
//                 if (!AvailableBlocks.count(*PI)) {
//                   AnyNotAvailable = true;
//                   break;
//                 }
            
//               // If any predecessor blocks are not available, add the node to
//               // the current expression dominance frontier.
//               if (AnyNotAvailable) {
//                 AvailabilityFrontier.insert(DFBlockID);
//               } else {
//                 // This block is no longer in the availability frontier, it IS
//                 // available.
//                 AvailabilityFrontier.erase(DFBlockID);

//                 // If all of the predecessor blocks are available (and the block
//                 // anticipates a definition along the path to the exit), we need
//                 // to insert a new PHI node in this block.  This block serves as
//                 // a new definition for the expression, extending the available
//                 // region.
//                 //
//                 PHINode *PN = new PHINode(ExprType, Expr->getName()+".pre",
//                                           DFBlock->begin());
//                 ProcessedExpressions.insert(PN);

//                 DEBUG(std::cerr << "  INSERTING PHI on frontier: " << *PN);

//                 // Add the incoming blocks for the PHI node
//                 for (pred_iterator PI = pred_begin(DFBlock),
//                        PE = pred_end(DFBlock); PI != PE; ++PI)
//                   if (*PI != DFBlock)
//                     PN->addIncoming(AvailableBlocks[*PI], *PI);
//                   else                          // edge from the current block
//                     PN->addIncoming(PN, DFBlock);

//                 Instruction *&BlockOcc = Definitions[DFBlockID];
//                 if (BlockOcc) {
//                   DEBUG(std::cerr <<"    PHI superceeds occurrence: "<<
//                         *BlockOcc);
//                   BlockOcc->replaceAllUsesWith(PN);
//                   BlockOcc->getParent()->getInstList().erase(BlockOcc);
//                   ++NumRedundant;
//                 }
//                 BlockOcc = PN;
//               }
//             }
//           }
//         }
//       }

//     } else {
//       // Otherwise we must be looking at a node in the availability frontier!
//       unsigned AFBlockID = *AvailabilityFrontier.begin();
//       AvailabilityFrontier.erase(AvailabilityFrontier.begin());
//       BasicBlock *AFBlock = BlockMapping[AFBlockID];

//       // We eliminate the partial redundancy on this frontier by inserting a PHI
//       // node into this block, merging any incoming available versions into the
//       // PHI and inserting a new computation into predecessors without an
//       // incoming value.  Note that we would have to insert the expression on
//       // the edge if the predecessor didn't anticipate the expression and we
//       // didn't break critical edges.
//       //
//       PHINode *PN = new PHINode(ExprType, Expr->getName()+".PgoPre",
//                                 AFBlock->begin());
//       DEBUG(std::cerr << "INSERTING PHI for PR: " << *PN);

//       // If there is a pending occurrence in this block, make sure to replace it
//       // with the PHI node...
//       std::map<unsigned, Instruction*>::iterator EDFI =
//         Definitions.find(AFBlockID);
//       if (EDFI != Definitions.end()) {
//         // There is already an occurrence in this block.  Replace it with PN and
//         // remove it.
//         Instruction *OldOcc = EDFI->second;
//         DEBUG(std::cerr << "  Replaces occurrence: " << *OldOcc);
//         OldOcc->replaceAllUsesWith(PN);
//         AFBlock->getInstList().erase(OldOcc);
//         Definitions.erase(EDFI);
//         ++NumRedundant;
//       }

//       for (pred_iterator PI = pred_begin(AFBlock), PE = pred_end(AFBlock);
//            PI != PE; ++PI) {
//         BasicBlock *Pred = *PI;
//         AvailableBlocksTy::iterator LBI = AvailableBlocks.find(Pred);
//         if (LBI != AvailableBlocks.end()) {    // If there is a available value
//           PN->addIncoming(LBI->second, Pred);  // for this pred, use it.
//         } else {                         // No available value yet...
//           unsigned PredID = BlockNumbering[Pred];

//           // Is the predecessor the same block that we inserted the PHI into?
//           // (self loop)
//           if (Pred == AFBlock) {
//             // Yes, reuse the incoming value here...
//             PN->addIncoming(PN, Pred);
//           } else {
//             // No, we must insert a new computation into this block and add it
//             // to the definitions list...
//             assert(NonPHIOccurrence && "No non-phi occurrences seen so far???");
//             Instruction *New = NonPHIOccurrence->clone();
//             New->setName(NonPHIOccurrence->getName() + ".PgoPre-inserted");
//             ProcessedExpressions.insert(New);

//             DEBUG(std::cerr << "  INSERTING OCCURRRENCE: " << *New);

//             // Insert it into the bottom of the predecessor, right before the
//             // terminator instruction...
//             Pred->getInstList().insert(Pred->getTerminator(), New);

//             // Make this block be the available definition for any blocks it
//             // dominates.  The ONLY case that this can affect more than just the
//             // block itself is when we are moving a computation to a loop
//             // header.  In all other cases, because we don't have critical
//             // edges, the node is guaranteed to only dominate itself.
//             //
//             ReplaceDominatedAvailableOccurrencesWith(New, DT->getNode(Pred));

//             // Add it as an incoming value on this edge to the PHI node
//             PN->addIncoming(New, Pred);
//             NonPHIOccurrence = New;
//             NumInserted++;
//           }
//         }
//       }

//       // Find out if there is already an available value in this block.  If so,
//       // we need to replace the available value with the PHI node.  This can
//       // only happen when we just inserted a PHI node on a backedge.
//       //
//       AvailableBlocksTy::iterator LBBlockAvailableValIt =
//         AvailableBlocks.find(AFBlock);
//       if (LBBlockAvailableValIt != AvailableBlocks.end()) {
//         if (LBBlockAvailableValIt->second->getParent() == AFBlock) {
//           Instruction *OldVal = LBBlockAvailableValIt->second;
//           OldVal->replaceAllUsesWith(PN);        // Use the new PHI node now
//           ++NumRedundant;
//           DEBUG(std::cerr << "  PHI replaces available value: %"
//                 << OldVal->getName() << "\n");
          
//           // Loop over all of the blocks dominated by this PHI node, and change
//           // the AvailableBlocks entries to be the PHI node instead of the old
//           // instruction.
//           MarkOccurrenceAvailableInAllDominatedBlocks(PN, AFBlock);
          
//           AFBlock->getInstList().erase(OldVal);  // Delete old instruction!

//           // The resultant PHI node is a new definition of the value!
//           Definitions.insert(std::make_pair(AFBlockID, PN));
//         } else {
//           // If the value is not defined in this block, that means that an
//           // inserted occurrence in a predecessor is now the live value for the
//           // region (occurs when hoisting loop invariants, f.e.).  In this case,
//           // the PHI node should actually just be removed.
//           assert(PN->use_empty() && "No uses should exist for dead PHI node!");
//           PN->getParent()->getInstList().erase(PN);            
//         }
//       } else {
//         // The resultant PHI node is a new definition of the value!
//         Definitions.insert(std::make_pair(AFBlockID, PN));
//       }
//     }
//   }

//   AvailableBlocks.clear();

//   return Changed;

  return false;
}
