#ifndef AAPASS_H
#define AAPASS_H

#include "MemoryModel/PointerAnalysis.h"
#include <llvm/Analysis/AliasAnalysis.h>
#include "llvm/Support/Casting.h"
#include <llvm/Pass.h>
#include "klee/ExecutionState.h"

#include <unordered_map>

//struct klee::KFunction;

class AAPass {
protected:
  enum PassType {
      Dummy,
      SVF,
      SVFDelete,
      PreRun,
      Manual
    };
private:
  const PassType Kind;
public:
  AAPass(PassType pt): Kind(pt) {}
  PassType getKind() const { return Kind; }
  virtual int getMaxGroupedObjects() = 0;
  virtual int isNotAllone(const llvm::Value* V, klee::ExecutionState& state) = 0;
  virtual void printsPtsTo(const llvm::Value* V) = 0;
  virtual bool isModelingConstants() = 0;
  virtual bool runOnModule(llvm::Module &module) = 0;
  virtual ~AAPass() {};
};

class DummyAAPass : public AAPass {
public:
  virtual int getMaxGroupedObjects() { return 1; }
  virtual int isNotAllone(const llvm::Value* V, klee::ExecutionState& state) { return 1;}
  virtual void printsPtsTo(const llvm::Value* V) {}
  virtual bool isModelingConstants() {return false;}
  virtual llvm::AliasResult alias(const llvm::Value *V1,
                                                 const llvm::Value *V2) {
      return llvm::AliasResult::MayAlias;
  }
  virtual bool runOnModule(llvm::Module &module) { return false;}
  static bool classof(const AAPass* aa) {return aa->getKind() == Dummy;}
  DummyAAPass(): AAPass(Dummy){}

};


class SVFAAPass : public llvm::ModulePass,  public AAPass {

public:
  static char ID;

  enum AliasCheckRule {
    Conservative, ///< return MayAlias if any pta says alias
    Veto,         ///< return NoAlias if any pta says no alias
    Precise       ///< return alias result by the most precise pta
  };

  SVFAAPass(): SVFAAPass(false) {}

  SVFAAPass(bool modelConstants)
      : llvm::ModulePass(ID), AAPass(SVF), modelConstantsIndividually(modelConstants),
        type(PointerAnalysis::Default_PTA), _pta(0) {}

  ~SVFAAPass();


  static bool classof(const AAPass* aa) {return aa->getKind() == SVF;}
  virtual inline void *getAdjustedAnalysisPointer(llvm::AnalysisID id) {
    return this;
  }

//  virtual inline llvm::AliasResult
//  alias(const llvm::MemoryLocation &LocA,
//        const llvm::MemoryLocation &LocB) {
//    return alias(LocA.Ptr, LocB.Ptr);
//  }
//
//  virtual llvm::AliasResult alias(const llvm::Value *V1,
//                                                 const llvm::Value *V2);
//
  virtual bool runOnModule(llvm::Module &module);

  virtual inline llvm::StringRef getPassName() const { return "AAPass"; }

  void setPAType(PointerAnalysis::PTATY type) { this->type = type; }

  BVDataPTAImpl *getPTA() { return _pta; }
  //void getPointsTo(const llvm::Value* V);
  int getMaxGroupedObjects();
  int isNotAllone(const llvm::Value* V, klee::ExecutionState&);
  void printsPtsTo(const llvm::Value* V);
  bool isModelingConstants();

  static bool isNoopInContext(klee::ExecutionState::allocCtx_ty *allocContext);

private:
  bool modelConstantsIndividually;
  void runPointerAnalysis(llvm::Module &module, u32_t kind);
  std::list<PointsTo> disjointObjects;

  PointerAnalysis::PTATY type;
  BVDataPTAImpl *_pta;
};

class SVFDeletingAAPass : public llvm::ModulePass,  public AAPass {

public:
  static char ID;

  SVFDeletingAAPass(): SVFDeletingAAPass(false) {}

  SVFDeletingAAPass(bool modelConstants)
      : llvm::ModulePass(ID), AAPass(SVF), modelConstantsIndividually(modelConstants),
        type(PointerAnalysis::Default_PTA), _pta(0) {}

  ~SVFDeletingAAPass() {}


  static bool classof(const AAPass* aa) {return aa->getKind() == SVFDelete;}
  virtual inline void *getAdjustedAnalysisPointer(llvm::AnalysisID id) {
    return this;
  }
  virtual bool runOnModule(llvm::Module &module);
  virtual inline llvm::StringRef getPassName() const { return "AAPass cleans up SVF"; }
  void setPAType(PointerAnalysis::PTATY type) { this->type = type; }

  int getMaxGroupedObjects();
  int isNotAllone(const llvm::Value* V, klee::ExecutionState&);
  void printsPtsTo(const llvm::Value* V);
  bool isModelingConstants();

  static bool isNoopInContext(klee::ExecutionState::allocCtx_ty *allocContext);

private:
  int valToGroup(const llvm::Value* V);
  bool modelConstantsIndividually;
  void runPointerAnalysis(llvm::Module &module, u32_t kind);
  std::list<PointsTo> disjointObjects;
  std::unordered_map<const llvm::Value*, int> valueToGroup;

  PointerAnalysis::PTATY type;
  BVDataPTAImpl *_pta;
};

class ManualAAPass : public AAPass {
public:
  ManualAAPass() : AAPass(Manual) {}
  static bool classof(const AAPass* aa) {return aa->getKind() == Manual;}

  int getMaxGroupedObjects() { return 10; }
  int isNotAllone(const llvm::Value* V, klee::ExecutionState& state) { 

      llvm::errs() << "returning " << state.memoryPool << "\n";
      if(state.memoryPool > 0)
          state.dumpStack(llvm::errs());

      return state.memoryPool; 
  }
  void printsPtsTo(const llvm::Value* V) {}
  bool isModelingConstants() { return false; }
  bool runOnModule(llvm::Module &module) { return false; }
};

class PreRunAAPass : public AAPass {
    int lastObject = 1;
    std::unordered_map<std::string, int> ctxToMp;
public:
  PreRunAAPass(std::string preRunRecordPath);
  //: AAPass(PreRun) {}
  static bool classof(const AAPass* aa) {return aa->getKind() == PreRun;}

  int getMaxGroupedObjects() {return lastObject; }
  int isNotAllone(const llvm::Value* V, klee::ExecutionState& state);

  void printsPtsTo(const llvm::Value* V) {}
  bool isModelingConstants() { return false; }
  bool runOnModule(llvm::Module &module) { return false; }
};
#endif /* AAPASS_H */
