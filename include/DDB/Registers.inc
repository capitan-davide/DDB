#ifndef REG
#error "This file is intended for inclusion with the REG macro defined"
#endif

#define GPR_OFFSET(r) (offsetof(user, regs) + offsetof(user_regs_struct, r))

#define GPR_64(name, dwarfId)                                                  \
  REG(name, dwarfId, 8, GPR_OFFSET(name), RegisterType::GPR,                   \
      RegisterFormat::UInt)

GPR_64(rax, 0)
GPR_64(rdx, 1)
GPR_64(rcx, 2)
GPR_64(rbx, 3)
GPR_64(rsi, 4)
GPR_64(rdi, 5)
GPR_64(rbp, 6)
GPR_64(rsp, 7)
GPR_64(r8, 8)
GPR_64(r9, 9)
GPR_64(r10, 10)
GPR_64(r11, 11)
GPR_64(r12, 12)
GPR_64(r13, 13)
GPR_64(r14, 14)
GPR_64(r15, 15)
GPR_64(rip, 16)
GPR_64(eflags, 49)
GPR_64(cs, 51)
GPR_64(fs, 54)
GPR_64(gs, 55)
GPR_64(ss, 52)
GPR_64(ds, 53)
GPR_64(es, 50)
GPR_64(orig_rax, -1)

#define GPR_32(name, super)                                                    \
  REG(name, -1, 4, GPR_OFFSET(super), RegisterType::SubGPR,                    \
      RegisterFormat::UInt)

GPR_32(eax, rax)
GPR_32(edx, rdx)
GPR_32(ecx, rcx)
GPR_32(ebx, rbx)
GPR_32(esi, rsi)
GPR_32(edi, rdi)
GPR_32(ebp, rbp)
GPR_32(esp, rsp)
GPR_32(r8d, r8)
GPR_32(r9d, r9)
GPR_32(r10d, r10)
GPR_32(r11d, r11)
GPR_32(r12d, r12)
GPR_32(r13d, r13)
GPR_32(r14d, r14)
GPR_32(r15d, r15)

#define GPR_16(name, super)                                                    \
  REG(name, -1, 2, GPR_OFFSET(super), RegisterType::SubGPR,                    \
      RegisterFormat::UInt)

GPR_16(ax, rax)
GPR_16(dx, rdx)
GPR_16(cx, rcx)
GPR_16(bx, rbx)
GPR_16(si, rsi)
GPR_16(di, rdi)
GPR_16(bp, rbp)
GPR_16(sp, rsp)
GPR_16(r8w, r8)
GPR_16(r9w, r9)
GPR_16(r10w, r10)
GPR_16(r11w, r11)
GPR_16(r12w, r12)
GPR_16(r13w, r13)
GPR_16(r14w, r14)
GPR_16(r15w, r15)

#define GPR_8H(name, super)                                                    \
  REG(name, -1, 1, GPR_OFFSET(super) + 1, RegisterType::SubGPR,                \
      RegisterFormat::UInt)

GPR_8H(ah, rax)
GPR_8H(dh, rdx)
GPR_8H(ch, rcx)
GPR_8H(bh, rbx)

#define GPR_8L(name, super)                                                    \
  REG(name, -1, 1, GPR_OFFSET(super), RegisterType::SubGPR,                    \
      RegisterFormat::UInt)

GPR_8L(al, rax)
GPR_8L(dl, rdx)
GPR_8L(cl, rcx)
GPR_8L(bl, rbx)
GPR_8L(sil, rsi)
GPR_8L(dil, rdi)
GPR_8L(bpl, rbp)
GPR_8L(spl, rsp)
GPR_8L(r8b, r8)
GPR_8L(r9b, r9)
GPR_8L(r10b, r10)
GPR_8L(r11b, r11)
GPR_8L(r12b, r12)
GPR_8L(r13b, r13)
GPR_8L(r14b, r14)
GPR_8L(r15b, r15)

#define FPR_OFFSET(r) (offsetof(user, i387) + offsetof(user_fpregs_struct, r))
#define FPR_SIZE(r) (sizeof(user_fpregs_struct::r))

#define FPR(name, dwarfId, userName)                                           \
  REG(name, dwarfId, FPR_SIZE(userName), FPR_OFFSET(userName),                 \
      RegisterType::FPR, RegisterFormat::UInt)

FPR(fcw, 65, cwd)
FPR(fsw, 66, swd)
FPR(ftw, -1, ftw)
FPR(fop, -1, fop)
FPR(frip, -1, rip)
FPR(frdp, -1, rdp)
FPR(mxcsr, 64, mxcsr)
FPR(mxcsrmask, -1, mxcr_mask)

#define FPR_ST(num)                                                            \
  REG(st##num, (33 + num), 16, (FPR_OFFSET(st_space) + num * 16),              \
      RegisterType::FPR, RegisterFormat::LongDouble)

FPR_ST(0)
FPR_ST(1)
FPR_ST(2)
FPR_ST(3)
FPR_ST(4)
FPR_ST(5)
FPR_ST(6)
FPR_ST(7)

#define FPR_MM(num)                                                            \
  REG(mm##num, (41 + num), 8, (FPR_OFFSET(st_space) + num * 16),               \
      RegisterType::FPR, RegisterFormat::Vector)

FPR_MM(0)
FPR_MM(1)
FPR_MM(2)
FPR_MM(3)
FPR_MM(4)
FPR_MM(5)
FPR_MM(6)
FPR_MM(7)

#define FPR_XMM(num)                                                           \
  REG(xmm##num, (17 + num), 16, (FPR_OFFSET(xmm_space) + num * 16),            \
      RegisterType::FPR, RegisterFormat::Vector)

FPR_XMM(0)
FPR_XMM(1)
FPR_XMM(2)
FPR_XMM(3)
FPR_XMM(4)
FPR_XMM(5)
FPR_XMM(6)
FPR_XMM(7)
FPR_XMM(8)
FPR_XMM(9)
FPR_XMM(10)
FPR_XMM(11)
FPR_XMM(12)
FPR_XMM(13)
FPR_XMM(14)
FPR_XMM(15)

#define DR_OFFSET(num) (offsetof(user, u_debugreg) + num * 8)

#define DR(num)                                                                \
  REG(dr##num, -1, 8, DR_OFFSET(num), RegisterType::DR, RegisterFormat::UInt)

DR(0)
DR(1)
DR(2)
DR(3)
DR(4)
DR(5)
DR(6)
DR(7)
