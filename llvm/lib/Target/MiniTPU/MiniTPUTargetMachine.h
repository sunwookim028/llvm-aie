//===-- MiniTPUTargetMachine.h - MiniTPU target machine --------*- C++ -*-===//
#ifndef LLVM_LIB_TARGET_MINITPU_MINITPUTARGETMACHINE_H
#define LLVM_LIB_TARGET_MINITPU_MINITPUTARGETMACHINE_H

#include "AIEBaseTargetMachine.h"
#include "MiniTPUSubtarget.h"

namespace llvm {
class MiniTPUTargetMachine : public AIEBaseTargetMachine {
  MiniTPUSubtarget Subtarget;

public:
  MiniTPUTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                       StringRef FS, const TargetOptions &Options,
                       std::optional<Reloc::Model> RM,
                       std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                       bool JIT);
  const MiniTPUSubtarget *getSubtargetImpl(const Function &) const override {
    return &Subtarget;
  }
  const AIEBaseSubtarget *getAIESubtarget() const override {
    return &Subtarget;
  }
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;
  ScheduleDAGInstrs *
  createPostMachineScheduler(MachineSchedContext *C) const override;
  bool targetSchedulesPostRAScheduling() const override { return true; }
};
} // namespace llvm
#endif
