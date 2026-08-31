; RUN: rm -rf %t && split-file %s %t
; RUN: not llvm-as %t/operands.ll -disable-output 2>&1 | FileCheck %s --check-prefix=OPERANDS
; RUN: not llvm-as %t/type.ll -disable-output 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llvm-as %t/size.ll -disable-output 2>&1 | FileCheck %s --check-prefix=SIZE

;--- operands.ll
; OPERANDS: incorrect number of operands in llvm.abom metadata
!llvm.abom = !{!0}
!0 = !{!"ABOM\01\01\00\FF\7F\00\00\05\00\00\00\90=E3\E8", !"ABOM\01\01\00\FF\7F\00\00\05\00\00\00\90=E3\E8"}

;--- type.ll
; TYPE: invalid value for llvm.abom metadata entry operand
!llvm.abom = !{!0}
!0 = !{i32 0}

;--- size.ll
; SIZE: llvm.abom metadata entry is not a bill of materials: invalid ABOM magic word
!llvm.abom = !{!0}
!0 = !{!"NOPE\01\01\00\FF\7F\00\00\05\00\00\00\90=E3\E8"}
