set(LLVM_LINK_COMPONENTS
  BinaryFormat
  BitReader
  Core
  Object
  Support
  )

add_llvm_tool(llvm-abom-check
  llvm-abom-check.cpp
  )
