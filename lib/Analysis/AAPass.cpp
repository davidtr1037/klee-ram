#include <MemoryModel/PointerAnalysis.h>
#include <MemoryModel/MemModel.h>
#include <WPA/Andersen.h>
#include <WPA/FlowSensitive.h>
#include "llvm/ADT/SparseBitVector.h"

#include "klee/Internal/Analysis/AAPass.h"

using namespace llvm;

char SVFAAPass::ID = 0;

static RegisterPass<SVFAAPass> WHOLEPROGRAMPA("SVFAAPass",
        "Whole Program Pointer Analysis Pass");

SVFAAPass::~SVFAAPass() {
    delete _pta;
}

bool SVFAAPass::isModelingConstants() {
    return modelConstantsIndividually;
}

bool SVFAAPass::runOnModule(llvm::Module& module) {
    runPointerAnalysis(module, type);
    return false;
}

void SVFAAPass::runPointerAnalysis(llvm::Module& module, u32_t kind) {
    switch (kind) {
    case PointerAnalysis::Andersen_WPA:
        _pta = new Andersen();
        break;
    case PointerAnalysis::AndersenLCD_WPA:
        _pta = new AndersenLCD();
        break;
    case PointerAnalysis::AndersenWave_WPA:
        _pta = new AndersenWave();
        break;
    case PointerAnalysis::AndersenWaveDiff_WPA:
        _pta = new AndersenWaveDiff();
        break;
    case PointerAnalysis::AndersenWaveDiffWithType_WPA:
        _pta = new AndersenWaveDiffWithType();
         break;
    case PointerAnalysis::FSSPARSE_WPA:
        _pta = new FlowSensitive();
        break;
    default:
        llvm::errs() << "This pointer analysis has not been implemented yet.\n";
        break;
    }


//    _pta->quiet = true;
//    _pta->modelConstants = modelConstantsIndividually;

    //need to keep this around becauase otherwise module will be deleted
    auto svfModule = new SVFModule(&module);
    _pta->analyze(*svfModule);

    PAG* pag = _pta->getPAG();
    PointsTo memObjects;
    for(auto& idToType : *pag) {
        if(isa<ObjPN>(idToType.second) && !pag->getObject(idToType.first)->isFunction()  )
            memObjects.set(idToType.first);
    }
    //errs() << "All mem objects: ";
    //llvm::dump(memObjects, errs());

    for(auto& idToType : *pag) {
        if(ObjPN* opn = dyn_cast<ObjPN>(idToType.second)) {
          unsigned nodeId = idToType.first;
          PointsTo& ptsToOrIsPointedTo = _pta->getPts(nodeId); 
//          ptsToOrIsPointedTo |= _pta->getRevPts(nodeId);
//          ptsToOrIsPointedTo &= memObjects;
          if(!ptsToOrIsPointedTo.empty()) {
//            errs() << "Mem object " << nodeId << " " << opn->getValueName() << " same group as ";
//            ptsToOrIsPointedTo.set(nodeId);
            auto foundElem = std::find_if(disjointObjects.begin(), disjointObjects.end(),
              [&ptsToOrIsPointedTo](const PointsTo& e)  
                  {return e.intersects(ptsToOrIsPointedTo);});
            if( foundElem == disjointObjects.end()) {
                disjointObjects.push_front(ptsToOrIsPointedTo);
            } else {
                *foundElem |= ptsToOrIsPointedTo;
            }
          }
        }
    }
    
    //make all uniqueIds;
    for(auto dob = disjointObjects.begin();  dob != disjointObjects.end(); dob++) {
      for(auto nid : *dob) {
          if(pag->getObject(nid) != nullptr) {
              dob->set(pag->getObject(nid)->getSymId());
          }
      }
    }
    int changes = 0;
    do {
      changes = 0;
      for(auto it = disjointObjects.begin(); it != disjointObjects.end(); it++) {
            PointsTo &dob = *it;
            for(auto it1  = disjointObjects.begin(); it1 != disjointObjects.end(); it1++) {
                if(it != it1 && it->intersects(*it1)) {
                    dob |= *it1;
                    it1 = disjointObjects.erase(it1);
                    changes++;
                }
            }
            if(changes > 0) break;
      }
      errs() << "Completed loop with " << changes << " changes\n";
    } while(changes > 0);

    //don't need this if I don't filter by memory objects
    //for(auto& dob : disjointObjects) {
    //    memObjects = memObjects - dob;
    //}
    //for(auto nid: memObjects) {
    //    PointsTo *pt = new PointsTo();
    //    pt->set(nid);
    //    disjointObjects.push_front(*pt);
    //}
//#define PRINT_OBJS
    auto numObjs = 0;
    for(auto& dob : disjointObjects) {
      llvm::dump(dob, errs());
      numObjs += dob.count();
#ifdef PRINT_OBJS      
      for(auto nid : dob) {
          if(nid == 0 || pag->getObject(nid) == nullptr ||  pag->getObject(nid)->getRefVal() == nullptr) continue;
        errs() << nid << " -> "  << pag->getObject(nid)->getRefVal() << " tainted: " << pag->getObject(nid)->getSymId() << " is FIObjPN: " << isa<FIObjPN>(pag->getPAGNode(nid)) << " is: ";
        if(!isa<Function>(pag->getObject(nid)->getRefVal()))
        pag->getObject(nid)->getRefVal()->dump();
        else errs() << "\n";
      }
#endif
    }
    errs() << "Number of dijoint objects: " << disjointObjects.size() << "\n";
    errs() << "Number of objects: " << numObjs << "\n";


}

int SVFAAPass::getMaxGroupedObjects() {
    return disjointObjects.size();
}
void SVFAAPass::printsPtsTo(const llvm::Value* V) {
  assert(V && "Can't print null ptrs");
  PAG* pag = _pta->getPAG();
  NodeID node = pag->getValueNode(V);
  PointsTo& ptsTo = _pta->getPts(node);
//  ptsTo |= _pta->getRevPts(node);
  for(auto nid: ptsTo) {
    if(nid == 0 || pag->getObject(nid) == nullptr ||  pag->getObject(nid)->getRefVal() == nullptr) continue;
    errs() << "node: " << nid  << " " << pag->getObject(nid)->getRefVal() << " -> ";
    pag->getObject(nid)->getRefVal()->dump();
  }
  errs() << "node: " << node << " -> ";
  llvm::dump(ptsTo, errs());
}
int SVFAAPass::isNotAllone(const llvm::Value* V, klee::ExecutionState& state) {
    if(V == nullptr) return 0;
    PAG* pag = _pta->getPAG();
    if(!pag->hasValueNode(V)) return 0;
    NodeID node = pag->getValueNode(V);
    PointsTo& ptsTo = _pta->getPts(node);
//    ptsTo.set(node);
    int resultingGroup = 1;
    //what if it itnerescts with more than 1? can it?
//  errs() << "not alone node: " << node << " -> ";
//  llvm::dump(ptsTo, errs());
    for(auto& pts : disjointObjects) {
        assert(pts.contains(ptsTo) == pts.intersects(ptsTo) && "There shouldn't be more elements in ptsTo");
        if(pts.contains(ptsTo)) return resultingGroup;
        resultingGroup++;
        
    }

    return 0;
}
//llvm::AliasResult SVFAAPass::alias(const Value* V1, const Value* V2) {
//    llvm::AliasAnalysis::AliasResult result = MayAlias;
//
//    PAG* pag = _pta->getPAG();
//    if (pag->hasValueNode(V1) && pag->hasValueNode(V2)) {
//        result = _pta->alias(V1, V2);
//    }
//
//    return result;
//
//}
std::string kindTostring(GenericNode<PAGNode, PAGEdge>::GNodeK kind) {
  switch(kind) {
    case PAGNode::PNODEK::ValNode: return "ValNode";
    case PAGNode::PNODEK::ObjNode: return "ObjNode";
    case PAGNode::PNODEK::RetNode: return "RetNode";
    case PAGNode::PNODEK::VarargNode: return "VarargNode";
    case PAGNode::PNODEK::GepValNode: return "GepValNode";
    case PAGNode::PNODEK::GepObjNode: return "GepObjNode";
    case PAGNode::PNODEK::FIObjNode: return "FIOBJNode";
    case PAGNode::PNODEK::DummyValNode: return "DummyValNode";
    case PAGNode::PNODEK::DummyObjNode: return "DummyObjNode";
    default: return " UNKOWN!!! ";
  }
}
