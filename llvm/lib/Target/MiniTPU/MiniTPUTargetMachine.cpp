//===-- MiniTPUTargetMachine.cpp - MiniTPU entry point (MC-only spike) ----===//
//
// Step 1 builds the MC layer only. InitializeAllTargets() still calls this
// entry point, so it exists and registers nothing: there is no TargetMachine,
// instruction selection, register allocation or scheduling yet.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Compiler.h"

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMiniTPUTarget() {}
