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
//===----------------------------------------------------------------------===//
#include "MCTargetDesc/MiniTPUMCTargetDesc.h"
#include "MiniTPU.h"
#include "MiniTPUInstrInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
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

bool MiniTPUFoldDelays::runOnMachineFunction(MachineFunction &MF) {
  const auto *TII =
      static_cast<const MiniTPUInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const unsigned MaxDelay = getMiniTPUScheduleFact("MaxDelay");
  const unsigned Nop = TII->getNopOpcode();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    MachineInstr *Prev = nullptr; // the DELAY of the last real bundle
    unsigned Run = 0;
    auto Flush = [&](MachineBasicBlock::iterator Before) {
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
    }
    Flush(MBB.end());
  }
  return Changed;
}
