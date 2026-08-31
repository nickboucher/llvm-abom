## The bills of materials of the inputs are merged into a single record.

# RUN: rm -rf %t && split-file %s %t
# RUN: llvm-mc -filetype=obj -triple=wasm32-unknown-unknown %t/a.s -o %t/a.o
# RUN: llvm-mc -filetype=obj -triple=wasm32-unknown-unknown %t/b.s -o %t/b.o
# RUN: wasm-ld -o %t/out.wasm %t/a.o %t/b.o

# RUN: llvm-abom-check %t/out.wasm 5881092dd 123456789 | FileCheck %s
# RUN: not llvm-abom-check %t/out.wasm fffffffff | FileCheck %s --check-prefix=ABSENT

# CHECK:      5881092dd is present
# CHECK-NEXT: 123456789 is present
# ABSENT:     fffffffff is absent

## A malformed record fails the link: merging past it would produce a bill of
## materials that looks sound but has silently forgotten source files.
# RUN: llvm-mc -filetype=obj -triple=wasm32-unknown-unknown %t/bad.s -o %t/bad.o
# RUN: not wasm-ld -o %t/bad.wasm %t/a.o %t/bad.o 2>&1 | FileCheck %s --check-prefix=BAD
# BAD: error: {{.*}}bad.o: invalid ABOM magic word

## The output holds exactly one bill of materials.
# RUN: obj2yaml %t/out.wasm | FileCheck %s --check-prefix=SECTION
# SECTION:     Name: abom
# SECTION-NOT: Name: abom

#--- a.s
.globl _start
_start:
  .functype _start () -> ()
  end_function

.section .custom_section.abom,"",@
## An ABOM holding 5881092dd.
.byte 0x41, 0x42, 0x4f, 0x4d, 0x01, 0x01, 0x00, 0xff, 0x7f, 0x00, 0x00, 0x05
.byte 0x00, 0x00, 0x00, 0x90, 0x3d, 0x45, 0x33, 0xe8

#--- bad.s
.section .custom_section.abom,"",@
## A record that is not an ABOM at all.
.ascii "NOPE"
.byte 1
.short 1
.long 0
.long 5
.byte 0x00, 0x00, 0x00, 0x00, 0x00

#--- b.s
.section .custom_section.abom,"",@
## An ABOM holding 123456789.
.byte 0x41, 0x42, 0x4f, 0x4d, 0x01, 0x01, 0x00, 0xff, 0x7f, 0x00, 0x00, 0x05
.byte 0x00, 0x00, 0x00, 0xc0, 0x9e, 0xeb, 0x5a, 0x2e
