//===-- MiniTPUTargetInfo.cpp - MiniTPU target registration --------------===//
#include "TargetInfo/MiniTPUTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
using namespace llvm;

Target &llvm::getTheMiniTPUTarget() {
  static Target TheMiniTPUTarget;
  return TheMiniTPUTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMiniTPUTargetInfo() {
  RegisterTarget<Triple::minitpu> X(getTheMiniTPUTarget(), "minitpu",
                                    "MiniTPU 128-bit VLIW tensor unit", "MiniTPU");
}
