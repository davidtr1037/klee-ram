#ifndef AAPASS_H
#define AAPASS_H

#include "MemoryModel/PointerAnalysis.h"
#include <llvm/Analysis/AliasAnalysis.h>
#include "llvm/Support/Casting.h"
#include <llvm/Pass.h>
#include "klee/ExecutionState.h"

class AAPass {
protected:
  enum PassType {
      Dummy,
      SVF,
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

private:
  bool modelConstantsIndividually;
  void runPointerAnalysis(llvm::Module &module, u32_t kind);
  std::list<PointsTo> disjointObjects;

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
#endif /* AAPASS_H */
