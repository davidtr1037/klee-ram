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
                                           TypeBuilder<void(), false>::get(getGlobalContext()),
                                           AttributeSet().addAttribute(M.getContext(), 1U, Attribute::NoAlias) 
                                          );
      kleeOpenMerge = cast<Function>(c);

}

bool OpenMergePass::runOnFunction(Function& F) {
    std::vector<Value*> paramArrayRef;
    Instruction *call_open_merge =  CallInst::Create(kleeOpenMerge, paramArrayRef);
    F.getEntryBlock().getInstList().push_front(call_open_merge);

    outs() << "In function\n";
    return false;
}
