# REQUIRES: x86
## The bills of materials of the inputs are merged into a single record.

# RUN: rm -rf %t && split-file %s %t
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/a.s -o %t/a.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/b.s -o %t/b.obj
# RUN: lld-link -entry:main -subsystem:console %t/a.obj %t/b.obj -out:%t/out.exe

# RUN: llvm-abom-check %t/out.exe 5881092dd 123456789 | FileCheck %s
# RUN: not llvm-abom-check %t/out.exe fffffffff | FileCheck %s --check-prefix=ABSENT

# CHECK:      5881092dd is present
# CHECK-NEXT: 123456789 is present
# ABSENT:     fffffffff is absent

## A malformed record fails the link: merging past it would produce a bill of
## materials that looks sound but has silently forgotten source files.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/bad.s -o %t/bad.obj
# RUN: not lld-link -entry:main -subsystem:console %t/a.obj %t/bad.obj \
# RUN:   -out:%t/bad.exe 2>&1 | FileCheck %s --check-prefix=BAD
# BAD: error: {{.*}}bad.obj: invalid ABOM magic word

## The output holds exactly one bill of materials, which is discardable.
# RUN: llvm-readobj --sections %t/out.exe | FileCheck %s --check-prefix=SECTION
# SECTION:     Name: .abom
# SECTION:     IMAGE_SCN_MEM_DISCARDABLE
# SECTION-NOT: Name: .abom

#--- a.s
.globl main
main:
  retq

.section .abom,"drD"
## An ABOM holding 5881092dd.
.byte 0x41, 0x42, 0x4f, 0x4d, 0x01, 0x01, 0x00, 0xff, 0x7f, 0x00, 0x00, 0x05
.byte 0x00, 0x00, 0x00, 0x90, 0x3d, 0x45, 0x33, 0xe8

#--- bad.s
.section .abom,"drD"
## A record that is not an ABOM at all.
.ascii "NOPE"
.byte 1
.short 1
.long 0
.long 5
.byte 0x00, 0x00, 0x00, 0x00, 0x00

#--- b.s
.section .abom,"drD"
## An ABOM holding 123456789.
.byte 0x41, 0x42, 0x4f, 0x4d, 0x01, 0x01, 0x00, 0xff, 0x7f, 0x00, 0x00, 0x05
.byte 0x00, 0x00, 0x00, 0xc0, 0x9e, 0xeb, 0x5a, 0x2e
