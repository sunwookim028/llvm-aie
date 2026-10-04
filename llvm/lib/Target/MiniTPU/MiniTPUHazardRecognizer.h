//===-- MiniTPUHazardRecognizer.h - MiniTPU hazards ------------*- C++ -*-===//
//
// AIEHazardRecognizer (the scoreboard over the generated itineraries: write
// port, port C, matrix engines, the three co-issue claims, the lock unit) plus
// what MiniTPU adds:
//  - a placed bundle that must stay whole (every bundle in in-order mode, the
//    pinned bundles in reorder mode) is one issue unit, so its first
//    instruction is admitted only if the WHOLE bundle fits the scoreboard in
//    that cycle;
//  - S_LAT: a descriptor or loop.begin.r reading an SREG must issue
//    ScalarLatencyBundles bundles after the S op that wrote it.
//    In-order mode counts issued bundles; a stall cycle does not count (it
//    becomes a delay field), so no stall can satisfy it and the program is
//    refused, as asm.py refuses it.
//    Reorder mode counts CYCLES and reports a hazard, so the scheduler fills
//    the gap with other instructions or advances the cycle; the delay-folding
//    pass then keeps as many of those cycles as literal empty bundles as the
//    bundle count needs (asm.pack() keeps the same gaps, asm._needs_literal_gap).
//    A write and a read of one SREG inside one pinned bundle is refused in
//    both modes: no gap can separate them.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIB_TARGET_MINITPU_MINITPUHAZARDRECOGNIZER_H
#define LLVM_LIB_TARGET_MINITPU_MINITPUHAZARDRECOGNIZER_H

#include "AIEHazardRecognizer.h"
#include "MiniTPUBundleGroups.h"
#include <memory>

namespace llvm {
class ScheduleDAGMI;

class MiniTPUHazardRecognizer : public AIEHazardRecognizer {
  const ScheduleDAGMI *DAG;
  const AIEBaseInstrInfo *TheTII;
  const InstrItineraryData *Itins;
  AIEAlternateDescriptors &AltDescs;
  std::unique_ptr<MiniTPUBundleGroups> Groups;
  /// In-order mode: bundle index of the last write to each SREG (by encoding).
  DenseMap<unsigned, int> LastSWrite;
  int GroupsIssued = 0;
  /// Reorder mode: cycles advanced so far (top-down), and the cycle of the
  /// last write to each SREG.
  int Cycle = 0;
  DenseMap<unsigned, int> LastSWriteCycle;

  MiniTPUBundleGroups &groups();
  /// Refuse a bundle no cycle can admit: its own instructions conflict.
  void checkBundleAlone(const std::vector<SUnit *> &G);
  void checkScalarLatency(const std::vector<SUnit *> &G, int Index);
  /// Reorder mode: whether \p SU, issued at \p AtCycle, reads an SREG written
  /// fewer than ScalarLatencyBundles cycles before.
  bool scalarHazard(const SUnit *SU, int AtCycle) const;

public:
  MiniTPUHazardRecognizer(const AIEBaseInstrInfo *TII,
                          const InstrItineraryData *II,
                          AIEAlternateDescriptors &Alt, const ScheduleDAGMI *DAG);
  HazardType getHazardType(SUnit *SU, int DeltaCycles) override;
  void EmitInstruction(SUnit *SU, int DeltaCycles) override;
  void EmitInstruction(SUnit *SU) override { EmitInstruction(SU, 0); }
  void AdvanceCycle() override;
  void RecedeCycle() override;
  void Reset() override;
};

/// Which SREG an instruction reads on its issue cycle: a descriptor's base and
/// stride, a loop.begin.r's bound (asm._issue_time_sreg_reads), and with
/// -minitpu-slat-all-readers an S op's source too (review finding M6-F2).
void miniTPUIssueTimeSRegReads(const MachineInstr &MI,
                               const TargetRegisterInfo *TRI,
                               SmallVectorImpl<unsigned> &Regs);
void miniTPUSRegWrites(const MachineInstr &MI, const TargetRegisterInfo *TRI,
                       SmallVectorImpl<unsigned> &Regs);
} // namespace llvm
#endif
