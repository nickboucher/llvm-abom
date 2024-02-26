set(LLVM_LINK_COMPONENTS
  Core
  Object
  Option
  Support
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM AbomCheckOpts.inc -gen-opt-parser-defs)
add_public_tablegen_target(AbomCheckOptsTableGen)

add_llvm_tool(llvm-abom-check
  llvm-abom-check.cpp
  DEPENDS
  AbomCheckOptsTableGen
  )

if(LLVM_INSTALL_BINUTILS_SYMLINKS)
  add_llvm_tool_symlink(abom-check llvm-abom-check)
endif()
