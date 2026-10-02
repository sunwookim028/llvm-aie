//===-- MiniTPUMCTargetDesc.cpp - MiniTPU target descriptions -------------===//
#include "MiniTPUMCTargetDesc.h"
#include "InstPrinter/MiniTPUInstPrinter.h"
#include "MCTargetDesc/AIEMCAsmInfo.h"
#include "TargetInfo/MiniTPUTargetInfo.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "MiniTPUGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "MiniTPUGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "MiniTPUGenRegisterInfo.inc"

static MCInstrInfo *createMiniTPUMCInstrInfo() {
  auto *X = new MCInstrInfo();
  InitMiniTPUMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createMiniTPUMCRegisterInfo(const Triple &TT) {
  auto *X = new MCRegisterInfo();
  InitMiniTPUMCRegisterInfo(X, MiniTPU::NoRegister);
  return X;
}

static MCSubtargetInfo *createMiniTPUMCSubtargetInfo(const Triple &TT,
                                                     StringRef CPU, StringRef FS) {
  return createMiniTPUMCSubtargetInfoImpl(TT, CPU, CPU, FS);
}

static MCAsmInfo *createMiniTPUMCAsmInfo(const MCRegisterInfo &MRI,
                                         const Triple &TT,
                                         const MCTargetOptions &Options) {
  // AIE's: "//" comments, ";" separates the slots of one bundle.
  return new AIEMCAsmInfo(TT);
}

static MCInstPrinter *createMiniTPUMCInstPrinter(const Triple &TT,
                                                 unsigned SyntaxVariant,
                                                 const MCAsmInfo &MAI,
                                                 const MCInstrInfo &MII,
                                                 const MCRegisterInfo &MRI) {
  return new MiniTPUInstPrinter(MAI, MII, MRI);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMiniTPUTargetMC() {
  Target &T = getTheMiniTPUTarget();
  RegisterMCAsmInfoFn X(T, createMiniTPUMCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createMiniTPUMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createMiniTPUMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createMiniTPUMCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createMiniTPUMCInstPrinter);
  TargetRegistry::RegisterMCCodeEmitter(T, createMiniTPUMCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createMiniTPUAsmBackend);
}
