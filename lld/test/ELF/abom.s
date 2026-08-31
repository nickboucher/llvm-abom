# REQUIRES: x86
## The bills of materials of the inputs are merged into a single record, which
## describes the source files of every input.

# RUN: rm -rf %t && split-file %s %t
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/a.s -o %t/a.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/b.s -o %t/b.o
# RUN: ld.lld %t/a.o %t/b.o -o %t/out

# RUN: llvm-abom-check %t/out 5881092dd 123456789 | FileCheck %s
# RUN: not llvm-abom-check %t/out fffffffff | FileCheck %s --check-prefix=ABSENT

# CHECK:      5881092dd is present
# CHECK-NEXT: 123456789 is present
# ABSENT:     fffffffff is absent

## The output holds exactly one bill of materials, which is not part of the
## load image.
# RUN: llvm-readelf -S %t/out | FileCheck %s --check-prefix=SECTION
# SECTION:     .abom PROGBITS [[#%x,]] [[#%x,]] [[#%x,]] 00 0
# SECTION-NOT: .abom

## A relocatable output carries a bill of materials of its own, so that it can
## be read like any other artifact.
# RUN: ld.lld -r %t/a.o %t/b.o -o %t/rel.o
# RUN: llvm-abom-check %t/rel.o 5881092dd 123456789 | FileCheck %s
# RUN: llvm-abom-check --summary %t/rel.o | FileCheck %s --check-prefix=ONE
# ONE: 1 Bloom filter

## A malformed record fails the link: merging past it would produce a bill of
## materials that looks sound but has silently forgotten source files.
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/bad.s -o %t/bad.o
# RUN: not ld.lld %t/a.o %t/bad.o -o %t/out2 2>&1 | FileCheck %s --check-prefix=BAD
# BAD: error: {{.*}}bad.o:(.abom): invalid ABOM magic word

## So does a record whose payload has been corrupted.
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/truncated.s -o %t/truncated.o
# RUN: not ld.lld %t/truncated.o -o %t/out3 2>&1 | FileCheck %s --check-prefix=CORRUPT
# CORRUPT: error: {{.*}}truncated.o:(.abom): ABOM payload is corrupt

## A record that claims more Bloom filters than its payload could hold is
## rejected rather than decoded.
# RUN: llvm-mc -filetype=obj -triple=x86_64 %t/bomb.s -o %t/bomb.o
# RUN: not ld.lld %t/bomb.o -o %t/out4 2>&1 | FileCheck %s --check-prefix=BOMB
# BOMB: error: {{.*}}bomb.o:(.abom): ABOM record declares 65535 Bloom filters in 0 bytes

#--- a.s
.globl _start
_start:
  ret

.section .abom,"",@progbits
## An ABOM holding 5881092dd.
.byte 0x41, 0x42, 0x4f, 0x4d, 0x01, 0x01, 0x00, 0xff, 0x7f, 0x00, 0x00, 0x05
.byte 0x00, 0x00, 0x00, 0x90, 0x3d, 0x45, 0x33, 0xe8

#--- b.s
.section .abom,"",@progbits
## An ABOM holding 123456789.
.byte 0x41, 0x42, 0x4f, 0x4d, 0x01, 0x01, 0x00, 0xff, 0x7f, 0x00, 0x00, 0x05
.byte 0x00, 0x00, 0x00, 0xc0, 0x9e, 0xeb, 0x5a, 0x2e

#--- bad.s
.section .abom,"",@progbits
## A record that is not an ABOM at all.
.ascii "NOPE"
.byte 1
.short 1
.long 0
.long 5
.byte 0x00, 0x00, 0x00, 0x00, 0x00

#--- truncated.s
## An ABOM holding 5881092dd, with the last byte of the payload removed and
## the payload length adjusted to match.
.section .abom,"",@progbits
.byte 0x41, 0x42, 0x4f, 0x4d, 0x01, 0x01, 0x00, 0xff, 0x7f, 0x00, 0x00, 0x04
.byte 0x00, 0x00, 0x00, 0x90, 0x3d, 0x45, 0x33

#--- bomb.s
.section .abom,"",@progbits
.ascii "ABOM"
.byte 1
.short 65535
.long 0
.long 0
