//===-- MiniTPUSubtarget.h - MiniTPU subtarget -----------------*- C++ -*-===//
#ifndef LLVM_LIB_TARGET_MINITPU_MINITPUSUBTARGET_H
#define LLVM_LIB_TARGET_MINITPU_MINITPUSUBTARGET_H

#include "AIEBaseSubtarget.h"
#include "MiniTPUInstrInfo.h"
#include "MiniTPURegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetLowering.h"

#define GET_SUBTARGETINFO_HEADER
#include "MiniTPUGenSubtargetInfo.inc"

namespace llvm {

/// No stack: a kernel has no frame.
class MiniTPUFrameLowering : public TargetFrameLowering {
public:
  MiniTPUFrameLowering()
      : TargetFrameLowering(StackGrowsUp, Align(4), 0, Align(4)) {}
  void emitPrologue(MachineFunction &, MachineBasicBlock &) const override {}
  void emitEpilogue(MachineFunction &, MachineBasicBlock &) const override {}

protected:
  bool hasFPImpl(const MachineFunction &) const override { return false; }
};

/// Only what MachineFunction construction reads; nothing is lowered.
class MiniTPUTargetLowering : public TargetLowering {
public:
  MiniTPUTargetLowering(const TargetMachine &TM, const TargetRegisterInfo *)
      : TargetLowering(TM) {}
};

class MiniTPUSubtarget : public MiniTPUGenSubtargetInfo {
  MiniTPUFrameLowering FrameLowering;
  MiniTPUInstrInfo InstrInfo;
  MiniTPURegisterInfo RegInfo;
  MiniTPUTargetLowering TLInfo;
  InstrItineraryData InstrItins;
  SelectionDAGTargetInfo TSInfo;
  AIEBaseAddrSpaceInfo AddrSpaceInfo;

public:
  MiniTPUSubtarget(const Triple &TT, StringRef CPU, StringRef FS,
                   const TargetMachine &TM);

  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  bool enableMachineScheduler() const override { return false; }
  bool enablePostRAMachineScheduler() const override { return true; }
  bool enablePostRAScheduler() const override { return true; }

  const MiniTPUFrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const InstrItineraryData *getInstrItineraryData() const override {
    return &InstrItins;
  }
  const MiniTPUInstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const MiniTPURegisterInfo *getRegisterInfo() const override {
    return &RegInfo;
  }
  const MiniTPUTargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }
  const AIEBaseAddrSpaceInfo &getAddrSpaceInfo() const override {
    return AddrSpaceInfo;
  }

  /// MiniTPU's dependence latencies (isa_latency.json scheduler.delays):
  ///  - write after write on a VREG: W + 1 of the FIRST writer, whatever the
  ///    second's W (writes land in program order);
  ///  - write after a stream's read: the write lands no earlier than the last
  ///    cycle the vmatload/vmatpush streams that VREG;
  ///  - any other anti or output edge, and every SREG or state edge: order only.
  void adjustSchedDependency(SUnit *Def, int DefOpIdx, SUnit *Use, int UseOpIdx,
                             SDep &Dep,
                             const TargetSchedModel *SchedModel) const override;
};
} // namespace llvm
#endif
