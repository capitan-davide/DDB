.global main

#define SYS_getpid 39
#define SYS_kill 62

#define SIGTRAP 5

#define NULL 0

.section .data

hexFormat:        .asciz "%#x"
floatFormat:      .asciz "%.2f"
longFloatFormat:  .asciz "%.2Lf"

.section .text

.macro trap
  movq $SYS_kill, %rax // kill(
  movq %r12, %rdi      //   pid,
  movq $SIGTRAP, %rsi  //   SIGTRAP
  syscall              // );
.endm

main:
  push %rbp
  movq %rsp, %rbp

  movq $SYS_getpid, %rax  // getpid(
  syscall                 // );
  movq %rax, %r12
  trap

  // Print the content of 'rsi'
  leaq hexFormat(%rip), %rdi
  movq $0, %rax // number of vector registers
  call printf@plt
  movq $NULL, %rdi
  call fflush@plt
  trap

  // Print the content of 'mm0'
  movq %mm0, %rsi // copy 'mm0' to 'rsi'
  leaq hexFormat(%rip), %rdi
  movq $0, %rax
  call printf@plt
  movq $NULL, %rdi
  call fflush@plt
  trap

  // Print the content of 'xmm0'
  leaq floatFormat(%rip), %rdi
  movq $1, %rax // we have 1 vector register
  call printf@plt
  movq $NULL, %rdi
  call fflush@plt
  trap

  // Print the content of 'st0'
  subq $16, %rsp // allocate 16 Bytes on the stack to store the content of 'st0'
  fstpt (%rsp) // pop 'st0' from the FPU stack
  leaq longFloatFormat(%rip), %rdi
  movq $0, %rax
  call printf@plt
  movq $NULL, %rdi
  call fflush@plt
  addq $16, %rsp
  trap

  popq %rbp
  movq $0, %rax
  ret
