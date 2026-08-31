// Check that the source files of a translation unit, including its headers,
// are recorded in the Automatic Bill of Materials of the object file.

// RUN: rm -rf %t && mkdir %t
// RUN: printf 'void abom_header(void);' > %t/abom.h

// The module carries the bill of materials as metadata, in the form the
// backend writes to the object file.
// RUN: %clang_cc1 -fabom -emit-llvm -I%t %s -o - | FileCheck %s
// CHECK: !llvm.abom = !{![[#MD:]]}
// CHECK: ![[#MD]] = !{!"ABOM{{.*}}"}

// RUN: %clang_cc1 -emit-llvm -I%t %s -o - | FileCheck %s --check-prefix=NOABOM
// NOABOM-NOT: !llvm.abom

// It records this file and the header it includes.
// RUN: %clang_cc1 -fabom -triple x86_64-unknown-linux-gnu -emit-obj -I%t %s \
// RUN:   -o %t/abom.o
// RUN: llvm-abom-hash %s %t/abom.h > %t/hashes
// RUN: llvm-abom-check --hashes %t/hashes %t/abom.o | FileCheck %s \
// RUN:   --check-prefix=PRESENT
// PRESENT:      is present
// PRESENT-NEXT: is present

// RUN: not llvm-abom-check %t/abom.o 000000000 | FileCheck %s \
// RUN:   --check-prefix=ABSENT
// ABSENT: 000000000 is absent

#include "abom.h"

int main(void) { return 0; }
