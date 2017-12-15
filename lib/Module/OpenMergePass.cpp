#include "Passes.h"
#include "klee/Config/Version.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/TypeBuilder.h"

#include "llvm/Support/raw_ostream.h"


using namespace klee;
using namespace llvm;

char OpenMergePass::ID = 0;

bool OpenMergePass::doInitialization(Module &M) {
      Constant* c  = M.getOrInsertFunction("klee_open_merge", 
                                           TypeBuilder<void(), false>::get(getGlobalContext())
                                          );
      kleeOpenMerge = cast<Function>(c);

      c  = M.getOrInsertFunction("klee_close_merge", 
                                  TypeBuilder<void(), false>::get(getGlobalContext())
                                 );

      kleeCloseMerge = cast<Function>(c);
      return false;
}

bool OpenMergePass::isADoubleDereference(LoadInst* load) {
  if(!load) return false;

  if(GetElementPtrInst* getElemPtr = dyn_cast<GetElementPtrInst>(load->getPointerOperand())) {
    if(LoadInst* load1 = dyn_cast<LoadInst>(getElemPtr->getPointerOperand())) {
      Type* load1_type = load1->getPointerOperand()->getType();
      if(PointerType* type1 = dyn_cast<PointerType>(load1_type)) { 
         if(PointerType* type = dyn_cast<PointerType>(type1->getElementType())) {
            return !isa<PointerType>(type->getElementType());
         }
      }
    }
  }
  return false;
}

bool OpenMergePass::runOnFunction(Function& F) {
//    F.getEntryBlock().getInstList().push_front(call_open_merge);
    bool ret = false;
    for(auto bb = F.begin(), be = F.end(); bb != be; ++bb) {
      for(auto inst = bb->begin(), ie = bb->end(); inst!= ie; ++inst) {
         if(isADoubleDereference(dyn_cast<LoadInst>(inst))) {
           CallInst::Create(kleeOpenMerge)->insertBefore(inst);
           CallInst::Create(kleeCloseMerge)->insertAfter(inst);
           ret = true;
    //       outs() << "load\n";
         }
      }
    }

    return ret;
}
