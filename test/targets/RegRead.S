.global main

#define SYS_getpid 39
#define SYS_kill 62

#define SIGTRAP 5

.section .data

myDouble: .double 64.125

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

  // Store to 'r13'
  movq $0xcafebabe, %r13
  trap

  // Store to 'r13b'
  movb $42, %r13b
  trap

  // Store to 'mm0'
  movq $0xba5eba11, %r13
  movq %r13, %mm0
  trap

  // Store to 'xmm0'
  movsd myDouble(%rip), %xmm0
  trap

  // Store to 'st0'
  emms
  fldl myDouble(%rip)
  trap

  popq %rbp
  movq $0, %rax
  ret
