//===-- MiniTPUInstPrinter.cpp - MiniTPU MCInst to text -------------------===//
#include "MiniTPUInstPrinter.h"
#include "MCTargetDesc/MiniTPUMCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"

using namespace llvm;

#define DEBUG_TYPE "minitpu-asm-printer"

#include "MiniTPUGenAsmWriter.inc"

void MiniTPUInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                   StringRef Annot, const MCSubtargetInfo &STI,
                                   raw_ostream &O) {
  AIECommonInstPrinter::printInstr(MI, Address, Annot, STI, O);
}

void MiniTPUInstPrinter::printRegName(raw_ostream &O, MCRegister Reg) {
  O << getRegisterName(Reg);
}

void MiniTPUInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                      const MCSubtargetInfo &STI, raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);
  if (MO.isReg()) {
    printRegName(O, MO.getReg());
    return;
  }
  if (MO.isImm()) {
    O << "#" << MO.getImm();
    return;
  }
  MAI.printExpr(O, *MO.getExpr());
}
