; Test the Automatic Bill of Materials record emitted for each object file
; format that supports one.

; RUN: llc -mtriple=x86_64-unknown-linux-gnu < %s | FileCheck %s --check-prefix=ELF
; RUN: llc -mtriple=x86_64-apple-darwin < %s | FileCheck %s --check-prefix=MACHO
; RUN: llc -mtriple=x86_64-pc-windows-msvc < %s | FileCheck %s --check-prefix=COFF

; The module carries its bill of materials as metadata, and an object file
; that is compiled from one module holds that record as it is.

; ELF:      .section .abom,"",@progbits
; ELF-NEXT: .ascii "ABOM\001\001\000\377\377\000\000\t\000\000\000\300\237<\277Cx\227\263P"

; MACHO:      .section __ABOM,__abom
; MACHO-NEXT: .ascii "ABOM\001\001\000\377\377\000\000\t\000\000\000\300\237<\277Cx\227\263P"

; COFF:      .section .abom,"drD"
; COFF-NEXT: .ascii "ABOM\001\001\000\377\377\000\000\t\000\000\000\300\237<\277Cx\227\263P"

define i32 @main() {
  ret i32 0
}

!llvm.abom = !{!0}
!0 = !{!"ABOM\01\01\00\FF\FF\00\00\09\00\00\00\C0\9F<\BFCx\97\B3P"}
