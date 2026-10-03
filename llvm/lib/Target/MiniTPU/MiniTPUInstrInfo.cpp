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
