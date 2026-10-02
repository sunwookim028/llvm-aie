//===-- MiniTPUMCFormats.h - CodeGenFormat tables for MiniTPU -*- C++ -*-===//
#ifndef LLVM_LIB_TARGET_MINITPU_MCTARGETDESC_MINITPUMCFORMATS_H
#define LLVM_LIB_TARGET_MINITPU_MCTARGETDESC_MINITPUMCFORMATS_H

#include "MCTargetDesc/AIEMCFormats.h"

namespace llvm {
class MiniTPUMCFormats : public AIEBaseMCFormats {
public:
  const std::vector<unsigned int> *
  getAlternateInstsOpcode(unsigned int Opcode) const override;
  std::optional<unsigned int>
  getFormatDescIndex(unsigned int Opcode) const override;
  const MCSlotInfo *getSlotInfo(const MCSlotKind Kind) const override;
  const MCFormatDesc *getMCFormats() const override;
  const PacketFormats &getPacketFormats() const override;
  ArrayRef<bool> getIsFormatAvailable() const override;
};
} // namespace llvm
#endif
