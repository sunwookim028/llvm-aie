add_llvm_component_library(LLVMMiniTPUDisassembler
  MiniTPUDisassembler.cpp

  LINK_COMPONENTS
  MCDisassembler
  MC
  MiniTPUDesc
  MiniTPUInfo
  Support

  ADD_TO_COMPONENT
  MiniTPU
  )
