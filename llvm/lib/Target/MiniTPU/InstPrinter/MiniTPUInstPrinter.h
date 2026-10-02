//===-- MiniTPUInstPrinter.h - MiniTPU MCInst to text ---------*- C++ -*-===//
#ifndef LLVM_LIB_TARGET_MINITPU_INSTPRINTER_MINITPUINSTPRINTER_H
#define LLVM_LIB_TARGET_MINITPU_INSTPRINTER_MINITPUINSTPRINTER_H

#include "InstPrinter/AIECommonInstPrinter.h"

namespace llvm {
/// A bundle prints as its slots joined by ";" (AIECommonInstPrinter); an
/// immediate always carries its "#", so printed text assembles again.
class MiniTPUInstPrinter final : public AIECommonInstPrinter {
public:
  MiniTPUInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                     const MCRegisterInfo &MRI)
      : AIECommonInstPrinter(MAI, MII, MRI) {}

  void printInst(const MCInst *MI, uint64_t Address, StringRef Annot,
                 const MCSubtargetInfo &STI, raw_ostream &O) override;
  void printRegName(raw_ostream &O, MCRegister Reg) override;
  void printOperand(const MCInst *MI, unsigned OpNo, const MCSubtargetInfo &STI,
                    raw_ostream &O);

  std::pair<const char *, uint64_t> getMnemonic(const MCInst &MI) const override;
  void printInstruction(const MCInst *MI, uint64_t Address,
                        const MCSubtargetInfo &STI, raw_ostream &O) override;
  bool printAliasInstr(const MCInst *MI, uint64_t Address,
                       const MCSubtargetInfo &STI, raw_ostream &O) override {
    return false;
  }
  static const char *getRegisterName(MCRegister Reg);
};
} // namespace llvm
#endif
