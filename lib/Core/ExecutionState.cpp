//===-- ExecutionState.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Memory.h"

#include "klee/ExecutionState.h"

#include "klee/Expr.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/OptionCategories.h"
#include "klee/util/ExprUtil.h"

#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdarg.h>

using namespace llvm;
using namespace klee;

namespace {
cl::opt<bool> DebugLogStateMerge(
    "debug-log-state-merge", cl::init(false),
    cl::desc("Debug information for underlying state merging (default=false)"),
    cl::cat(MergeCat));
}

cl::opt<bool> klee::UseLocalSymAddr("use-local-sym-addr", cl::init(false), cl::desc("..."));

/***/

StackFrame::StackFrame(KInstIterator _caller, KFunction *_kf)
  : caller(_caller), kf(_kf), callPathNode(0), 
    minDistToUncoveredOnReturn(0), varargs(0) {
  locals = new Cell[kf->numRegisters];
}

StackFrame::StackFrame(const StackFrame &s) 
  : caller(s.caller),
    kf(s.kf),
    callPathNode(s.callPathNode),
    allocas(s.allocas),
    minDistToUncoveredOnReturn(s.minDistToUncoveredOnReturn),
    varargs(s.varargs) {
  locals = new Cell[s.kf->numRegisters];
  for (unsigned i=0; i<s.kf->numRegisters; i++)
    locals[i] = s.locals[i];
}

StackFrame::~StackFrame() { 
  delete[] locals; 
}

/***/

ExecutionState::ExecutionState(KFunction *kf) :
    pc(kf->instructions),
    prevPC(pc),

    weight(1),
    depth(0),

    instsSinceCovNew(0),
    coveredNew(false),
    forkDisabled(false),
    ptreeNode(0),
    steppedInstructions(0){
  pushFrame(0, kf);
}

/* TODO: add rewritten constraints? */
ExecutionState::ExecutionState(const std::vector<ref<Expr> > &assumptions)
    : constraints(assumptions), ptreeNode(0) {}

ExecutionState::~ExecutionState() {
  for (unsigned int i=0; i<symbolics.size(); i++)
  {
    const MemoryObject *mo = symbolics[i].first;
    assert(mo->refCount > 0);
    mo->refCount--;
    if (mo->refCount == 0)
      delete mo;
  }

  for (auto cur_mergehandler: openMergeStack){
    cur_mergehandler->removeOpenState(this);
  }


  while (!stack.empty()) popFrame();
}

ExecutionState::ExecutionState(const ExecutionState& state):
    fnAliases(state.fnAliases),
    addressConstraints(state.addressConstraints),
    cache(state.cache),
    pc(state.pc),
    prevPC(state.prevPC),
    stack(state.stack),
    incomingBBIndex(state.incomingBBIndex),

    addressSpace(state.addressSpace),
    constraints(state.constraints),

    queryCost(state.queryCost),
    weight(state.weight),
    depth(state.depth),

    pathOS(state.pathOS),
    symPathOS(state.symPathOS),

    instsSinceCovNew(state.instsSinceCovNew),
    coveredNew(state.coveredNew),
    forkDisabled(state.forkDisabled),
    coveredLines(state.coveredLines),
    ptreeNode(state.ptreeNode),
    symbolics(state.symbolics),
    arrayNames(state.arrayNames),
    openMergeStack(state.openMergeStack),
    steppedInstructions(state.steppedInstructions),
    rewrittenConstraints(state.rewrittenConstraints)
{
  for (unsigned int i=0; i<symbolics.size(); i++)
    symbolics[i].first->refCount++;

  for (auto cur_mergehandler: openMergeStack)
    cur_mergehandler->addOpenState(this);
}

ExecutionState *ExecutionState::branch() {
  depth++;

  ExecutionState *falseState = new ExecutionState(*this);
  falseState->coveredNew = false;
  falseState->coveredLines.clear();

  weight *= .5;
  falseState->weight -= weight;

  return falseState;
}

void ExecutionState::pushFrame(KInstIterator caller, KFunction *kf) {
  stack.push_back(StackFrame(caller,kf));
}

void ExecutionState::popFrame() {
  StackFrame &sf = stack.back();
  for (const MemoryObject* mo : sf.allocas) {
    if (UseLocalSymAddr) {
      /* TODO: avoid lookup... */
      const ObjectState *os = addressSpace.findObject(mo);
      for (const SubObject &o : os->getSubObjects()) {
        removeAddressConstraint(o.info.arrayID);
      }
    }
    addressSpace.unbindObject(mo);
  }
  stack.pop_back();
}

void ExecutionState::addSymbolic(const MemoryObject *mo, const Array *array) { 
  mo->refCount++;
  symbolics.push_back(std::make_pair(mo, array));
}

void ExecutionState::addConstraint(ref<Expr> e) {
  constraints.addConstraint(e);
  if (!constraints.mayHaveAddressConstraints() && !e->flag) {
    /* both PC and expression are free of address constraints... */
    /* TODO: something better than copy? */
    rewrittenConstraints = constraints;
  } else {
    ref<Expr> rewritten = addressSpace.unfold(*this, e);
    rewrittenConstraints.addConstraint(rewritten);
  }
}
///

std::string ExecutionState::getFnAlias(std::string fn) {
  std::map < std::string, std::string >::iterator it = fnAliases.find(fn);
  if (it != fnAliases.end())
    return it->second;
  else return "";
}

void ExecutionState::addFnAlias(std::string old_fn, std::string new_fn) {
  fnAliases[old_fn] = new_fn;
}

void ExecutionState::removeFnAlias(std::string fn) {
  fnAliases.erase(fn);
}

/**/

llvm::raw_ostream &klee::operator<<(llvm::raw_ostream &os, const MemoryMap &mm) {
  os << "{";
  MemoryMap::iterator it = mm.begin();
  MemoryMap::iterator ie = mm.end();
  if (it!=ie) {
    os << "MO" << it->first->id << ":" << it->second;
    for (++it; it!=ie; ++it)
      os << ", MO" << it->first->id << ":" << it->second;
  }
  os << "}";
  return os;
}

bool ExecutionState::merge(const ExecutionState &b) {
  if (DebugLogStateMerge)
    llvm::errs() << "-- attempting merge of A:" << this << " with B:" << &b
                 << "--\n";
  if (pc != b.pc)
    return false;

  // XXX is it even possible for these to differ? does it matter? probably
  // implies difference in object states?
  if (symbolics!=b.symbolics)
    return false;

  {
    std::vector<StackFrame>::const_iterator itA = stack.begin();
    std::vector<StackFrame>::const_iterator itB = b.stack.begin();
    while (itA!=stack.end() && itB!=b.stack.end()) {
      // XXX vaargs?
      if (itA->caller!=itB->caller || itA->kf!=itB->kf)
        return false;
      ++itA;
      ++itB;
    }
    if (itA!=stack.end() || itB!=b.stack.end())
      return false;
  }

  std::set< ref<Expr> > aConstraints(constraints.begin(), constraints.end());
  std::set< ref<Expr> > bConstraints(b.constraints.begin(), 
                                     b.constraints.end());
  std::set< ref<Expr> > commonConstraints, aSuffix, bSuffix;
  std::set_intersection(aConstraints.begin(), aConstraints.end(),
                        bConstraints.begin(), bConstraints.end(),
                        std::inserter(commonConstraints, commonConstraints.begin()));
  std::set_difference(aConstraints.begin(), aConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(aSuffix, aSuffix.end()));
  std::set_difference(bConstraints.begin(), bConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(bSuffix, bSuffix.end()));
  if (DebugLogStateMerge) {
    llvm::errs() << "\tconstraint prefix: [";
    for (std::set<ref<Expr> >::iterator it = commonConstraints.begin(),
                                        ie = commonConstraints.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
    llvm::errs() << "\tA suffix: [";
    for (std::set<ref<Expr> >::iterator it = aSuffix.begin(),
                                        ie = aSuffix.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
    llvm::errs() << "\tB suffix: [";
    for (std::set<ref<Expr> >::iterator it = bSuffix.begin(),
                                        ie = bSuffix.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
  }

  // We cannot merge if addresses would resolve differently in the
  // states. This means:
  // 
  // 1. Any objects created since the branch in either object must
  // have been free'd.
  //
  // 2. We cannot have free'd any pre-existing object in one state
  // and not the other

  if (DebugLogStateMerge) {
    llvm::errs() << "\tchecking object states\n";
    llvm::errs() << "A: " << addressSpace.objects << "\n";
    llvm::errs() << "B: " << b.addressSpace.objects << "\n";
  }
    
  std::set<const MemoryObject*> mutated;
  MemoryMap::iterator ai = addressSpace.objects.begin();
  MemoryMap::iterator bi = b.addressSpace.objects.begin();
  MemoryMap::iterator ae = addressSpace.objects.end();
  MemoryMap::iterator be = b.addressSpace.objects.end();
  for (; ai!=ae && bi!=be; ++ai, ++bi) {
    if (ai->first != bi->first) {
      if (DebugLogStateMerge) {
        if (ai->first < bi->first) {
          llvm::errs() << "\t\tB misses binding for: " << ai->first->id << "\n";
        } else {
          llvm::errs() << "\t\tA misses binding for: " << bi->first->id << "\n";
        }
      }
      return false;
    }
    if (ai->second != bi->second) {
      if (DebugLogStateMerge)
        llvm::errs() << "\t\tmutated: " << ai->first->id << "\n";
      mutated.insert(ai->first);
    }
  }
  if (ai!=ae || bi!=be) {
    if (DebugLogStateMerge)
      llvm::errs() << "\t\tmappings differ\n";
    return false;
  }
  
  // merge stack

  ref<Expr> inA = ConstantExpr::alloc(1, Expr::Bool);
  ref<Expr> inB = ConstantExpr::alloc(1, Expr::Bool);
  for (std::set< ref<Expr> >::iterator it = aSuffix.begin(), 
         ie = aSuffix.end(); it != ie; ++it)
    inA = AndExpr::create(inA, *it);
  for (std::set< ref<Expr> >::iterator it = bSuffix.begin(), 
         ie = bSuffix.end(); it != ie; ++it)
    inB = AndExpr::create(inB, *it);

  // XXX should we have a preference as to which predicate to use?
  // it seems like it can make a difference, even though logically
  // they must contradict each other and so inA => !inB

  std::vector<StackFrame>::iterator itA = stack.begin();
  std::vector<StackFrame>::const_iterator itB = b.stack.begin();
  for (; itA!=stack.end(); ++itA, ++itB) {
    StackFrame &af = *itA;
    const StackFrame &bf = *itB;
    for (unsigned i=0; i<af.kf->numRegisters; i++) {
      ref<Expr> &av = af.locals[i].value;
      const ref<Expr> &bv = bf.locals[i].value;
      if (av.isNull() || bv.isNull()) {
        // if one is null then by implication (we are at same pc)
        // we cannot reuse this local, so just ignore
      } else {
        av = SelectExpr::create(inA, av, bv);
      }
    }
  }

  for (std::set<const MemoryObject*>::iterator it = mutated.begin(), 
         ie = mutated.end(); it != ie; ++it) {
    const MemoryObject *mo = *it;
    const ObjectState *os = addressSpace.findObject(mo);
    const ObjectState *otherOS = b.addressSpace.findObject(mo);
    assert(os && !os->readOnly && 
           "objects mutated but not writable in merging state");
    assert(otherOS);

    ObjectState *wos = addressSpace.getWriteable(mo, os);
    for (unsigned i=0; i<mo->size; i++) {
      ref<Expr> av = wos->read8(i);
      ref<Expr> bv = otherOS->read8(i);
      wos->write(i, SelectExpr::create(inA, av, bv));
    }
  }

  constraints = ConstraintManager();
  for (std::set< ref<Expr> >::iterator it = commonConstraints.begin(), 
         ie = commonConstraints.end(); it != ie; ++it)
    constraints.addConstraint(*it);
  constraints.addConstraint(OrExpr::create(inA, inB));

  return true;
}

void ExecutionState::dumpStack(llvm::raw_ostream &out) const {
  unsigned idx = 0;
  const KInstruction *target = prevPC;
  for (ExecutionState::stack_ty::const_reverse_iterator
         it = stack.rbegin(), ie = stack.rend();
       it != ie; ++it) {
    const StackFrame &sf = *it;
    Function *f = sf.kf->function;
    const InstructionInfo &ii = *target->info;
    out << "\t#" << idx++;
    std::stringstream AssStream;
    AssStream << std::setw(8) << std::setfill('0') << ii.assemblyLine;
    out << AssStream.str();
    out << " in " << f->getName().str() << " (";
    // Yawn, we could go up and print varargs if we wanted to.
    unsigned index = 0;
    for (Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
         ai != ae; ++ai) {
      if (ai!=f->arg_begin()) out << ", ";

      out << ai->getName().str();
      // XXX should go through function
      ref<Expr> value = sf.locals[sf.kf->getArgRegister(index++)].value;
      if (value.get() && isa<ConstantExpr>(value))
        out << "=" << value;
    }
    out << ")";
    if (ii.file != "")
      out << " at " << ii.file << ":" << ii.line;
    out << "\n";
    target = sf.caller;
  }
}

void ExecutionState::addAddressConstraint(uint64_t id,
                                          uint64_t address,
                                          ref<Expr> alpha) {
  ref<ConstantExpr> c = ConstantExpr::create(address, Context::get().getPointerWidth());
  ref<Expr> eq = EqExpr::create(c, alpha);

  std::vector<ref<ConstantExpr>> bytes;
  for (unsigned i = 0; i < 8; i++) {
    uint64_t value = (address >> (i * 8)) & 0xff;
    ref<ConstantExpr> e = ConstantExpr::create(value, Expr::Int8);
    bytes.push_back(e);
  }
  ref<AddressRecord> record = new AddressRecord(c, bytes, eq);

  addressConstraints[id] = record;
  cache[alpha->hash()] = record;
}

ref<AddressRecord> ExecutionState::getAddressConstraint(uint64_t id) const {
  auto i = addressConstraints.find(id);
  if (i == addressConstraints.end()) {
    assert(false);
  } else {
    return i->second;
  }
}

void ExecutionState::removeAddressConstraint(uint64_t id) {
  auto i = addressConstraints.find(id);
  if (i == addressConstraints.end()) {
    assert(false);
  }
  addressConstraints.erase(i);
}

ref<Expr> ExecutionState::build(ref<Expr> e) const {
  /* collect dependencies */
  AddressArrayCollector collector;
  collector.visit(e);

  ref<Expr> all = ConstantExpr::create(1, Expr::Bool);
  for (uint64_t id : collector.ids) {
    ref<AddressRecord> ar = getAddressConstraint(id);
    ref<Expr> eq = ar->constraint;
    all = AndExpr::create(all, eq);
  }

  return all;
}

ref<Expr> ExecutionState::build(std::vector<ref<Expr>> &es) const {
  std::set<uint64_t> all_arrays;

  for (ref<Expr> e : es) {
    /* collect dependencies */
    AddressArrayCollector collector;
    collector.visit(e);
    for (uint64_t id : collector.ids) {
      all_arrays.insert(id);
    }
  }

  ref<Expr> all = ConstantExpr::create(1, Expr::Bool);
  for (uint64_t id : all_arrays) {
    ref<AddressRecord> ar = getAddressConstraint(id);
    ref<Expr> eq = ar->constraint;
    all = AndExpr::create(all, eq);
  }

  return all;
}

void ExecutionState::dumpAddressConstraints() const {
  for (auto &i : addressConstraints) {
    ref<AddressRecord> ar = i.second;
    ar->constraint->dump();
  }
}

void ExecutionState::computeRewrittenConstraints() {
  rewrittenConstraints.clear();
  for (ref<Expr> e : constraints) {
    ref<Expr> rewritten = addressSpace.unfold(*this, e);
    rewrittenConstraints.addConstraint(rewritten);
  }
}

void ExecutionState::rewriteUL(const UpdateList &ul, UpdateList &result) const {
  for (const UpdateNode *un = ul.head; un != nullptr; un = un->next) {
    ref<Expr> index = addressSpace.unfold(*this, un->index);
    ref<Expr> value = addressSpace.unfold(*this, un->value);
    result.extend(index, value);
  }
}

ExprVisitor::Action AddressUnfolder::visitConcat(const ConcatExpr &e) {
  auto i = state.getCache().find(e.hash());
  if (i != state.getCache().end()) {
    return Action::changeTo(i->second->address);
  }

  return Action::doChildren();
}

ExprVisitor::Action AddressUnfolder::visitRead(const ReadExpr &e) {
  if (e.updates.root->isAddressArray) {
    ref<ConstantExpr> index = dyn_cast<ConstantExpr>(e.index);
    if (index.isNull()) {
      /* should not happen... */
      assert(false);
    }

    ref<AddressRecord> ar = state.getAddressConstraint(e.updates.root->id);
    return Action::changeTo(ar->bytes[index->getZExtValue()]);
  }

  if (e.updates.getSize() == 0) {
    return Action::doChildren();
  }

  /* TODO: we should keep the existing updates... */
  UpdateList updates(e.updates.root, nullptr);
  //UpdateList updates(e.updates);

  for (const UpdateNode *un = e.updates.head; un; un = un->next) {
    ref<ReadExpr> re = dyn_cast<ReadExpr>(un->value);
    if (re.isNull()) {
      continue;
    }

    if (!re->updates.root->isAddressArray) {
      continue;
    }

    ref<ConstantExpr> index = dyn_cast<ConstantExpr>(re->index);
    assert(!index.isNull());
    ref<AddressRecord> ar = state.getAddressConstraint(re->updates.root->id);
    uint64_t i = index->getZExtValue();
    updates.extend(un->index, ar->bytes[i]);
  }

  if (updates.getSize() != 0) {
    /* TODO: do some caching? */
    return Action::changeTo(ReadExpr::create(updates, e.index));
  }

  return Action::doChildren();
}
