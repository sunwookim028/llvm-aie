//===-- MiniTPUInstrInfo.h - MiniTPU instruction information ---*- C++ -*-===//
//
// Spike: the subset of AIEBaseInstrInfo that AIE's post-RA scheduler reads.
// No instruction selection, spilling or branch analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MINITPU_MINITPUINSTRINFO_H
#define LLVM_LIB_TARGET_MINITPU_MINITPUINSTRINFO_H

#include "AIEBaseInstrInfo.h"
#include "MiniTPURegisterInfo.h"

#define GET_INSTRINFO_HEADER
#include "MiniTPUGenInstrInfo.inc"

namespace llvm {

/// A fact from MiniTPUGenSchedule.td's MiniTPUScheduleFacts table. Aborts if
/// the name is not there: no rule may fall back to a literal.
int getMiniTPUScheduleFact(StringRef Name);

class MiniTPUInstrInfo : public MiniTPUGenInstrInfo {
public:
  MiniTPUInstrInfo();

  /// The empty bundle the scheduler inserts for a stall cycle. The delay
  /// folding pass turns runs of these into DELAY fields.
  unsigned getNopOpcode() const override;

  VarItinInterface getVarItinInterface() const override { return {}; }

  ScheduleHazardRecognizer *
  CreateTargetMIHazardRecognizer(const InstrItineraryData *II,
                                 const ScheduleDAGMI *DAG) const override;

  /// Def at its itinerary cycle (W), use at its itinerary cycle or 0: L = W + 1
  /// for every operand, implicit ones included.
  std::optional<unsigned> getOperandLatency(const InstrItineraryData *ItinData,
                                            const MachineInstr &DefMI,
                                            unsigned DefIdx,
                                            const MachineInstr &UseMI,
                                            unsigned UseIdx) const override;

  /// The itinerary cycle at which operand \p Idx of \p MI is read or written;
  /// 0 when the itinerary names none.
  static unsigned operandCycle(const InstrItineraryData *ItinData,
                               const MachineInstr &MI, unsigned Idx);

  unsigned getInstSizeInBytes(const MachineInstr &MI) const override {
    return 0;
  }
  unsigned getMachineBlockAlignmentBytes() const override { return 16; }
};

} // namespace llvm
#endif
