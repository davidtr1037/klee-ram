#ifndef AAPASS_H
#define AAPASS_H

#include "MemoryModel/PointerAnalysis.h"
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Pass.h>
class AAPass {
public:
  virtual int getMaxGroupedObjects() = 0;
  virtual int isNotAllone(const llvm::Value* V) = 0;
  virtual void printsPtsTo(const llvm::Value* V) = 0;
  virtual bool isModelingConstants() = 0;
  virtual bool runOnModule(llvm::Module &module) = 0;
  virtual ~AAPass() {};
};

class DummyAAPass : public AAPass {
public:
  virtual int getMaxGroupedObjects() { return 1; }
  virtual int isNotAllone(const llvm::Value* V) { return 1;}
  virtual void printsPtsTo(const llvm::Value* V) {}
  virtual bool isModelingConstants() {return false;}
  virtual llvm::AliasAnalysis::AliasResult alias(const llvm::Value *V1,
                                                 const llvm::Value *V2) {
      return llvm::AliasAnalysis::AliasResult::MayAlias;
  }
  virtual bool runOnModule(llvm::Module &module) { return false;}
  DummyAAPass(){}

};




class SVFAAPass : public llvm::ModulePass, public llvm::AliasAnalysis, public AAPass {

public:
  static char ID;

  enum AliasCheckRule {
    Conservative, ///< return MayAlias if any pta says alias
    Veto,         ///< return NoAlias if any pta says no alias
    Precise       ///< return alias result by the most precise pta
  };

  SVFAAPass(): SVFAAPass(false) {}

  SVFAAPass(bool modelConstants)
      : llvm::ModulePass(ID), llvm::AliasAnalysis(), modelConstantsIndividually(modelConstants),
        type(PointerAnalysis::Default_PTA), _pta(0) {}

  ~SVFAAPass();

  virtual inline void getAnalysisUsage(llvm::AnalysisUsage &au) const {
    au.setPreservesAll();
  }

  virtual inline void *getAdjustedAnalysisPointer(llvm::AnalysisID id) {
    return this;
  }

  virtual inline llvm::AliasAnalysis::AliasResult
  alias(const llvm::AliasAnalysis::Location &LocA,
        const llvm::AliasAnalysis::Location &LocB) {
    return alias(LocA.Ptr, LocB.Ptr);
  }

  virtual llvm::AliasAnalysis::AliasResult alias(const llvm::Value *V1,
                                                 const llvm::Value *V2);

  virtual bool runOnModule(llvm::Module &module);

  virtual inline const char *getPassName() const { return "AAPass"; }

  void setPAType(PointerAnalysis::PTATY type) { this->type = type; }

  BVDataPTAImpl *getPTA() { return _pta; }
  //void getPointsTo(const llvm::Value* V);
  int getMaxGroupedObjects();
  int isNotAllone(const llvm::Value* V);
  void printsPtsTo(const llvm::Value* V);
  bool isModelingConstants();

private:
  bool modelConstantsIndividually;
  void runPointerAnalysis(llvm::Module &module, u32_t kind);
  std::list<PointsTo> disjointObjects;

  PointerAnalysis::PTATY type;
  BVDataPTAImpl *_pta;
};

#endif /* AAPASS_H */
