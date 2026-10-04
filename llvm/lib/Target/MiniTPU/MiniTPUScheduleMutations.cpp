//===-- MiniTPUScheduleMutations.cpp - MiniTPU DAG mutations ----*- C++ -*-===//
//
// Rules an itinerary cannot hold, added as dependence edges. Every number is
// read from MiniTPUScheduleFacts (generated from docs/isa_latency.json).
//
//===----------------------------------------------------------------------===//
#include "MiniTPU.h"
#include "MiniTPUBundleGroups.h"
#include "MiniTPUInstrInfo.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Debug.h"
#include <deque>

using namespace llvm;

#define DEBUG_TYPE "minitpu-mutations"

static void refuse(const Twine &Why) {
  report_fatal_error("MiniTPU schedule refused: " + Why, /*gen_crash_diag=*/false);
}

static void addEdge(SUnit &From, SUnit &To, unsigned Latency) {
  SDep D(&From, SDep::Artificial);
  D.setLatency(Latency);
  To.addPred(D, /*Required=*/true);
}

/// Set the latency of the edge Pred -> SU, in both directions. The two sides
/// are matched by what they connect, not by latency: AIE's region-end edges
/// arrive with the two sides already carrying different latencies (16 against
/// 17 on a vmatload's exit edge), and SDep::operator== compares the latency.
static void setEdgeLatency(SUnit &SU, SDep &PredEdge, unsigned Latency) {
  SUnit *Pred = PredEdge.getSUnit();
  SDep Forward = PredEdge;
  Forward.setSUnit(&SU);
  for (SDep &S : Pred->Succs)
    if (S.overlaps(Forward))
      S.setLatency(Latency);
  PredEdge.setLatency(Latency);
  SU.setDepthDirty();
  Pred->setHeightDirty();
}

/// One placed bundle as one issue unit: a dependence inside it is not a delay
/// (asm.schedule() times a bundle as one unit against what issued before it),
/// two writes of one VREG in it are refused (asm._check_bundle_writes), the
/// leader inherits every member's outside predecessor, and members hang off
/// the leader at latency 0.
static void groupAsUnit(const std::vector<SUnit *> &G,
                        const TargetRegisterInfo *TRI) {
  SUnit &Leader = *G.front();
  SmallPtrSet<SUnit *, 8> In(G.begin(), G.end());
  for (SUnit *M : G) {
    for (SDep &P : M->Preds) {
      if (!In.count(P.getSUnit()))
        continue;
      if (P.getKind() == SDep::Output && P.getReg().isPhysical() &&
          MiniTPU::VREGRegClass.contains(P.getReg()))
        refuse("one bundle writes " + Twine(TRI->getName(P.getReg())) +
               " twice; no delay orders the two writes");
      if (P.getLatency())
        setEdgeLatency(*M, P, 0);
    }
  }
  for (SUnit *M : G) {
    if (M == &Leader)
      continue;
    SmallVector<SDep, 8> Outside;
    for (SDep &P : M->Preds)
      if (!In.count(P.getSUnit()))
        Outside.push_back(P);
    for (SDep &P : Outside)
      if (P.getSUnit() != &Leader && !P.getSUnit()->isBoundaryNode())
        addEdge(*P.getSUnit(), Leader, P.getLatency());
    addEdge(Leader, *M, 0);
  }
}

namespace {

/// vmatpush_i -> vmatpop_i (result latency), the weight-switch span, and the
/// output-FIFO capacity, all paired in program order exactly as asm._Timeline
/// and asm._check_matrix_fifo pair them.
class MatrixEdges : public ScheduleDAGMutation {
  void apply(ScheduleDAGInstrs *DAG) override {
    const int ResultLatency = getMiniTPUScheduleFact("VmatpushResultLatency");
    const int Span = getMiniTPUScheduleFact("WeightSwitchSpan");
    const unsigned Banks = getMiniTPUScheduleFact("WeightBanks");
    const int Depth = getMiniTPUScheduleFact("OutputFifoDepth");
    const int PerPush = getMiniTPUScheduleFact("OutputFifoEntriesPerPush");
    const int PerPop = getMiniTPUScheduleFact("OutputFifoEntriesPerPop");

    std::deque<SUnit *> Unpopped;       // pushes whose result no pop has taken
    std::deque<SUnit *> FirstPush;      // per recent vmatload: its first push
    std::vector<SUnit *> Pops;
    int Outstanding = 0, Pushes = 0;
    for (SUnit &SU : DAG->SUnits) {
      if (!SU.isInstr())
        continue;
      switch (SU.getInstr()->getOpcode()) {
      case MiniTPU::VMATPUSH:
        Unpopped.push_back(&SU);
        if (!FirstPush.empty() && !FirstPush.back())
          FirstPush.back() = &SU;
        Outstanding += PerPush;
        if (Outstanding > Depth)
          refuse("vmatpush number " + Twine(Pushes) +
                 " overflows the MXU output FIFO (" + Twine(Depth) +
                 " entries), counted in program order; no delay drains it");
        // Capacity, for a scheduler that reorders: this push needs the pop
        // that freed its entries to come first.
        if (int Back = Depth / PerPush; Pushes >= Back && Pushes - Back < int(Pops.size()))
          addEdge(*Pops[Pushes - Back], SU, 1);
        ++Pushes;
        break;
      case MiniTPU::VMATPOP:
        if (!Unpopped.empty()) {
          addEdge(*Unpopped.front(), SU, ResultLatency);
          Unpopped.pop_front();
        }
        Outstanding = std::max(0, Outstanding - PerPop);
        Pops.push_back(&SU);
        break;
      case MiniTPU::VMATLOAD:
        if (FirstPush.size() == Banks && FirstPush.front())
          addEdge(*FirstPush.front(), SU, Span);
        FirstPush.push_back(nullptr);
        if (FirstPush.size() > Banks)
          FirstPush.pop_front();
        break;
      default:
        break;
      }
    }
  }
};

/// In-order mode: one issue unit per placed bundle, units chained in program
/// order with at least the placed delay between them.
class InOrderBundles : public ScheduleDAGMutation {
  void apply(ScheduleDAGInstrs *DAG) override {
    MiniTPUBundleGroups BG(DAG->SUnits, /*InOrder=*/true);
    SUnit *PrevLast = nullptr;
    unsigned PrevDelay = 0;
    for (auto &G : BG.Groups) {
      groupAsUnit(G, DAG->TRI);
      if (PrevLast)
        addEdge(*PrevLast, *G.front(), PrevDelay + 1);
      PrevLast = G.back();
      PrevDelay = MiniTPUBundleGroups::placedDelay(G);
    }
  }
};

/// Reorder mode: a pinned bundle (DELAY_GROUP) is one issue unit that keeps
/// its place in program order, and a free instruction stays between the
/// pinned bundles that surround it in program order. That is asm.pack()'s
/// model (asm._is_barrier): the region between two pinned bundles is
/// re-placed, nothing crosses a pinned bundle, and nothing joins one. The
/// latency 1 on every edge here is what keeps a free instruction out of a
/// pinned bundle's cycle; the pinned bundles are chained to each other at 1
/// too, so two of them never share a cycle, which is also what a placed
/// program means by two bundles.
class PinnedGroups : public ScheduleDAGMutation {
  void apply(ScheduleDAGInstrs *DAG) override {
    MiniTPUBundleGroups BG(DAG->SUnits, /*InOrder=*/false);
    for (auto &G : BG.Groups)
      groupAsUnit(G, DAG->TRI);
    SUnit *PrevLeader = nullptr;
    SmallVector<SUnit *, 32> FreeSince;
    for (SUnit &SU : DAG->SUnits) {
      if (!SU.isInstr())
        continue;
      if (BG.isGrouped(&SU)) {
        if (!BG.isLeader(&SU))
          continue;
        for (SUnit *F : FreeSince)
          addEdge(*F, SU, 1);
        FreeSince.clear();
        if (PrevLeader)
          addEdge(*PrevLeader, SU, 1);
        PrevLeader = &SU;
        continue;
      }
      if (PrevLeader)
        addEdge(*PrevLeader, SU, 1);
      FreeSince.push_back(&SU);
    }
  }
};

/// The VMEM words [Lo, Hi) an instruction touches, when they are known from
/// its operands: a non-AGU vld/vst (one word), a descriptor (its rows), a lock
/// (what it guards). An AGU access is unknown (the loop index is a run-time
/// value), which is treated as "may touch anything".
struct VmemWindow {
  int Lo, Hi;
};
static std::optional<VmemWindow> vmemWindow(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case MiniTPU::VLD:
  case MiniTPU::VST: {
    int W = MI.getOperand(1).getImm();
    return VmemWindow{W, W + 1};
  }
  case MiniTPU::VMEMLD:
  case MiniTPU::VMEMLD_D:
  case MiniTPU::VMEMST:
  case MiniTPU::VMEMST_D: {
    int W = MI.getOperand(1).getImm(), Rows = MI.getOperand(2).getImm();
    return VmemWindow{W, W + Rows};
  }
  case MiniTPU::LOCK_ACQUIRE:
  case MiniTPU::LOCK_RELEASE: {
    int W = MI.getOperand(1).getImm(), Count = MI.getOperand(2).getImm();
    return VmemWindow{W, W + Count};
  }
  default:
    return std::nullopt;
  }
}

static bool isVmemAccess(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case MiniTPU::VLD:
  case MiniTPU::VLD_AGU:
  case MiniTPU::VST:
  case MiniTPU::VST_AGU:
  case MiniTPU::VMEMLD:
  case MiniTPU::VMEMLD_D:
  case MiniTPU::VMEMST:
  case MiniTPU::VMEMST_D:
    return true;
  default:
    return false;
  }
}

/// EXPERIMENTAL (docs/isa_experimental.json). A lock is hasSideEffects, so
/// generic LLVM orders every VMEM access against it, and AIE's LockDelays
/// mutation (through MiniTPUInstrInfo::isLock and the lock cycles) has already
/// set the stall and resume latencies on those edges. What this adds is the
/// lock's reach: an access whose VMEM words cannot meet the guarded window is
/// detached from the lock on both sides, and the accesses it had been
/// separating are re-ordered directly where they may alias (the generic
/// barrier chain had routed that order through the lock).
class LockEdges : public ScheduleDAGMutation {
  void apply(ScheduleDAGInstrs *DAG) override {
    const auto *TII = static_cast<const MiniTPUInstrInfo *>(DAG->TII);
    for (SUnit &Lock : DAG->SUnits) {
      if (!Lock.isInstr() || !TII->isLock(Lock.getInstr()->getOpcode()))
        continue;
      VmemWindow Guard = *vmemWindow(*Lock.getInstr());
      auto Disjoint = [&](const MachineInstr &MI) {
        auto W = vmemWindow(MI);
        return W && (W->Hi <= Guard.Lo || Guard.Hi <= W->Lo);
      };
      SmallVector<SUnit *, 16> Before, After;
      for (SDep P : SmallVector<SDep, 16>(Lock.Preds.begin(), Lock.Preds.end())) {
        MachineInstr *MI = P.getSUnit()->getInstr();
        if (P.getKind() != SDep::Order || !MI || !isVmemAccess(*MI) ||
            !Disjoint(*MI))
          continue;
        Lock.removePred(P);
        Before.push_back(P.getSUnit());
      }
      for (SDep S : SmallVector<SDep, 16>(Lock.Succs.begin(), Lock.Succs.end())) {
        SUnit *Succ = S.getSUnit();
        MachineInstr *MI = Succ->getInstr();
        if (S.getKind() != SDep::Order || !MI || !isVmemAccess(*MI) ||
            !Disjoint(*MI))
          continue;
        for (SDep P : SmallVector<SDep, 16>(Succ->Preds.begin(), Succ->Preds.end()))
          if (P.getSUnit() == &Lock && P.getKind() == SDep::Order)
            Succ->removePred(P);
        After.push_back(Succ);
      }
      // The order the lock had carried between the detached accesses.
      for (SUnit *B : Before)
        for (SUnit *A : After) {
          const MachineInstr &MB = *B->getInstr(), &MA = *A->getInstr();
          if (!MB.mayStore() && !MA.mayStore())
            continue;
          if (TII->areMemAccessesTriviallyDisjoint(MB, MA))
            continue;
          addEdge(*B, *A, 1);
        }
    }
  }
};

/// A region that ends in halt has no successor: drop AIE's region-end latencies
/// (it assumes an unknown successor reads every result in flight).
class ControlSinkExit : public ScheduleDAGMutation {
  void apply(ScheduleDAGInstrs *DAG) override {
    bool EndsInHalt = false;
    for (SUnit &SU : DAG->SUnits)
      if (SU.isInstr())
        EndsInHalt = SU.getInstr()->getOpcode() == MiniTPU::HALT ||
                     (EndsInHalt && (SU.getInstr()->getOpcode() == MiniTPU::DELAY ||
                                     SU.getInstr()->getOpcode() == MiniTPU::DELAY_GROUP));
    LLVM_DEBUG(dbgs() << "ControlSinkExit: EndsInHalt=" << EndsInHalt
                      << " ExitSU preds=" << DAG->ExitSU.Preds.size() << "\n");
    if (!EndsInHalt)
      return;
    SUnit &ExitSU = DAG->ExitSU;
    for (SDep &P : ExitSU.Preds) {
      LLVM_DEBUG(dbgs() << "  pred SU(" << P.getSUnit()->NodeNum << ") latency "
                        << P.getLatency() << " -> 0\n");
      setEdgeLatency(ExitSU, P, 0);
    }
  }
};
} // namespace

std::unique_ptr<ScheduleDAGMutation> llvm::createMiniTPUMatrixEdges() {
  return std::make_unique<MatrixEdges>();
}
std::unique_ptr<ScheduleDAGMutation> llvm::createMiniTPUInOrderBundles() {
  return std::make_unique<InOrderBundles>();
}
std::unique_ptr<ScheduleDAGMutation> llvm::createMiniTPUPinnedGroups() {
  return std::make_unique<PinnedGroups>();
}
std::unique_ptr<ScheduleDAGMutation> llvm::createMiniTPULockEdges() {
  return std::make_unique<LockEdges>();
}
std::unique_ptr<ScheduleDAGMutation> llvm::createMiniTPUControlSinkExit() {
  return std::make_unique<ControlSinkExit>();
}
