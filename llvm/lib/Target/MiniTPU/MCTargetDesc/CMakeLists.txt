add_llvm_component_library(LLVMMiniTPUDesc
  MiniTPUAsmBackend.cpp
  MiniTPUMCCodeEmitter.cpp
  MiniTPUMCFormats.cpp
  MiniTPUMCTargetDesc.cpp

  LINK_COMPONENTS
  AIEDesc
  MC
  MiniTPUAsmPrinter
  MiniTPUInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  MiniTPU
  )
