set(LLVM_LINK_COMPONENTS
  Core
  Object
  Option
  Support
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM AbomHashOpts.inc -gen-opt-parser-defs)
add_public_tablegen_target(AbomHashOptsTableGen)

add_llvm_tool(llvm-abom-hash
  llvm-abom-hash.cpp
  DEPENDS
  AbomHashOptsTableGen
  )

if(LLVM_INSTALL_BINUTILS_SYMLINKS)
  add_llvm_tool_symlink(abom-hash llvm-abom-hash)
endif()
