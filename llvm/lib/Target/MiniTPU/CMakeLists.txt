# MiniTPU: MC layer only (spike step 1). Reuses the AIE base MC machinery
# (AIEBaseMCCodeEmitter, AIEBaseMCFormats, AIEBaseAsmBackend, AIEBaseAsmParser,
# AIECommonInstPrinter, AIEMCAsmInfo) and CodeGenFormat's InstSlot/InstFormat.
add_llvm_component_group(MiniTPU)

set(LLVM_TARGET_DEFINITIONS MiniTPU.td)
list(APPEND LLVM_TABLEGEN_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/../AIE")
tablegen(LLVM MiniTPUGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM MiniTPUGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM MiniTPUGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM MiniTPUGenFormats.inc -gen-instr-format)
tablegen(LLVM MiniTPUGenInstrInfo.inc -gen-instr-info -base-instrinfo-class AIEBaseInstrInfo)
tablegen(LLVM MiniTPUGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM MiniTPUGenRegisterInfo.inc -gen-register-info -base-registerinfo-class AIEBaseRegisterInfo)
tablegen(LLVM MiniTPUGenSubtargetInfo.inc -gen-subtarget -base-subtargetinfo-class AIEBaseSubtarget)
tablegen(LLVM MiniTPUGenSearchableTables.inc -gen-searchable-tables)
add_public_tablegen_target(MiniTPUCommonTableGen)

# The AIE headers this target includes, and their generated tables.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../AIE ${CMAKE_CURRENT_BINARY_DIR}/../AIE)
# Lets AIEMCFormats.h name this target's slot kinds (see the hook there).
add_compile_definitions([[LLVM_AIE_EXTRA_SLOTKINDS_INC="MiniTPUGenFormats.inc"]])

# CodeGen: just enough to run AIE's post-RA scheduler (postmisched) on MIR.
add_llvm_target(MiniTPUCodeGen
  MiniTPUFoldDelays.cpp
  MiniTPUHazardRecognizer.cpp
  MiniTPUInstrInfo.cpp
  MiniTPURegisterInfo.cpp
  MiniTPUScheduleMutations.cpp
  MiniTPUSubtarget.cpp
  MiniTPUTargetMachine.cpp

  LINK_COMPONENTS
  AIECodeGen
  AIEDesc
  CodeGen
  CodeGenTypes
  Core
  MC
  MiniTPUDesc
  MiniTPUInfo
  SelectionDAG
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  MiniTPU
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(InstPrinter)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
