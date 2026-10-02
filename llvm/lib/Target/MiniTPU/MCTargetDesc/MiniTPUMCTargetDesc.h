//===-- MiniTPUMCTargetDesc.h - MiniTPU target descriptions ---*- C++ -*-===//
#ifndef LLVM_LIB_TARGET_MINITPU_MCTARGETDESC_MINITPUMCTARGETDESC_H
#define LLVM_LIB_TARGET_MINITPU_MCTARGETDESC_MINITPUMCTARGETDESC_H

#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCTargetOptions;
class Target;

MCCodeEmitter *createMiniTPUMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx);
MCAsmBackend *createMiniTPUAsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                      const MCRegisterInfo &MRI,
                                      const MCTargetOptions &Options);
} // namespace llvm

#define GET_REGINFO_ENUM
#include "MiniTPUGenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#include "MiniTPUGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "MiniTPUGenSubtargetInfo.inc"

#endif
