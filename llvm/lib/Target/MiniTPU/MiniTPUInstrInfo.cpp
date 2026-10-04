//===-- MiniTPUInstrInfo.cpp - MiniTPU instruction information --*- C++ -*-===//
#include "MiniTPUInstrInfo.h"
#include "AIEHazardRecognizer.h"
#include "AIEMachineScheduler.h"
#include "MCTargetDesc/MiniTPUMCFormats.h"
#include "MCTargetDesc/MiniTPUMCTargetDesc.h"
#include "MiniTPUHazardRecognizer.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInstrItineraries.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#define GET_INSTRINFO_SCHED_ENUM
#include "MiniTPUGenInstrInfo.inc"

namespace {
struct MiniTPUScheduleFact {
  const char *Name;
  unsigned Value;
};
#define GET_MiniTPUScheduleFacts_DECL
#define GET_MiniTPUScheduleFacts_IMPL
#include "MiniTPUGenSearchableTables.inc"

const MiniTPUMCFormats MiniTPUFormats;
} // namespace

int llvm::getMiniTPUScheduleFact(StringRef Name) {
  for (const MiniTPUScheduleFact &F : MiniTPUScheduleFacts)
    if (Name == F.Name)
      return int(F.Value);
  report_fatal_error("MiniTPUScheduleFacts has no fact " + Name);
}

MiniTPUInstrInfo::MiniTPUInstrInfo() : MiniTPUGenInstrInfo() {
  FormatInterface = &MiniTPUFormats;
  FuncUnitWrapper::setFormatInterface(FormatInterface);
}

unsigned MiniTPUInstrInfo::getNopOpcode() const { return MiniTPU::NOP; }

ScheduleHazardRecognizer *MiniTPUInstrInfo::CreateTargetMIHazardRecognizer(
    const InstrItineraryData *II, const ScheduleDAGMI *DAG) const {
  if (DAG->hasVRegLiveness())
    report_fatal_error("MiniTPU schedules after register allocation only");
  AIEAlternateDescriptors &Alt = static_cast<const AIEScheduleDAGMI *>(DAG)
                                     ->getSchedImpl()
                                     ->getSelectedAltDescs();
  return new MiniTPUHazardRecognizer(this, II, Alt, DAG);
}

unsigned MiniTPUInstrInfo::operandCycle(const InstrItineraryData *ItinData,
                                        const MachineInstr &MI, unsigned Idx) {
  std::optional<unsigned> C =
      ItinData->getOperandCycle(MI.getDesc().getSchedClass(), Idx);
  return C.value_or(0);
}

std::optional<unsigned> MiniTPUInstrInfo::getOperandLatency(
    const InstrItineraryData *ItinData, const MachineInstr &DefMI,
    unsigned DefIdx, const MachineInstr &UseMI, unsigned UseIdx) const {
  int Def = operandCycle(ItinData, DefMI, DefIdx);
  int Use = operandCycle(ItinData, UseMI, UseIdx);
  return unsigned(std::max(Def - Use + 1, 1));
}

// -- VMEM through the compute port -------------------------------------------

static bool isComputePortAccess(unsigned SchedClass) {
  return SchedClass == MiniTPU::Sched::II_VLD ||
         SchedClass == MiniTPU::Sched::II_VST;
}

std::optional<int>
MiniTPUInstrInfo::getFirstMemoryCycle(unsigned SchedClass) const {
  if (isComputePortAccess(SchedClass))
    return 0;
  return std::nullopt;
}

std::optional<int>
MiniTPUInstrInfo::getLastMemoryCycle(unsigned SchedClass) const {
  return getFirstMemoryCycle(SchedClass);
}

namespace {
/// A vld/vst's VMEM address: a literal word, plus (loop index << shift) of the
/// loop at `Level` when `Agu`.
struct LdStAddress {
  int Word;
  bool Agu;
  int Level, Shift;
};
} // namespace

static std::optional<LdStAddress> ldstAddress(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case MiniTPU::VLD:
  case MiniTPU::VST:
    return LdStAddress{int(MI.getOperand(1).getImm()), false, 0, 0};
  case MiniTPU::VLD_AGU:
  case MiniTPU::VST_AGU:
    return LdStAddress{int(MI.getOperand(1).getImm()), true,
                       int(MI.getOperand(2).getImm()),
                       int(MI.getOperand(3).getImm())};
  default:
    return std::nullopt;
  }
}

bool MiniTPUInstrInfo::areMemAccessesTriviallyDisjoint(
    const MachineInstr &MIa, const MachineInstr &MIb) const {
  auto A = ldstAddress(MIa), B = ldstAddress(MIb);
  if (!A || !B)
    return false;
  if (!A->Agu && !B->Agu)
    return A->Word != B->Word;
  if (A->Agu && B->Agu && A->Level == B->Level && A->Shift == B->Shift)
    return A->Word != B->Word;
  return false;
}

// -- EXPERIMENTAL: the mock lock ---------------------------------------------

bool MiniTPUInstrInfo::isLock(unsigned Opc) const {
  return isAcquire(Opc) || isRelease(Opc);
}
bool MiniTPUInstrInfo::isAcquire(unsigned Opc) const {
  return Opc == MiniTPU::LOCK_ACQUIRE;
}
bool MiniTPUInstrInfo::isRelease(unsigned Opc) const {
  return Opc == MiniTPU::LOCK_RELEASE;
}
int MiniTPUInstrInfo::getCoreStallCycleAfterLock() const {
  return getMiniTPUScheduleFact("LockCoreStallCycle");
}
int MiniTPUInstrInfo::getCoreResumeCycleAfterLock() const {
  return getMiniTPUScheduleFact("LockCoreResumeCycle");
}
