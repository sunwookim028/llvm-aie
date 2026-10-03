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

/// Set the latency of the edge Pred -> SU, in both directions.
static void setEdgeLatency(SUnit &SU, SDep &PredEdge, unsigned Latency) {
  SUnit *Pred = PredEdge.getSUnit();
  SDep Forward = PredEdge;
  Forward.setSUnit(&SU);
  for (SDep &S : Pred->Succs)
    if (S == Forward)
      S.setLatency(Latency);
  PredEdge.setLatency(Latency);
  SU.setDepthDirty();
  Pred->setHeightDirty();
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
    MiniTPUBundleGroups BG(DAG->SUnits);
    const TargetRegisterInfo *TRI = DAG->TRI;
    SUnit *PrevLast = nullptr;
    unsigned PrevDelay = 0;
    for (auto &G : BG.Groups) {
      SUnit &Leader = *G.front();
      SmallPtrSet<SUnit *, 8> In(G.begin(), G.end());
      for (SUnit *M : G) {
        // asm.schedule() times a bundle as one unit against what issued before
        // it; a dependence inside one bundle is not a delay (and two writes of
        // one VREG in a bundle are refused, as asm._check_bundle_writes does).
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
      if (PrevLast)
        addEdge(*PrevLast, Leader, PrevDelay + 1);
      PrevLast = G.back();
      PrevDelay = MiniTPUBundleGroups::placedDelay(G);
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
                     (EndsInHalt && SU.getInstr()->getOpcode() == MiniTPU::DELAY);
    if (!EndsInHalt)
      return;
    SUnit &ExitSU = DAG->ExitSU;
    for (SDep &P : ExitSU.Preds)
      setEdgeLatency(ExitSU, P, 0);
  }
};
} // namespace

std::unique_ptr<ScheduleDAGMutation> llvm::createMiniTPUMatrixEdges() {
  return std::make_unique<MatrixEdges>();
}
std::unique_ptr<ScheduleDAGMutation> llvm::createMiniTPUInOrderBundles() {
  return std::make_unique<InOrderBundles>();
}
std::unique_ptr<ScheduleDAGMutation> llvm::createMiniTPUControlSinkExit() {
  return std::make_unique<ControlSinkExit>();
}
