// RUN: %clang -### -c -fabom %s 2>&1 | FileCheck %s
// RUN: %clang -### -c -fabom -fno-abom %s 2>&1 | FileCheck %s --check-prefix=NOABOM
// RUN: %clang -### -c %s 2>&1 | FileCheck %s --check-prefix=NOABOM

// CHECK: "-fabom"
// NOABOM-NOT: "-fabom"
