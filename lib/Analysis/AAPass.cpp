#include <MemoryModel/PointerAnalysis.h>
#include <WPA/Andersen.h>
#include <WPA/FlowSensitive.h>
#include "llvm/ADT/SparseBitVector.h"

#include "klee/Internal/Analysis/AAPass.h"

using namespace llvm;

char AAPass::ID = 0;

static RegisterPass<AAPass> WHOLEPROGRAMPA("AAPass",
        "Whole Program Pointer Analysis Pass");

AAPass::~AAPass() {
    delete _pta;
}

bool AAPass::runOnModule(llvm::Module& module) {
    runPointerAnalysis(module, type);
    return false;
}

void AAPass::runPointerAnalysis(llvm::Module& module, u32_t kind) {
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
    case PointerAnalysis::FSSPARSE_WPA:
        _pta = new FlowSensitive();
        break;
    default:
        llvm::errs() << "This pointer analysis has not been implemented yet.\n";
        break;
    }

    _pta->analyze(module);

    PAG* pag = _pta->getPAG();
    PointsTo memObjects;
    for(auto& idToType : *pag) {
        if(isa<ObjPN>(idToType.second))
            memObjects.set(idToType.first);
    }
    errs() << "All mem objects: ";
    llvm::dump(memObjects, errs());

    for(auto& idToType : *pag) {
        if(ObjPN* opn = dyn_cast<ObjPN>(idToType.second)) {
          unsigned nodeId = idToType.first;
          PointsTo& ptsToOrIsPointedTo = _pta->getPts(nodeId); 
          ptsToOrIsPointedTo |= _pta->getRevPts(nodeId);
          ptsToOrIsPointedTo &= memObjects;
          if(!ptsToOrIsPointedTo.empty()) {
//            errs() << "Mem object " << nodeId << " " << opn->getValueName() << " same group as ";
            ptsToOrIsPointedTo.set(nodeId);
            auto foundElem = std::find_if(disjointObjects.begin(), disjointObjects.end(),
              [&ptsToOrIsPointedTo](const PointsTo& e)  
                  {return e.contains(ptsToOrIsPointedTo) || ptsToOrIsPointedTo.contains(e);});
            if( foundElem == disjointObjects.end()) {
                disjointObjects.push_front(ptsToOrIsPointedTo);
            } else {
                *foundElem |= ptsToOrIsPointedTo;
            }
          }
        }
    }

    for(auto& dob : disjointObjects)
    llvm::dump(dob, errs());

}

int AAPass::getMaxGroupedObjects() {
    return disjointObjects.size();
}
int AAPass::isNotAllone(const llvm::Value* V) {
    PAG* pag = _pta->getPAG();
    NodeID node = pag->getValueNode(V);
    PointsTo& ptsTo = _pta->getPts(node);
    int resultingGroup = 1;
    for(auto& pts : disjointObjects) {
        if(pts.contains(ptsTo)) return resultingGroup;
        resultingGroup++;
        
    }

    return 0;
}
llvm::AliasAnalysis::AliasResult AAPass::alias(const Value* V1, const Value* V2) {
    llvm::AliasAnalysis::AliasResult result = MayAlias;

    PAG* pag = _pta->getPAG();
    if (pag->hasValueNode(V1) && pag->hasValueNode(V2)) {
        result = _pta->alias(V1, V2);
    }

    return result;

}
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
/*
void AAPass::getPointsTo(const Value* V) {
    return ;
    PAG* pag = _pta->getPAG();
    NodeID node = pag->getValueNode(V);
    PointsTo& ptsTo = _pta->getPts(node);
     errs() << "Print all memobj \n";
    for(auto& idToType : *pag) {
        if(ObjPN* opn = dyn_cast<ObjPN>(idToType.second)) {

//            errs() << "Node: " << idToType.first << " is a " << kindTostring(opn->getNodeKind())  << " of type " << opn->getMemObj()->isHeap() << "\n";
        }
    }
    for(auto& idToType : *pag) {
        if((idToType.second->getNodeKind() == PAGNode::PNODEK::DummyObjNode ||
             idToType.second->getNodeKind() == PAGNode::PNODEK::GepObjNode ||
             idToType.second->getNodeKind() == PAGNode::PNODEK::FIObjNode)
 && (idToType.second->hasValue() &&  _pta->alias(V, idToType.second->getValue()))
            )
//       if(idToType.second->getNodeKind() != PAGNode::PNODEK::ValNode)
        errs() << idToType.first << " type: " <<  kindTostring(idToType.second->getNodeKind()) 
        << " alias: " << (idToType.second->hasValue() ?  _pta->alias(V, idToType.second->getValue()) : 5434) << "\n";
    }
    //_pta->dumpPts(node, ptsTo);
//    CPtSet& cpt = ((Andersen*)_pta)->getCondPointsTo(node);
    errs() << "nodeID : " << node << " size : " << ptsTo.count() << " points to " << ptsTo.find_first() << " which poitns to\n" ;//cpts size: " << cpt.size() << " ";
    llvm::dump(_pta->getPts(ptsTo.find_first()),errs()); 
    llvm::dump(_pta->getRevPts(ptsTo.find_first()),errs()); //is pointed to


}
*/
