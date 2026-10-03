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

MiniTPUHazardRecognizer::MiniTPUHazardRecognizer(const AIEBaseInstrInfo *TII,
                                                 const InstrItineraryData *II,
                                                 AIEAlternateDescriptors &Alt,
                                                 const ScheduleDAGMI *DAG)
    : AIEHazardRecognizer(TII, II, Alt, /*IsPreRA=*/false), DAG(DAG),
      TheTII(TII), Itins(II), AltDescs(Alt) {}

MiniTPUBundleGroups &MiniTPUHazardRecognizer::groups() {
  if (!Groups) {
    Groups = std::make_unique<MiniTPUBundleGroups>(
        const_cast<std::vector<SUnit> &>(DAG->SUnits));
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
}

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
    const bool SOpReader = MI.getOpcode() == MiniTPU::SADDI ||
                           MI.getOpcode() == MiniTPU::SMAC ||
                           MI.getOpcode() == MiniTPU::SSHL;
    if (!isIssueTimeSRegReader(MI.getOpcode()) && !(SLatAllReaders && SOpReader))
      continue;
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || !MO.isUse() || MO.isImplicit() ||
          !MiniTPU::SREGRegClass.contains(MO.getReg()))
        continue;
      auto It = Writes.find(TRI->getEncodingValue(MO.getReg()));
      if (It != Writes.end() && Index - It->second < SLat) {
        std::string S;
        raw_string_ostream OS(S);
        OS << MI;
        report_fatal_error(
            "MiniTPU schedule refused: S_LAT: " + Twine(OS.str()) + " reads " +
                TRI->getName(MO.getReg()) + " " + Twine(Index - It->second) +
                " bundle(s) after its write; it needs " + Twine(SLat) +
                ", and a stall cycle is a delay, not a bundle",
            false);
      }
    }
  }
}

ScheduleHazardRecognizer::HazardType
MiniTPUHazardRecognizer::getHazardType(SUnit *SU, int DeltaCycles) {
  if (!miniTPUInOrder())
    return AIEHazardRecognizer::getHazardType(SU, DeltaCycles);
  MiniTPUBundleGroups &BG = groups();
  if (!BG.isLeader(SU))
    return AIEHazardRecognizer::getHazardType(SU, DeltaCycles);
  // The whole bundle must fit this cycle, or none of it issues here.
  AIEHazardRecognizer Trial(*this);
  for (SUnit *M : BG.Groups[BG.GroupOf[SU]]) {
    if (Trial.AIEHazardRecognizer::getHazardType(M, DeltaCycles) != NoHazard)
      return NoopHazard;
    Trial.AIEHazardRecognizer::EmitInstruction(M, DeltaCycles);
  }
  return NoHazard;
}

void MiniTPUHazardRecognizer::EmitInstruction(SUnit *SU, int DeltaCycles) {
  AIEHazardRecognizer::EmitInstruction(SU, DeltaCycles);
  if (!miniTPUInOrder())
    return;
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
