//===-- MiniTPUHazardRecognizer.cpp - MiniTPU hazards -----------*- C++ -*-===//
#include "MiniTPUHazardRecognizer.h"
#include "MiniTPU.h"
#include "MiniTPUInstrInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

static cl::opt<bool> SLatAllReaders(
    "minitpu-slat-all-readers", cl::init(false),
    cl::desc("MiniTPU: hold every SREG reader to S_LAT, S ops included "
             "(review finding M6-F2), not only descriptors and loop.begin.r "
             "as asm.py does"));

bool llvm::miniTPUSLatAllReaders() { return SLatAllReaders; }

static bool isIssueTimeSRegReader(unsigned Opc) {
  switch (Opc) {
  case MiniTPU::VMEMLD:
  case MiniTPU::VMEMLD_D:
  case MiniTPU::VMEMST:
  case MiniTPU::VMEMST_D:
  case MiniTPU::LOOP_BEGIN_R_S:
    return true;
  default:
    return false;
  }
}

static bool isSOpReader(unsigned Opc) {
  return Opc == MiniTPU::SADDI || Opc == MiniTPU::SMAC || Opc == MiniTPU::SSHL;
}

void llvm::miniTPUIssueTimeSRegReads(const MachineInstr &MI,
                                     const TargetRegisterInfo *TRI,
                                     SmallVectorImpl<unsigned> &Regs) {
  if (!isIssueTimeSRegReader(MI.getOpcode()) &&
      !(SLatAllReaders && isSOpReader(MI.getOpcode())))
    return;
  for (const MachineOperand &MO : MI.operands())
    if (MO.isReg() && MO.isUse() && !MO.isImplicit() &&
        MiniTPU::SREGRegClass.contains(MO.getReg()))
      Regs.push_back(TRI->getEncodingValue(MO.getReg()));
}

void llvm::miniTPUSRegWrites(const MachineInstr &MI,
                             const TargetRegisterInfo *TRI,
                             SmallVectorImpl<unsigned> &Regs) {
  for (const MachineOperand &MO : MI.operands())
    if (MO.isReg() && MO.isDef() && MiniTPU::SREGRegClass.contains(MO.getReg()))
      Regs.push_back(TRI->getEncodingValue(MO.getReg()));
}

MiniTPUHazardRecognizer::MiniTPUHazardRecognizer(const AIEBaseInstrInfo *TII,
                                                 const InstrItineraryData *II,
                                                 AIEAlternateDescriptors &Alt,
                                                 const ScheduleDAGMI *DAG)
    : AIEHazardRecognizer(TII, II, Alt, /*IsPreRA=*/false), DAG(DAG),
      TheTII(TII), Itins(II), AltDescs(Alt) {}

MiniTPUBundleGroups &MiniTPUHazardRecognizer::groups() {
  if (!Groups) {
    Groups = std::make_unique<MiniTPUBundleGroups>(
        const_cast<std::vector<SUnit> &>(DAG->SUnits), miniTPUInOrder());
    for (auto &G : Groups->Groups)
      checkBundleAlone(G);
  }
  return *Groups;
}

void MiniTPUHazardRecognizer::Reset() {
  AIEHazardRecognizer::Reset();
  Groups.reset();
  LastSWrite.clear();
  GroupsIssued = 0;
  Cycle = 0;
  LastSWriteCycle.clear();
}

void MiniTPUHazardRecognizer::AdvanceCycle() {
  AIEHazardRecognizer::AdvanceCycle();
  ++Cycle;
}

void MiniTPUHazardRecognizer::RecedeCycle() {
  AIEHazardRecognizer::RecedeCycle();
  --Cycle;
}

void MiniTPUHazardRecognizer::checkBundleAlone(const std::vector<SUnit *> &G) {
  AIEHazardRecognizer Empty(TheTII, Itins, AltDescs, /*IsPreRA=*/false);
  for (SUnit *SU : G) {
    if (Empty.AIEHazardRecognizer::getHazardType(SU, 0) != NoHazard) {
      std::string S;
      raw_string_ostream OS(S);
      OS << *SU->getInstr();
      report_fatal_error("MiniTPU schedule refused: a bundle's own "
                         "instructions claim one resource twice, at " +
                             Twine(OS.str()),
                         false);
    }
    Empty.AIEHazardRecognizer::EmitInstruction(SU, 0);
  }
  if (miniTPUInOrder())
    return;
  // Reorder mode: a write and an issue-time read of one SREG in one pinned
  // bundle; in-order mode finds it through checkScalarLatency at distance 0.
  SmallVector<unsigned, 4> Writes, Reads;
  for (SUnit *SU : G) {
    miniTPUSRegWrites(*SU->getInstr(), DAG->TRI, Writes);
    miniTPUIssueTimeSRegReads(*SU->getInstr(), DAG->TRI, Reads);
  }
  for (unsigned R : Reads)
    if (llvm::is_contained(Writes, R))
      report_fatal_error("MiniTPU schedule refused: S_LAT: one bundle writes "
                         "s" + Twine(R) + " and reads it at issue; no gap "
                         "separates them (asm.py counts a same-bundle write as "
                         "distance 0)",
                         false);
}

void MiniTPUHazardRecognizer::checkScalarLatency(const std::vector<SUnit *> &G,
                                                 int Index) {
  const int SLat = getMiniTPUScheduleFact("ScalarLatencyBundles");
  const TargetRegisterInfo *TRI = DAG->TRI;
  // A write in the reading bundle counts as distance 0 (asm.py's rule).
  DenseMap<unsigned, int> Writes = LastSWrite;
  for (SUnit *SU : G)
    for (const MachineOperand &MO : SU->getInstr()->operands())
      if (MO.isReg() && MO.isDef() && MiniTPU::SREGRegClass.contains(MO.getReg()))
        Writes[TRI->getEncodingValue(MO.getReg())] = Index;
  for (SUnit *SU : G) {
    const MachineInstr &MI = *SU->getInstr();
    SmallVector<unsigned, 4> Reads;
    miniTPUIssueTimeSRegReads(MI, TRI, Reads);
    for (unsigned R : Reads) {
      auto It = Writes.find(R);
      if (It != Writes.end() && Index - It->second < SLat) {
        std::string S;
        raw_string_ostream OS(S);
        OS << MI;
        report_fatal_error(
            "MiniTPU schedule refused: S_LAT: " + Twine(OS.str()) + " reads s" +
                Twine(R) + " " + Twine(Index - It->second) +
                " bundle(s) after its write; it needs " + Twine(SLat) +
                ", and a stall cycle is a delay, not a bundle",
            false);
      }
    }
  }
}

bool MiniTPUHazardRecognizer::scalarHazard(const SUnit *SU, int AtCycle) const {
  const int SLat = getMiniTPUScheduleFact("ScalarLatencyBundles");
  SmallVector<unsigned, 4> Reads;
  miniTPUIssueTimeSRegReads(*SU->getInstr(), DAG->TRI, Reads);
  for (unsigned R : Reads) {
    auto It = LastSWriteCycle.find(R);
    if (It != LastSWriteCycle.end() && AtCycle - It->second < SLat)
      return true;
  }
  return false;
}

ScheduleHazardRecognizer::HazardType
MiniTPUHazardRecognizer::getHazardType(SUnit *SU, int DeltaCycles) {
  MiniTPUBundleGroups &BG = groups();
  if (!BG.isLeader(SU)) {
    HazardType H = AIEHazardRecognizer::getHazardType(SU, DeltaCycles);
    if (H != NoHazard || miniTPUInOrder() || BG.isGrouped(SU))
      return H;
    return scalarHazard(SU, Cycle + DeltaCycles) ? Hazard : NoHazard;
  }
  // The whole bundle must fit this cycle, or none of it issues here.
  AIEHazardRecognizer Trial(*this);
  for (SUnit *M : BG.Groups[BG.GroupOf[SU]]) {
    if (Trial.AIEHazardRecognizer::getHazardType(M, DeltaCycles) != NoHazard)
      return NoopHazard;
    Trial.AIEHazardRecognizer::EmitInstruction(M, DeltaCycles);
    if (!miniTPUInOrder() && scalarHazard(M, Cycle + DeltaCycles))
      return Hazard;
  }
  return NoHazard;
}

void MiniTPUHazardRecognizer::EmitInstruction(SUnit *SU, int DeltaCycles) {
  AIEHazardRecognizer::EmitInstruction(SU, DeltaCycles);
  if (!miniTPUInOrder()) {
    SmallVector<unsigned, 4> Writes;
    miniTPUSRegWrites(*SU->getInstr(), DAG->TRI, Writes);
    for (unsigned W : Writes) {
      int &Last = LastSWriteCycle[W];
      Last = std::max(Last, Cycle + DeltaCycles);
    }
    return;
  }
  MiniTPUBundleGroups &BG = groups();
  if (!BG.isLeader(SU))
    return;
  // Bundles issue in program order, one a cycle: this is bundle GroupsIssued.
  const auto &G = BG.Groups[BG.GroupOf[SU]];
  checkScalarLatency(G, GroupsIssued);
  for (SUnit *M : G)
    for (const MachineOperand &MO : M->getInstr()->operands())
      if (MO.isReg() && MO.isDef() && MiniTPU::SREGRegClass.contains(MO.getReg()))
        LastSWrite[DAG->TRI->getEncodingValue(MO.getReg())] = GroupsIssued;
  ++GroupsIssued;
}
