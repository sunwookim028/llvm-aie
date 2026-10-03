//===-- MiniTPURegisterInfo.cpp - MiniTPU registers -------------*- C++ -*-===//
//
// The machine has no stack and no calls; register allocation never runs.
//
//===----------------------------------------------------------------------===//
#include "MiniTPURegisterInfo.h"
#include "MiniTPUSubtarget.h"
#include "MCTargetDesc/MiniTPUMCTargetDesc.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "MiniTPUGenRegisterInfo.inc"

MiniTPURegisterInfo::MiniTPURegisterInfo() : MiniTPUGenRegisterInfo(0) {}

const MCPhysReg *
MiniTPURegisterInfo::getCalleeSavedRegs(const MachineFunction *) const {
  static const MCPhysReg None[] = {0};
  return None;
}

BitVector MiniTPURegisterInfo::getReservedRegs(const MachineFunction &) const {
  BitVector R(getNumRegs());
  // Kernel arguments and the state placeholders are never allocatable.
  for (MCPhysReg Reg : MiniTPU::ARGRegClass)
    R.set(Reg);
  for (MCPhysReg Reg : MiniTPU::STATERegClass)
    R.set(Reg);
  return R;
}

bool MiniTPURegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator,
                                              int, unsigned,
                                              RegScavenger *) const {
  report_fatal_error("MiniTPU has no stack frame");
}

Register MiniTPURegisterInfo::getFrameRegister(const MachineFunction &) const {
  return MiniTPU::NoRegister;
}
Register MiniTPURegisterInfo::getStackPointerRegister() const {
  return MiniTPU::NoRegister;
}
