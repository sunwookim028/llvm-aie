//===-- MiniTPUSubtarget.cpp - MiniTPU subtarget ----------------*- C++ -*-===//
#include "MiniTPUSubtarget.h"
#include "MCTargetDesc/MiniTPUMCTargetDesc.h"
#include "llvm/CodeGen/ScheduleDAG.h"

using namespace llvm;

#define DEBUG_TYPE "minitpu-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "MiniTPUGenSubtargetInfo.inc"

static StringRef cpuOrDefault(StringRef CPU) {
  return CPU.empty() ? StringRef("minitpu") : CPU;
}

MiniTPUSubtarget::MiniTPUSubtarget(const Triple &TT, StringRef CPU,
                                   StringRef FS, const TargetMachine &TM)
    : MiniTPUGenSubtargetInfo(TT, cpuOrDefault(CPU), cpuOrDefault(CPU), FS),
      TLInfo(TM, &RegInfo),
      InstrItins(getInstrItineraryForCPU(cpuOrDefault(CPU))) {}

static bool isStream(const MachineInstr &MI) {
  return MI.getOpcode() == MiniTPU::VMATLOAD ||
         MI.getOpcode() == MiniTPU::VMATPUSH;
}

void MiniTPUSubtarget::adjustSchedDependency(
    SUnit *Def, int DefOpIdx, SUnit *Use, int UseOpIdx, SDep &Dep,
    const TargetSchedModel *) const {
  if (!Def->isInstr() || !Use->isInstr())
    return;
  if (Dep.getKind() == SDep::Data) {
    // vmatload vN reads vN+1..vN+3 too; the exporter spells those as extra
    // implicit uses, which generic LLVM gives latency 0 ("fake operands added
    // by regalloc"). Every VREG read is timed alike: L = W + 1.
    if (Dep.getReg().isPhysical() && MiniTPU::VREGRegClass.contains(Dep.getReg()) &&
        DefOpIdx >= 0 && UseOpIdx >= 0)
      Dep.setLatency(*InstrInfo.getOperandLatency(&InstrItins, *Def->getInstr(),
                                                  DefOpIdx, *Use->getInstr(),
                                                  UseOpIdx));
    return;
  }
  if (Dep.getKind() != SDep::Anti && Dep.getKind() != SDep::Output)
    return;
  const MachineInstr &First = *Def->getInstr();
  const MachineInstr &Second = *Use->getInstr();
  Register Reg = Dep.getReg();
  if (!Reg.isPhysical() || !MiniTPU::VREGRegClass.contains(Reg) ||
      DefOpIdx < 0 || UseOpIdx < 0) {
    Dep.setLatency(Dep.getKind() == SDep::Anti ? 0 : 1);
    return;
  }
  if (Dep.getKind() == SDep::Output) {
    // asm._Timeline: ready[v] = t + W + 1 of the earlier write.
    Dep.setLatency(
        MiniTPUInstrInfo::operandCycle(&InstrItins, First, DefOpIdx) + 1);
    return;
  }
  // Anti: First reads Reg, Second writes it.
  if (!isStream(First)) {
    Dep.setLatency(0);
    return;
  }
  // Register enums are not in encoding order: use the hardware encoding.
  const int NumVregs = MiniTPU::VREGRegClass.getNumRegs();
  int Base = RegInfo.getEncodingValue(First.getOperand(0).getReg());
  int K = (RegInfo.getEncodingValue(Reg) - Base + NumVregs) % NumVregs;
  int Last = First.getOpcode() == MiniTPU::VMATLOAD
                 ? getMiniTPUScheduleFact("VmatloadStreamLast") -
                       getMiniTPUScheduleFact("StreamCyclesPerVreg") * K
                 : getMiniTPUScheduleFact("VmatpushStreamLast");
  int W = MiniTPUInstrInfo::operandCycle(&InstrItins, Second, UseOpIdx);
  Dep.setLatency(unsigned(std::max(Last - W, 0)));
}
