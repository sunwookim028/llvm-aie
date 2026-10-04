//===-- MiniTPU.td - MiniTPU target, MC layer (spike) ------*- tablegen -*-===//
//
// Hand-written glue only. Every register, slot, field, opcode and latency is in
// the MiniTPUGen*.td files, which tools/gen_isa_td.py in the MiniTPU repository
// writes from that repository's ISA sources and checks with --check.
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"
include "llvm/TableGen/SearchableTable.td"
include "llvm/Target/AIETarget.td"
include "llvm/Target/CodeGenFormat.td"
include "AIEBaseInstrInfo.td"
include "AIEBaseInstrFormats.td"

include "MiniTPUGenRegisterInfo.td"
include "MiniTPUGenSchedule.td"
include "MiniTPUGenSlots.td"
include "MiniTPUGenInstrInfo.td"
include "MiniTPUPseudos.td"

def : ProcessorModel<"minitpu", MiniTPUSchedModel, []>;

def MiniTPUInstrInfo : InstrInfo;
def MiniTPUAsmParser : AsmParser;
def MiniTPUAsmWriter : AsmWriter { int PassSubtarget = 1; }
def MiniTPUCodeGenFormat : CodeGenFormat;

def MiniTPU : Target {
  let InstructionSet = MiniTPUInstrInfo;
  let AssemblyParsers = [MiniTPUAsmParser];
  let AssemblyWriters = [MiniTPUAsmWriter];
}
