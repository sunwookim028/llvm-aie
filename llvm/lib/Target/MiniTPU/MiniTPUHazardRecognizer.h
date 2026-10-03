//===-- MiniTPUHazardRecognizer.h - MiniTPU hazards ------------*- C++ -*-===//
//
// AIEHazardRecognizer (the scoreboard over the generated itineraries: write
// port, port C, matrix engines, the three co-issue claims) plus what MiniTPU
// adds:
//  - in-order mode: a placed bundle is one issue unit, so its first instruction
//    is admitted only if the WHOLE bundle fits the scoreboard in that cycle;
//  - S_LAT, counted in BUNDLES: a descriptor or loop.begin.r reading an SREG
//    must issue ScalarLatencyBundles bundles after the S op that wrote it. A
//    stall cycle does not count (it becomes a delay field), so in-order no
//    stall can satisfy it and the program is refused, as asm.py refuses it.
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
  /// Bundle index of the last write to each SREG (by encoding).
  DenseMap<unsigned, int> LastSWrite;
  int GroupsIssued = 0;

  MiniTPUBundleGroups &groups();
  /// Refuse a bundle no cycle can admit: its own instructions conflict.
  void checkBundleAlone(const std::vector<SUnit *> &G);
  void checkScalarLatency(const std::vector<SUnit *> &G, int Index);

public:
  MiniTPUHazardRecognizer(const AIEBaseInstrInfo *TII,
                          const InstrItineraryData *II,
                          AIEAlternateDescriptors &Alt, const ScheduleDAGMI *DAG);
  HazardType getHazardType(SUnit *SU, int DeltaCycles) override;
  void EmitInstruction(SUnit *SU, int DeltaCycles) override;
  void EmitInstruction(SUnit *SU) override { EmitInstruction(SU, 0); }
  void Reset() override;
};
} // namespace llvm
#endif
