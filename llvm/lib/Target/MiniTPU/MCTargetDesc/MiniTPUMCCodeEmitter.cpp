//===-- MiniTPUMCCodeEmitter.cpp - MiniTPU bundle encoder -----------------===//
//
// The AIE base emitter does the work: a bundle is a CodeGenFormat composite
// whose operands are the slot sub-instructions, each encoded standalone and
// placed at the slot offset the generated format tables give. MiniTPU has no
// relocations, so the fixup tables are empty.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/AIEBaseMCCodeEmitter.h"
#include "MiniTPUMCFormats.h"
#include "MiniTPUMCTargetDesc.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"

using namespace llvm;

namespace {
const MiniTPUMCFormats MiniTPUFormats;
const std::map<unsigned, SmallVector<FixupField>> NoFieldsInfos;
const std::map<SmallVector<FixupField>, std::set<unsigned>> NoFieldsMapper;
const std::map<unsigned, unsigned> NoFormatSize;
const std::map<unsigned, FixupFlag> NoFlags;
const std::map<unsigned, FixupFlag> NoInstrFlags;

class MiniTPUMCCodeEmitter : public AIEBaseMCCodeEmitter {
public:
  MiniTPUMCCodeEmitter(MCContext &Ctx, MCInstrInfo const &MCII)
      : AIEBaseMCCodeEmitter(
            Ctx, MCII,
            std::make_unique<const AIEMCFixupKinds>(NoFieldsInfos, NoFieldsMapper,
                                                    NoFormatSize, NoFlags,
                                                    NoInstrFlags),
            MiniTPUFormats) {}

  void getBinaryCodeForInstr(const MCInst &MI, SmallVectorImpl<MCFixup> &Fixups,
                             APInt &Inst, APInt &Scratch,
                             const MCSubtargetInfo &STI) const override;

  /// A pseudo has no encoding: the EXPERIMENTAL ops of docs/isa_experimental.json
  /// and the scheduler's own markers. Refuse them rather than emit their zero
  /// fixed bits, which would be a silent NOP in the slot.
  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override {
    auto Refuse = [&](const MCInst &Sub) {
      if (MCII.get(Sub.getOpcode()).isPseudo())
        report_fatal_error("MiniTPU: " + Twine(MCII.getName(Sub.getOpcode())) +
                               " is a pseudo with no encoding (experimental or "
                               "codegen-only); it cannot be emitted",
                           false);
    };
    Refuse(MI);
    for (const MCOperand &Op : MI)
      if (Op.isInst())
        Refuse(*Op.getInst());
    AIEBaseMCCodeEmitter::encodeInstruction(MI, CB, Fixups, STI);
  }

  /// A count spelled as itself and encoded minus one (descriptor rows).
  void getUImmMinusOneOpValue(const MCInst &MI, unsigned OpNo, APInt &Op,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const {
    Op = static_cast<uint64_t>(MI.getOperand(OpNo).getImm() - 1);
  }
};
} // namespace

MCCodeEmitter *llvm::createMiniTPUMCCodeEmitter(const MCInstrInfo &MCII,
                                                MCContext &Ctx) {
  return new MiniTPUMCCodeEmitter(Ctx, MCII);
}

#include "MiniTPUGenMCCodeEmitter.inc"
