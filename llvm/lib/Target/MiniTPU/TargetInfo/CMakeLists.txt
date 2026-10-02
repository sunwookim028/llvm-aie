add_llvm_component_library(LLVMMiniTPUInfo
  MiniTPUTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  MiniTPU
  )
