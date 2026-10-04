//===-- MiniTPUFoldDelays.cpp - stall cycles into DELAY fields --*- C++ -*-===//
//
// After postmisched, a stall cycle is a standalone NOP (getNopOpcode()). The
// machine has no such instruction: a bundle's 7-bit DELAY field holds issue
// that many cycles. Fold each run of NOPs into the DELAY of the bundle before
// it; a gap above MaxDelay keeps that bundle at MaxDelay and adds empty filler
// bundles (a DELAY alone), each one issue cycle plus its own delay.
//
// Literal empty bundles of the placed program are DELAY-only bundles, not
// NOPs, so they are never folded: they count as bundles for S_LAT, which is why
// asm.pack() keeps them (asm._needs_literal_gap).
//
// Reorder mode (G4) then re-derives those literal bundles itself: the hazard
// recognizer kept S_LAT in CYCLES, and S_LAT is a count of BUNDLES, so where a
// descriptor or loop.begin.r sits fewer than S_LAT bundles after the write of
// an SREG it reads -- on any issue path (asm.issue_paths), and around each
// loop's back edge (asm._check_scalar_read_gap_across_back_edges) -- a delay
// cycle of the bundle before it is turned into a literal empty bundle, or one
// is added when there is no delay cycle to turn.
//
//===----------------------------------------------------------------------===//
#include "MCTargetDesc/MiniTPUMCTargetDesc.h"
#include "AIEHazardRecognizer.h"
#include "MiniTPU.h"
#include "MiniTPUHazardRecognizer.h"
#include "MiniTPUInstrInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "minitpu-fold-delays"

namespace {
class MiniTPUFoldDelays : public MachineFunctionPass {
public:
  static char ID;
  MiniTPUFoldDelays() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override {
    return "MiniTPU fold stall cycles into delay fields";
  }
  bool runOnMachineFunction(MachineFunction &MF) override;
};

/// One issued bundle after folding: its instructions and its DELAY.
struct Bundle {
  MachineInstr *First = nullptr; // the BUNDLE root or the standalone instr
  MachineInstr *Delay = nullptr;
  SmallVector<MachineInstr *, 6> Instrs;
};
} // namespace

char MiniTPUFoldDelays::ID = 0;
INITIALIZE_PASS(MiniTPUFoldDelays, DEBUG_TYPE,
                "MiniTPU fold stall cycles into delay fields", false, false)

FunctionPass *llvm::createMiniTPUFoldDelaysPass() {
  return new MiniTPUFoldDelays();
}

/// The DELAY instruction of the bundle headed by \p Head, or null.
static MachineInstr *delayOf(MachineInstr &Head) {
  for (MachineInstr &MI :
       make_range(Head.getIterator(), getBundleEnd(Head.getIterator())))
    if (MI.getOpcode() == MiniTPU::DELAY)
      return &MI;
  return nullptr;
}

static std::vector<Bundle> bundlesOf(MachineBasicBlock &MBB) {
  std::vector<Bundle> Out;
  for (MachineInstr &MI : MBB) {
    Bundle B;
    B.First = &MI;
    // A scheduled bundle may have a BUNDLE header or be a bare run of
    // bundled instructions (a DELAY the fold gave a lone instruction).
    for (auto I = MI.getIterator(), E = getBundleEnd(MI.getIterator()); I != E; ++I) {
      if (I->isBundle())
        continue;
      if (I->getOpcode() == MiniTPU::DELAY)
        B.Delay = &*I;
      else
        B.Instrs.push_back(&*I);
    }
    if (!B.Delay)
      report_fatal_error("MiniTPU fold-delays: a bundle without its DELAY");
    Out.push_back(B);
  }
  return Out;
}

namespace {
struct LoopSpan {
  int Begin, End; // bundle indices of loop.begin(.r) and its loop.end
  bool Skippable; // loop.begin.r: may run no trip
};
} // namespace

static std::vector<LoopSpan> loopsOf(const std::vector<Bundle> &Bundles) {
  std::vector<LoopSpan> Out;
  SmallVector<std::pair<int, bool>, 8> Stack;
  for (int I = 0, E = Bundles.size(); I < E; ++I)
    for (MachineInstr *MI : Bundles[I].Instrs) {
      unsigned Opc = MI->getOpcode();
      if (Opc == MiniTPU::LOOP_BEGIN)
        Stack.push_back({I, false});
      else if (Opc == MiniTPU::LOOP_BEGIN_R_S || Opc == MiniTPU::LOOP_BEGIN_R_A)
        Stack.push_back({I, true});
      else if (Opc == MiniTPU::LOOP_END) {
        if (Stack.empty())
          report_fatal_error("MiniTPU fold-delays: loop.end without loop.begin");
        Out.push_back({Stack.back().first, I, Stack.back().second});
        Stack.pop_back();
      }
    }
  llvm::sort(Out, [](const LoopSpan &A, const LoopSpan &B) { return A.Begin < B.Begin; });
  return Out;
}

/// Every issue path (asm.issue_paths): each combination of skippable bodies
/// removed, the path skipping every one of them first.
static std::vector<std::vector<int>> pathsOf(const std::vector<Bundle> &Bundles,
                                             const std::vector<LoopSpan> &Loops) {
  SmallVector<const LoopSpan *, 8> Skippable;
  for (const LoopSpan &L : Loops)
    if (L.Skippable)
      Skippable.push_back(&L);
  const int Max = getMiniTPUScheduleFact("MaxSkippableLoops");
  if (int(Skippable.size()) > Max)
    report_fatal_error("MiniTPU fold-delays: more than " + Twine(Max) +
                       " loop.begin.r in one function");
  std::vector<std::vector<int>> Out;
  for (int Mask = (1 << Skippable.size()) - 1; Mask >= 0; --Mask) {
    std::vector<int> Path;
    for (int I = 0, E = Bundles.size(); I < E; ++I) {
      bool Skipped = false;
      for (unsigned Bit = 0; Bit < Skippable.size(); ++Bit)
        if ((Mask >> Bit) & 1 && Skippable[Bit]->Begin < I &&
            I <= Skippable[Bit]->End)
          Skipped = true;
      if (!Skipped)
        Path.push_back(I);
    }
    Out.push_back(Path);
  }
  return Out;
}

namespace {
/// Where the bundle count falls short of S_LAT: insert `Need` literal empty
/// bundles just before bundle `Before`.
struct Shortfall {
  int Before, Need;
};
} // namespace

static void sregsOf(const Bundle &B, const TargetRegisterInfo *TRI,
                    SmallVectorImpl<unsigned> &Reads,
                    SmallVectorImpl<unsigned> &Writes) {
  for (MachineInstr *MI : B.Instrs) {
    miniTPUIssueTimeSRegReads(*MI, TRI, Reads);
    miniTPUSRegWrites(*MI, TRI, Writes);
  }
}

/// asm._check_scalar_read_gap along one path.
static std::optional<Shortfall> alongPath(const std::vector<Bundle> &Bundles,
                                          const std::vector<int> &Path,
                                          const TargetRegisterInfo *TRI,
                                          int SLat) {
  DenseMap<unsigned, int> LastWrite;
  for (int Step = 0, E = Path.size(); Step < E; ++Step) {
    SmallVector<unsigned, 4> Reads, Writes;
    sregsOf(Bundles[Path[Step]], TRI, Reads, Writes);
    if (!Reads.empty()) {
      for (unsigned W : Writes)
        LastWrite[W] = Step;
      for (unsigned R : Reads) {
        auto It = LastWrite.find(R);
        if (It == LastWrite.end() || Step - It->second >= SLat)
          continue;
        if (It->second == Step)
          report_fatal_error("MiniTPU fold-delays: bundle " + Twine(Path[Step]) +
                             " writes s" + Twine(R) +
                             " and reads it at issue; no gap separates them");
        return Shortfall{Path[Step], SLat - (Step - It->second)};
      }
    }
    for (unsigned W : Writes)
      LastWrite[W] = Step;
  }
  return std::nullopt;
}

/// asm._check_scalar_read_gap_across_back_edges for one loop body.
static std::optional<Shortfall> aroundBackEdge(const std::vector<Bundle> &Bundles,
                                               const LoopSpan &L,
                                               const TargetRegisterInfo *TRI,
                                               int SLat) {
  DenseMap<unsigned, int> LastWrite;
  for (int I = L.Begin + 1; I <= L.End; ++I) {
    SmallVector<unsigned, 4> Reads, Writes;
    sregsOf(Bundles[I], TRI, Reads, Writes);
    for (unsigned W : Writes)
      LastWrite[W] = I;
  }
  DenseSet<unsigned> Seen;
  for (int I = L.Begin + 1; I <= L.End; ++I) {
    SmallVector<unsigned, 4> Reads, Writes;
    sregsOf(Bundles[I], TRI, Reads, Writes);
    for (unsigned R : Reads) {
      if (Seen.count(R))
        continue;
      auto It = LastWrite.find(R);
      if (It == LastWrite.end() || It->second < I)
        continue;
      // Tail of this iteration plus head of the next.
      int Distance = (L.End - It->second) + (I - L.Begin);
      if (Distance >= SLat)
        continue;
      // Lengthen the tail (after the write) while the write is not the
      // loop.end itself; otherwise the head (before the reader).
      int Before = It->second < L.End ? It->second + 1 : I;
      return Shortfall{Before, SLat - Distance};
    }
    for (unsigned W : Writes)
      Seen.insert(W);
  }
  return std::nullopt;
}

/// Insert one literal empty bundle before bundle \p Before, paid for by a
/// delay cycle of the bundle before it when it has one.
static void insertLiteral(MachineBasicBlock &MBB, std::vector<Bundle> &Bundles,
                          int Before, const MiniTPUInstrInfo *TII) {
  if (Before > 0) {
    MachineOperand &D = Bundles[Before - 1].Delay->getOperand(0);
    if (D.getImm() > 0)
      D.setImm(D.getImm() - 1);
  }
  MachineBasicBlock::iterator At =
      Before < int(Bundles.size()) ? Bundles[Before].First->getIterator()
                                   : MBB.end();
  BuildMI(MBB, At, DebugLoc(), TII->get(MiniTPU::DELAY)).addImm(0);
}

static bool keepScalarBundles(MachineBasicBlock &MBB,
                              const MiniTPUInstrInfo *TII,
                              const TargetRegisterInfo *TRI) {
  const int SLat = getMiniTPUScheduleFact("ScalarLatencyBundles");
  bool Changed = false;
  for (int Round = 0; Round < 100000; ++Round) {
    std::vector<Bundle> Bundles = bundlesOf(MBB);
    std::vector<LoopSpan> Loops = loopsOf(Bundles);
    std::optional<Shortfall> S;
    for (const std::vector<int> &Path : pathsOf(Bundles, Loops))
      if ((S = alongPath(Bundles, Path, TRI, SLat)))
        break;
    if (!S)
      for (const LoopSpan &L : Loops)
        if ((S = aroundBackEdge(Bundles, L, TRI, SLat)))
          break;
    if (!S)
      return Changed;
    for (int K = 0; K < S->Need; ++K)
      insertLiteral(MBB, Bundles, S->Before, TII);
    Changed = true;
    // Bundles shifted: rebuild.
  }
  report_fatal_error("MiniTPU fold-delays: S_LAT bundles did not settle");
}

bool MiniTPUFoldDelays::runOnMachineFunction(MachineFunction &MF) {
  const auto *TII =
      static_cast<const MiniTPUInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const unsigned MaxDelay = getMiniTPUScheduleFact("MaxDelay");
  const unsigned Nop = TII->getNopOpcode();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    // Reorder mode's pinned-bundle markers are DELAYs from here on.
    for (MachineInstr &MI : MBB.instrs())
      if (MI.getOpcode() == MiniTPU::DELAY_GROUP) {
        MI.removeOperand(0);
        MI.setDesc(TII->get(MiniTPU::DELAY));
        Changed = true;
      }
    MachineInstr *Prev = nullptr; // the DELAY of the last real bundle
    bool PrevIsHalt = false;
    unsigned Run = 0;
    auto Flush = [&](MachineBasicBlock::iterator Before) {
      if (PrevIsHalt) {
        // halt ends the program: AIE pads the region end until nothing is in
        // flight (an unknown successor), which asm.schedule() does not, and
        // a halt word with a delay is not the halt word.
        Run = 0;
        return;
      }
      if (!Run)
        return;
      if (!Prev) {
        // Stalls before the first bundle: a filler at the top.
        Prev = BuildMI(MBB, Before, DebugLoc(), TII->get(MiniTPU::DELAY))
                   .addImm(0);
        --Run;
      }
      unsigned Here = std::min(Run, MaxDelay);
      Prev->getOperand(0).setImm(Here);
      Run -= Here;
      while (Run) {
        // A filler takes one issue cycle itself, then holds the rest.
        unsigned Hold = std::min(Run - 1, MaxDelay);
        Prev = BuildMI(MBB, Before, DebugLoc(), TII->get(MiniTPU::DELAY))
                   .addImm(Hold);
        Run -= Hold + 1;
      }
      Changed = true;
    };
    for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E;) {
      MachineInstr &MI = *I++;
      if (MI.getOpcode() == Nop && !MI.isBundled()) {
        ++Run;
        MI.eraseFromParent();
        continue;
      }
      if (MI.isDebugOrPseudoInstr())
        continue;
      Flush(MI.getIterator());
      MachineInstr *D = MI.isBundle() ? delayOf(MI)
                        : MI.getOpcode() == MiniTPU::DELAY ? &MI
                                                           : nullptr;
      if (!D) {
        // A scheduled bundle without its DELAY: give it one.
        MachineBasicBlock::instr_iterator After =
            MI.isBundle() ? getBundleEnd(MI.getIterator()) : std::next(MI.getIterator());
        D = BuildMI(MBB, After, DebugLoc(), TII->get(MiniTPU::DELAY)).addImm(0);
        D->bundleWithPred();
      }
      Prev = D;
      PrevIsHalt = llvm::any_of(
          make_range(MI.getIterator(), getBundleEnd(MI.getIterator())),
          [](const MachineInstr &I) { return I.getOpcode() == MiniTPU::HALT; });
    }
    Flush(MBB.end());
    if (!miniTPUInOrder())
      Changed |= keepScalarBundles(MBB, TII, TRI);
  }
  return Changed;
}
