//===-- MiniTPUBundleGroups.h - placed bundles as issue units --*- C++ -*-===//
//
// In-order mode (gate G3) schedules a PLACED program: every bundle keeps its
// instructions, and the only freedom is how many stall cycles follow it. The
// exporter ends every placed bundle with its DELAY instruction (delay #0 when
// the field is empty), so a bundle is the run of instructions up to and
// including a DELAY. A DELAY alone is a literal empty bundle, which issues.
//
// Reorder mode (gate G4) dissolves every placed bundle into free instructions
// except the bundles that hold a PINNED op (a descriptor, a control op, and by
// asm.MACHINE["movable"] policy an M or S op): the exporter closes each of those
// with DELAY_GROUP <members>, <delay>, and that run is one issue unit. Every
// other instruction is in no group.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIB_TARGET_MINITPU_MINITPUBUNDLEGROUPS_H
#define LLVM_LIB_TARGET_MINITPU_MINITPUBUNDLEGROUPS_H

#include "MCTargetDesc/MiniTPUMCTargetDesc.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/Support/ErrorHandling.h"
#include <vector>

namespace llvm {
struct MiniTPUBundleGroups {
  std::vector<std::vector<SUnit *>> Groups;
  DenseMap<const SUnit *, unsigned> GroupOf;

  MiniTPUBundleGroups(std::vector<SUnit> &SUnits, bool InOrder) {
    std::vector<SUnit *> Order;
    for (SUnit &SU : SUnits)
      if (SU.isInstr())
        Order.push_back(&SU);
    if (InOrder) {
      std::vector<SUnit *> Current;
      for (SUnit *SU : Order) {
        Current.push_back(SU);
        if (SU->getInstr()->getOpcode() == MiniTPU::DELAY) {
          Groups.push_back(Current);
          Current.clear();
        }
      }
      if (!Current.empty())
        Groups.push_back(Current);
    } else {
      for (unsigned I = 0; I < Order.size(); ++I) {
        const MachineInstr &MI = *Order[I]->getInstr();
        if (MI.getOpcode() != MiniTPU::DELAY_GROUP)
          continue;
        unsigned Members = MI.getOperand(0).getImm();
        if (Members > I)
          report_fatal_error("MiniTPU: DELAY_GROUP names more instructions "
                             "than precede it");
        Groups.emplace_back(Order.begin() + (I - Members),
                            Order.begin() + I + 1);
      }
    }
    for (unsigned G = 0; G < Groups.size(); ++G)
      for (SUnit *SU : Groups[G])
        GroupOf[SU] = G;
  }
  bool isGrouped(const SUnit *SU) const { return GroupOf.count(SU); }
  bool isLeader(const SUnit *SU) const {
    auto It = GroupOf.find(SU);
    return It != GroupOf.end() && Groups[It->second].front() == SU;
  }
  /// The delay field the placed bundle already carries: a minimum.
  static unsigned placedDelay(const std::vector<SUnit *> &G) {
    const MachineInstr &Last = *G.back()->getInstr();
    if (Last.getOpcode() == MiniTPU::DELAY)
      return Last.getOperand(0).getImm();
    if (Last.getOpcode() == MiniTPU::DELAY_GROUP)
      return Last.getOperand(1).getImm();
    return 0;
  }
};
} // namespace llvm
#endif
