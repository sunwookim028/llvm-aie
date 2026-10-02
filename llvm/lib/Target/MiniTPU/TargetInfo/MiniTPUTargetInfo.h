//===-- MiniTPUTargetInfo.h - MiniTPU target registration -----*- C++ -*-===//
#ifndef LLVM_LIB_TARGET_MINITPU_TARGETINFO_MINITPUTARGETINFO_H
#define LLVM_LIB_TARGET_MINITPU_TARGETINFO_MINITPUTARGETINFO_H
namespace llvm {
class Target;
Target &getTheMiniTPUTarget();
} // namespace llvm
#endif
