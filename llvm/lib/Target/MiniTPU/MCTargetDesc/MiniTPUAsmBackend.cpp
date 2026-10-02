//===-- MiniTPUAsmBackend.cpp - MiniTPU assembler backend -----------------===//
#include "MCTargetDesc/AIEBaseAsmBackend.h"
#include "MiniTPUMCTargetDesc.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
class MiniTPUAsmBackend : public AIEBaseAsmBackend {
public:
  using AIEBaseAsmBackend::AIEBaseAsmBackend;
  /// An all-zero bundle issues nothing in any slot and holds issue for 0 cycles.
  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override {
    if (Count % 16)
      return false;
    OS.write_zeros(Count);
    return true;
  }
};
} // namespace

MCAsmBackend *llvm::createMiniTPUAsmBackend(const Target &T,
                                            const MCSubtargetInfo &STI,
                                            const MCRegisterInfo &MRI,
                                            const MCTargetOptions &Options) {
  uint8_t OSABI = MCELFObjectTargetWriter::getOSABI(STI.getTargetTriple().getOS());
  return new MiniTPUAsmBackend(STI, OSABI, Options);
}
