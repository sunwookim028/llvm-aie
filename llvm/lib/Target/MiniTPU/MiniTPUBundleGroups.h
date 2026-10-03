//===-- MiniTPUBundleGroups.h - placed bundles as issue units --*- C++ -*-===//
//
// In-order mode (gate G3) schedules a PLACED program: every bundle keeps its
// instructions, and the only freedom is how many stall cycles follow it. The
// exporter ends every placed bundle with its DELAY instruction (delay #0 when
// the field is empty), so a bundle is the run of instructions up to and
// including a DELAY. A DELAY alone is a literal empty bundle, which issues.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIB_TARGET_MINITPU_MINITPUBUNDLEGROUPS_H
#define LLVM_LIB_TARGET_MINITPU_MINITPUBUNDLEGROUPS_H

#include "MCTargetDesc/MiniTPUMCTargetDesc.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include <vector>

namespace llvm {
struct MiniTPUBundleGroups {
  std::vector<std::vector<SUnit *>> Groups;
  DenseMap<const SUnit *, unsigned> GroupOf;

  explicit MiniTPUBundleGroups(std::vector<SUnit> &SUnits) {
    std::vector<SUnit *> Current;
    for (SUnit &SU : SUnits) {
      if (!SU.isInstr())
        continue;
      Current.push_back(&SU);
      if (SU.getInstr()->getOpcode() == MiniTPU::DELAY) {
        Groups.push_back(Current);
        Current.clear();
      }
    }
    if (!Current.empty())
      Groups.push_back(Current);
    for (unsigned G = 0; G < Groups.size(); ++G)
      for (SUnit *SU : Groups[G])
        GroupOf[SU] = G;
  }
  bool isLeader(const SUnit *SU) const {
    auto It = GroupOf.find(SU);
    return It != GroupOf.end() && Groups[It->second].front() == SU;
  }
  /// The delay field the placed bundle already carries: a minimum.
  static unsigned placedDelay(const std::vector<SUnit *> &G) {
    const MachineInstr &Last = *G.back()->getInstr();
    return Last.getOpcode() == MiniTPU::DELAY ? Last.getOperand(0).getImm() : 0;
  }
};
} // namespace llvm
#endif
