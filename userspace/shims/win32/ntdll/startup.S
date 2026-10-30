 .text
 .globl GaiaProcessStart
GaiaProcessStart:
 subq $32,%rsp
 call GaiaInitializeProcess
 ud2

 .globl GaiaThreadEntry
GaiaThreadEntry:
 subq $32,%rsp
 movq %rdi,%rcx
 call GaiaThreadStart
 ud2
