//===-- MiniTPUDisassembler.cpp - MiniTPU bundle decoder ------------------===//
//
// Every bundle is 16 bytes, little-endian, decoded as the one composite format;
// each slot field is then decoded by that slot's own table. Unlike AIE2, a slot
// that decodes to nothing fails the whole bundle rather than printing an empty
// instruction, so an unknown encoding is visible.
//
//===----------------------------------------------------------------------===//

#include "Disassembler/AIEBaseDisassembler.h"
#include "Disassembler/AIEDisassemblerPP.h"
#include "MCTargetDesc/MiniTPUMCTargetDesc.h"
#include "TargetInfo/MiniTPUTargetInfo.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Endian.h"

using namespace llvm;

#define DEBUG_TYPE "minitpu-disassembler"

namespace {
class MiniTPUDisassembler : public MCDisassembler {
public:
  MiniTPUDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}
  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};

template <unsigned N>
static DecodeStatus decodeUImmPlusOneOperand(MCInst &Inst, uint64_t Imm,
                                             int64_t Address,
                                             const MCDisassembler *Decoder) {
  Inst.addOperand(MCOperand::createImm(static_cast<int64_t>(Imm) + 1));
  return MCDisassembler::Success;
}
} // namespace

#define GET_SLOT_DECODER_DECLS
#include "MiniTPUGenSlotDecoders.inc"

#include "MiniTPUGenDisassemblerTables.inc"

template <typename InsnType>
static DecodeStatus decodeSlot(const uint8_t *DecoderTable, MCInst &Inst,
                               InsnType &Imm, int64_t Address,
                               const MCDisassembler *Decoder) {
  MCInst *Sub = Decoder->getContext().createMCInst();
  DecodeStatus Result = decodeInstruction(DecoderTable, *Sub, Imm, Address,
                                          Decoder, Decoder->getSubtargetInfo());
  if (Result != MCDisassembler::Success)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createInst(Sub));
  return MCDisassembler::Success;
}

#define GET_SLOT_DECODER_DEFS
#include "MiniTPUGenSlotDecoders.inc"

DecodeStatus MiniTPUDisassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                                 ArrayRef<uint8_t> Bytes,
                                                 uint64_t Address,
                                                 raw_ostream &CS) const {
  if (Bytes.size() < 16) {
    Size = 0;
    return MCDisassembler::Fail;
  }
  Size = 16;
  APInt Insn(128, support::endian::read64le(Bytes.data() + 8));
  Insn <<= 64;
  Insn |= support::endian::read64le(Bytes.data());
  return decodeInstruction(DecoderTableFormats128, MI, Insn, Address, this, STI);
}

static MCDisassembler *createMiniTPUDisassembler(const Target &T,
                                                 const MCSubtargetInfo &STI,
                                                 MCContext &Ctx) {
  return new MiniTPUDisassembler(STI, Ctx);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMiniTPUDisassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheMiniTPUTarget(),
                                         createMiniTPUDisassembler);
}
