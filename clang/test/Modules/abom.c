// The source files a module was built from are compiled into whatever imports
// it, so they belong in the importing object's Automatic Bill of Materials.
// A module carries the hashes of the contents it was built from, so that what
// is recorded is what was compiled and not whatever its source files hold when
// the module is imported.

// RUN: rm -rf %t && split-file %s %t

// An explicitly built module.
// RUN: %clang_cc1 -fabom -emit-module -fmodules -fmodule-name=M \
// RUN:   %t/module.modulemap -o %t/M.pcm
// RUN: %clang_cc1 -fabom -fmodules -fmodule-file=%t/M.pcm -emit-obj %t/use.c \
// RUN:   -I%t -o %t/use.o
// RUN: llvm-abom-hash %t/modheader.h %t/use.c > %t/hashes
// RUN: llvm-abom-check --hashes %t/hashes %t/use.o | FileCheck %s
// CHECK:      is present
// CHECK-NEXT: is present

// A precompiled header, which carries its hashes the same way. It has to be
// built with -fabom as well, since a precompiled header and its user must
// agree on the option.
// RUN: %clang_cc1 -fabom -emit-pch -x c-header %t/pch.h -o %t/pch.pch
// RUN: %clang_cc1 -fabom -include-pch %t/pch.pch -emit-obj %t/usepch.c \
// RUN:   -o %t/usepch.o
// RUN: llvm-abom-hash %t/pch.h > %t/pchhashes
// RUN: llvm-abom-check --hashes %t/pchhashes %t/usepch.o | FileCheck %s \
// RUN:   --check-prefix=PCH
// PCH: is present

// The hashes travel in the module rather than being recomputed from disk: the
// header is rewritten, keeping its size and modification time, and the hash of
// the contents the module was built from is still the one recorded.
// RUN: cp %t/modheader.h %t/stamp
// RUN: sed -i.bak 's/(void)/(int )/' %t/modheader.h
// RUN: touch -r %t/stamp %t/modheader.h
// RUN: %clang_cc1 -fabom -fmodules -fmodule-file=%t/M.pcm -emit-obj %t/use.c \
// RUN:   -I%t -o %t/stale.o
// RUN: llvm-abom-check --hashes %t/hashes %t/stale.o | FileCheck %s

//--- module.modulemap
module M { header "modheader.h" export * }

//--- modheader.h
void from_module(void);

//--- use.c
#include "modheader.h"
int use(void) { return 0; }

//--- pch.h
void from_pch(void);

//--- usepch.c
int usepch(void) { return 0; }
