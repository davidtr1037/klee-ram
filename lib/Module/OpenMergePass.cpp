#include "Passes.h"
#include "klee/Config/Version.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/TypeBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "llvm/Support/raw_ostream.h"


using namespace klee;
using namespace llvm;

char OpenMergePass::ID = 0;

bool OpenMergePass::doInitialization(Module &M) {
      Constant* c  = M.getOrInsertFunction("klee_open_merge", 
                                           TypeBuilder<void(...), false>::get(M.getContext())
                                          );
      kleeOpenMerge = dyn_cast<Function>(c);

      c  = M.getOrInsertFunction("klee_close_merge", 
                                  TypeBuilder<void(...), false>::get(M.getContext())
                                 );

      kleeCloseMerge = dyn_cast<Function>(c);
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
           CallInst::Create(kleeOpenMerge)->insertBefore(&*inst);
           CallInst::Create(kleeCloseMerge)->insertAfter(&*inst);
           ret = true;
    //       outs() << "load\n";
         }
      }
    }

    return ret;
}
char RemoveReallocPass::ID = 0;
bool RemoveReallocPass::doInitialization(Module &M) {
      //mallocFun = M.getFunction("malloc");
      Constant* c1 = M.getOrInsertFunction("malloc",
                                        TypeBuilder<void*(size_t), false>::get(M.getContext()));
      mallocFun = dyn_cast<Function>(c1);
      assert(mallocFun);
      Constant* c  = M.getOrInsertFunction("memcpy", 
                                           TypeBuilder<void*(void*, void*, uint64_t), 
                                           false>::get(M.getContext()));
     memcpyFun = dyn_cast<Function>(c);

//     memcpyFun = M.getFunction("memcpy");
      assert(memcpyFun);
      return false;
}


bool RemoveReallocPass::runOnFunction(Function &F) {
    bool ret = false;
    for(auto BB = F.begin(); BB != F.end(); BB++)
    for(auto inst = BB->begin(); inst != BB->end(); ++inst) {
        if(CallInst* callInst = dyn_cast<CallInst>(inst)) {
            if(callInst->getCalledFunction() && callInst->getCalledFunction()->getName() == "realloc") {
              inst++;
//              BB++;
              Instruction* isReallocNull = new ICmpInst(&*inst, CmpInst::Predicate::ICMP_EQ ,callInst, ConstantPointerNull::get(dyn_cast<PointerType>(callInst->getCalledFunction()->getReturnType())));

              TerminatorInst *ThenTerm = SplitBlockAndInsertIfThen(isReallocNull, &*inst, false);

              Value* size = callInst->getArgOperand(1);
              Value* address = callInst->getOperand(0);
//Things can go horribly wrong here, if it's not doing reallocation iwthin the same memory pool 

              std::vector<Value*> mallocArgs;
              mallocArgs.push_back(size);
              CallInst* mallocedAddr = CallInst::Create(mallocFun,mallocArgs, "", ThenTerm);
              std::vector<Value*> gepIdx;
              gepIdx.push_back(ConstantInt::getSigned(IntegerType::get(F.getContext(), 8), -1));
              GetElementPtrInst * gep = GetElementPtrInst::Create(
                  Type::getInt32Ty(F.getContext()),
//                  memcpyFun->getFunctionType()->getParamType(2),
//Change this for padding!!!
                  CastInst::CreatePointerCast(address,Type::getInt32PtrTy(F.getContext()), "ptrcast", ThenTerm),
                  gepIdx,
                  "gep", ThenTerm );
              LoadInst* storedSize = new LoadInst(gep, "storedSize", ThenTerm);

              std::vector<Value*> memcpyArg;
              memcpyArg.push_back(mallocedAddr);
              memcpyArg.push_back(address);
              //memcpyArg.push_back(storedSize); //assumes realloc increases size
              memcpyArg.push_back(CastInst::CreateZExtOrBitCast(storedSize,
                                                   memcpyFun->getFunctionType()->getParamType(2),
                                                   "", ThenTerm)); //assumes realloc increases size
//              memcpyArg.push_back(ConstantInt::get(memcpyFun->getFunctionType()->getParamType(2), 8));
//              memcpyArg.push_back(CastInst::CreateZExtOrBitCast(size, 
//                                                   memcpyFun->getFunctionType()->getParamType(2),
//                                                   "", callInst));
              CallInst* memcpyAddr = CallInst::Create(memcpyFun,memcpyArg, "", ThenTerm);
              CallInst::CreateFree(address,ThenTerm);

              PHINode *phi = PHINode::Create(mallocedAddr->getType(), 2, "phi", &*inst);
              callInst->replaceUsesOutsideBlock(phi, &*BB);

              phi->addIncoming(mallocedAddr, mallocedAddr->getParent());
              phi->addIncoming(callInst, callInst->getParent());

//              memcpyAddr->takeName(callInst);
              //inst++;//means if there are two call realloc instructions, this won't work
//              callInst->eraseFromParent();
              ret = true;
              return true;
            }
        }
    }
    return ret;
}
