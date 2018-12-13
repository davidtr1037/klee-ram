//===-- KInstruction.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Internal/Module/KInstruction.h"
#include <string>

using namespace llvm;
using namespace klee;

/***/

KInstruction::~KInstruction() {
  delete[] operands;
}

std::string KInstruction::getSourceLocation() const {
  if (!info->file.empty())
    return info->file + ":" + std::to_string(info->line);
  else return "[no debug info]";
}
void KInstruction::printFileLine(llvm::raw_ostream &debugFile) const {
  if (info->file != "")
    debugFile << info->file << ":" << info->line << ":" << info->assemblyLine;
  else debugFile << "[no debug info]";
}

std::string KInstruction::printFileLine() const {
  std::string str;
  llvm::raw_string_ostream oss(str);
  printFileLine(oss);
  return oss.str();
}
