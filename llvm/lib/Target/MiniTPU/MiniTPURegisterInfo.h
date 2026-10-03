//===-- MiniTPURegisterInfo.h - MiniTPU registers --------------*- C++ -*-===//
#ifndef LLVM_LIB_TARGET_MINITPU_MINITPUREGISTERINFO_H
#define LLVM_LIB_TARGET_MINITPU_MINITPUREGISTERINFO_H

#include "AIEBaseRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "MiniTPUGenRegisterInfo.inc"

namespace llvm {
struct MiniTPURegisterInfo : public MiniTPUGenRegisterInfo {
  MiniTPURegisterInfo();
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  BitVector getReservedRegs(const MachineFunction &MF) const override;
  bool eliminateFrameIndex(MachineBasicBlock::iterator MI, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;
  Register getFrameRegister(const MachineFunction &MF) const override;
  Register getStackPointerRegister() const override;
  // No sticky status registers and no FIFO registers in AIE's sense: the MXU
  // output FIFO is the MXUFIFO placeholder, ordered like any register.
  bool isReservedStickyReg(MCRegister) const override { return false; }
  bool isFifoPhysReg(const Register) const override { return false; }
  bool isVecOrAccRegClass(const TargetRegisterClass &RC) const override {
    return &RC == &MiniTPU::VREGRegClass;
  }
  bool trackLivenessAfterRegAlloc(const MachineFunction &) const override {
    return true;
  }
};
} // namespace llvm
#endif
