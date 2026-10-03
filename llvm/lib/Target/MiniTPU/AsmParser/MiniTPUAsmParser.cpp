//===-- MiniTPUAsmParser.cpp - MiniTPU bundle assembler -------------------===//
//
// Syntax: one bundle per line, slots separated by ";" (AIE's convention). A
// slot left out is filled with its NOP. The shared immediate and the delay are
// slots of their own ("imm #v", "imm.loop #lo, #step, #hi", "delay #n"), so a
// bundle can spell each at most once and a second is refused as an invalid
// bundle. A bundle whose instructions reserve one FuncUnit in the same cycle
// is refused (checkBundleResources): this is how the generated itineraries'
// co-issue claims (ISSUE_PORT_C, ISSUE_IMM, ISSUE_NEXT_STATE) and the single
// VREG write port refuse what asm.py refuses.
//
//===----------------------------------------------------------------------===//

#include "AIEBundle.h"
#include "AsmParser/AIEBaseOperand.h"
#include "MCTargetDesc/MiniTPUMCFormats.h"
#include "MCTargetDesc/MiniTPUMCTargetDesc.h"
#include "TargetInfo/MiniTPUTargetInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

#define DEBUG_TYPE "minitpu-asm-parser"

using namespace llvm;

// The AIE base parser is written to be included after `using namespace llvm`.
#include "AsmParser/AIEBaseAsmParser.h"

static MCRegister MatchRegisterName(StringRef Name);

#define GET_OPERAND_RANGES
#include "MiniTPUGenSlotDecoders.inc"

#define GET_FUNCUNIT_NAMES
#include "MiniTPUGenSlotDecoders.inc"

/// cycle -> FuncUnits an instruction's itinerary reserves then.
static std::map<unsigned, uint64_t> reservations(const InstrItineraryData &II,
                                                 unsigned Opcode,
                                                 const MCInstrInfo &MII) {
  std::map<unsigned, uint64_t> Out;
  unsigned Class = MII.get(Opcode).getSchedClass();
  unsigned Cycle = 0;
  for (const InstrStage *S = II.beginStage(Class), *E = II.endStage(Class);
       S != E; ++S) {
    for (unsigned C = 0; C < S->getCycles(); ++C)
      Out[Cycle + C] |= uint64_t(1) << S->getUnits(); // a unit index (AIE)
    Cycle += S->getNextCycles();
  }
  return Out;
}

namespace {
MiniTPUMCFormats MiniTPUFormatInterface;

class MiniTPUOperand : public AIEBaseOperand {};

class MiniTPUAsmParser
    : public AIEBaseAsmParser<MiniTPUAsmParser, AIE::MCBundle, MiniTPUOperand> {
#define GET_ASSEMBLER_HEADER
#include "MiniTPUGenAsmMatcher.inc"

  bool parseIdentifier(OperandVector &Operands) override {
    MCRegister Reg;
    SMLoc Begin, End;
    if (!parseRegister(Reg, Begin, End)) {
      Operands.push_back(MiniTPUOperand::CreateReg(getContext(), Reg, Begin, End));
      return false;
    }
    return Error(getTok().getLoc(), "operand is not a register");
  }
  /// Refuse a bundle in which two instructions reserve one FuncUnit in one
  /// cycle: two port-C readers, two IMM owners, a descriptor beside a control
  /// op, two writes on one write-port cycle.
  bool checkBundleResources(MCInst &Inst, OperandVector &Operands) {
    InstrItineraryData II = getSTI().getInstrItineraryForCPU(getSTI().getCPU());
    if (II.isEmpty())
      return Error(Operands[0]->getStartLoc(), "no itineraries for the bundle check");
    auto Mine = reservations(II, Inst.getOpcode(), MII);
    for (const MCInst *Other : Bundle.getInstrs()) {
      for (auto [Cycle, Units] : reservations(II, Other->getOpcode(), MII)) {
        auto It = Mine.find(Cycle);
        if (It == Mine.end() || !(It->second & Units))
          continue;
        uint64_t Both = It->second & Units;
        unsigned Bit = llvm::countr_zero(Both);
        // Drop the whole bundle, as AIEBaseAsmParser does for a slot clash.
        while (!(getLexer().is(AsmToken::EndOfStatement) &&
                 (getLexer().getTok().getString().empty() ||
                  getLexer().getTok().getString()[0] == '\n')))
          Lex();
        Bundle.clear();
        return Error(Operands[0]->getStartLoc(),
                     "incorrect bundle: this instruction and " +
                         Twine(MII.getName(Other->getOpcode())) + " both claim " +
                         FuncUnitNames[Bit] + " at +" + Twine(Cycle));
      }
    }
    return false;
  }
  /// Refuse, never truncate, an immediate outside what asm.py's builders accept.
  bool validateInstruction(MCInst &Inst, OperandVector &Operands) override {
    if (checkBundleResources(Inst, Operands))
      return true;
    for (const auto &R : OperandRanges) {
      if (R.Opcode != Inst.getOpcode() || R.OpIdx >= Inst.getNumOperands())
        continue;
      const MCOperand &MO = Inst.getOperand(R.OpIdx);
      if (MO.isImm() && (MO.getImm() < R.Lo || MO.getImm() > R.Hi))
        return Error(Operands[0]->getStartLoc(),
                     "immediate " + Twine(MO.getImm()) + " is outside [" +
                         Twine(R.Lo) + ", " + Twine(R.Hi) + "]");
    }
    return false;
  }
  unsigned matchRegister(std::string Name) override {
    return MatchRegisterName(Name);
  }
  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;


public:
  MiniTPUAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                   const MCInstrInfo &MII, const MCTargetOptions &Options)
      : AIEBaseAsmParser(STI, MII, Options, MiniTPUFormatInterface) {
    setAvailableFeatures(ComputeAvailableFeatures(getSTI().getFeatureBits()));
  }
};
} // namespace

#define GET_MATCHER_IMPLEMENTATION
#define GET_REGISTER_MATCHER
#include "MiniTPUGenAsmMatcher.inc"

bool MiniTPUAsmParser::matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                               OperandVector &Operands,
                                               MCStreamer &Out, uint64_t &ErrorInfo,
                                               bool MatchingInlineAsm) {
  MCInst *Inst = getContext().createMCInst();
  unsigned Result =
      MatchInstructionImpl(Operands, *Inst, ErrorInfo, MatchingInlineAsm);
  SMLoc Loc = IDLoc;
  if (ErrorInfo != ~0ULL && ErrorInfo < Operands.size())
    Loc = static_cast<MiniTPUOperand &>(*Operands[ErrorInfo]).getStartLoc();
  switch (Result) {
  case Match_Success:
    return processMatchedInstruction(IDLoc, Operands, Out, Inst);
  case Match_MnemonicFail:
    return Error(IDLoc, "unrecognized instruction mnemonic");
  case Match_InvalidOperand:
    return Error(Loc, "invalid operand for instruction");
  default:
    return Error(IDLoc, "invalid instruction");
  }
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMiniTPUAsmParser() {
  RegisterMCAsmParser<MiniTPUAsmParser> X(getTheMiniTPUTarget());
}
