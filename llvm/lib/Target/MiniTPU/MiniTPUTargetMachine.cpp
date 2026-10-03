//===-- MiniTPUTargetMachine.cpp - MiniTPU target machine -------*- C++ -*-===//
//
// Spike step 2: enough CodeGen to run AIE's post-RA scheduling strategy
// (AIEPostRASchedStrategy, AIEHazardRecognizer, InterBlockScheduling) on MIR:
//   llc -mtriple=minitpu -run-pass=postmisched,minitpu-fold-delays in.mir
//
//===----------------------------------------------------------------------===//

#include "MiniTPUTargetMachine.h"
#include "AIEMachineScheduler.h"
#include "MiniTPU.h"
#include "TargetInfo/MiniTPUTargetInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool> InOrder(
    "minitpu-in-order", cl::init(true),
    cl::desc("MiniTPU: keep program order and the placed bundles; the "
             "scheduler only inserts stall cycles (gate G3)"));

bool llvm::miniTPUInOrder() { return InOrder; }

/// AIE keeps its scheduling policies in global cl::opts, not in target hooks.
/// Set the ones MiniTPU needs unless the command line already did.
template <typename T> static void setDefault(StringRef Name, T Value) {
  auto &Opts = cl::getRegisteredOptions();
  auto It = Opts.find(Name);
  if (It == Opts.end())
    report_fatal_error("MiniTPU: AIE option -" + Name + " not found");
  auto *Opt = static_cast<cl::opt<T> *>(It->second);
  if (Opt->getNumOccurrences() == 0)
    Opt->setValue(Value);
}

MiniTPUTargetMachine::MiniTPUTargetMachine(const Target &T, const Triple &TT,
                                           StringRef CPU, StringRef FS,
                                           const TargetOptions &Options,
                                           std::optional<Reloc::Model> RM,
                                           std::optional<CodeModel::Model> CM,
                                           CodeGenOptLevel OL, bool JIT)
    : AIEBaseTargetMachine(T, TT, CPU, FS, Options, RM, CM, OL, JIT),
      Subtarget(TT, CPU, FS, *this) {
  // Top-down only: asm.schedule() raises the delay of the bundle BEFORE the one
  // that must wait. Bottom-up would put the same stall after the producer.
  setDefault<unsigned>("aie-bottomup-cycles", 0);
  // A bundle issues one instruction a slot.
  setDefault<int>("issue-limit", Subtarget.getSchedModel().IssueWidth);
  // halt ends the program: there is no successor whose resources could
  // collide, so the region end needs no drained pipeline.
  setDefault<bool>("aie-interblock-scoreboard", false);
}

ScheduleDAGInstrs *
MiniTPUTargetMachine::createPostMachineScheduler(MachineSchedContext *C) const {
  ScheduleDAGMI *DAG =
      new AIEScheduleDAGMI(C, std::make_unique<AIEPostRASchedStrategy>(C),
                           /*RemoveKillFlags=*/true);
  for (auto &M : AIEBaseSubtarget::getPostRAMutationsImpl(getTargetTriple(),
                                                          C->AA))
    DAG->addMutation(std::move(M));
  DAG->addMutation(createMiniTPUControlSinkExit());
  DAG->addMutation(createMiniTPUMatrixEdges());
  if (miniTPUInOrder())
    DAG->addMutation(createMiniTPUInOrderBundles());
  return DAG;
}

TargetPassConfig *MiniTPUTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new TargetPassConfig(*this, PM);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMiniTPUTarget() {
  RegisterTargetMachine<MiniTPUTargetMachine> X(getTheMiniTPUTarget());
  initializeMiniTPUFoldDelaysPass(*PassRegistry::getPassRegistry());
}
