add_llvm_component_library(LLVMMiniTPUAsmParser
  MiniTPUAsmParser.cpp

  LINK_COMPONENTS
  AIEAsmParser
  AIEDesc
  MC
  MCParser
  MiniTPUDesc
  MiniTPUInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  MiniTPU
  )
