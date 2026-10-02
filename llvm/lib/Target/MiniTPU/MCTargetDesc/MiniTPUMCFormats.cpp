//===-- MiniTPUMCFormats.cpp - CodeGenFormat tables for MiniTPU -----------===//
#include "MiniTPUMCFormats.h"
#include "MiniTPUMCTargetDesc.h"

namespace llvm {
#define GET_FORMATS_PACKETS_TABLE
#define GET_FORMATS_SLOTS_DEFS
#define GET_FORMATS_SLOTINFOS_MAPPING
#define GET_OPCODE_FORMATS_INDEX_FUNC
#define GET_ALTERNATE_INST_OPCODE_FUNC
#include "MiniTPUGenFormats.inc"
namespace MiniTPU {
#define GET_FORMATS_FORMATS_DEFS
#include "MiniTPUGenFormats.inc"
} // namespace MiniTPU

const MCFormatDesc *MiniTPUMCFormats::getMCFormats() const {
  return MiniTPU::Formats;
}
const PacketFormats &MiniTPUMCFormats::getPacketFormats() const {
  return Formats;
}
ArrayRef<bool> MiniTPUMCFormats::getIsFormatAvailable() const {
  return FormatAvailable;
}
} // namespace llvm
