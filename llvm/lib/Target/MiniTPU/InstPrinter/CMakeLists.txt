add_llvm_component_library(LLVMMiniTPUAsmPrinter
  MiniTPUInstPrinter.cpp

  LINK_COMPONENTS
  AIEAsmPrinter
  MC
  Support

  ADD_TO_COMPONENT
  MiniTPU
  )
