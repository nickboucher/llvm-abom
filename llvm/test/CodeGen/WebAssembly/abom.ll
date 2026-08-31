; Test the Automatic Bill of Materials custom section.

; RUN: llc -mtriple=wasm32-unknown-unknown < %s | FileCheck %s

; CHECK:      .section .custom_section.abom,"",@
; CHECK-NEXT: .ascii "ABOM\001\001\000\377\177\000\000\005\000\000\000\220=E3\350"

define i32 @main() {
  ret i32 0
}

!llvm.abom = !{!0}
!0 = !{!"ABOM\01\01\00\FF\7F\00\00\05\00\00\00\90=E3\E8"}
