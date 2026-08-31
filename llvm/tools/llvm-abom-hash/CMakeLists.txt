set(LLVM_LINK_COMPONENTS
  BinaryFormat
  Support
  )

add_llvm_tool(llvm-abom-hash
  llvm-abom-hash.cpp
  )
