#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf("usage: getregs pid\n");
    exit(1);
  }

  int pid = atoi(argv[1]);

  // allocate buffers in user space
  struct trapframe tf;
  struct context ctx;

  if(getregs(pid, (uint64)&tf, (uint64)&ctx) < 0){
    printf("getregs: failed for pid %d\n", pid);
    exit(1);
  }

  printf("Trapframe (saved user regs) for pid %d:\n", pid);
  printf("  epc 0x%llx\n", tf.epc);
  printf("  ra  0x%llx\n", tf.ra);
  printf("  sp  0x%llx\n", tf.sp);
  printf("  gp  0x%llx\n", tf.gp);
  printf("  tp  0x%llx\n", tf.tp);
  printf("  t0  0x%llx t1 0x%llx t2 0x%llx\n", tf.t0, tf.t1, tf.t2);
  printf("  s0  0x%llx s1 0x%llx\n", tf.s0, tf.s1);
  printf("  a0  0x%llx a1 0x%llx\n", tf.a0, tf.a1);
  printf("  a2  0x%llx a3 0x%llx a4 0x%llx a5 0x%llx\n", tf.a2, tf.a3, tf.a4, tf.a5);
  printf("  a6  0x%llx a7 0x%llx\n", tf.a6, tf.a7);
  printf("  s2..s11: 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx\n",
         tf.s2, tf.s3, tf.s4, tf.s5, tf.s6, tf.s7, tf.s8, tf.s9, tf.s10, tf.s11);
  printf("  t3..t6: 0x%llx 0x%llx 0x%llx 0x%llx\n", tf.t3, tf.t4, tf.t5, tf.t6);

  printf("\nContext (saved kernel callee-saved regs):\n");
  printf("  ra 0x%llx\n", ctx.ra);
  printf("  sp 0x%llx\n", ctx.sp);
  printf("  s0 0x%llx s1 0x%llx s2 0x%llx s3 0x%llx s4 0x%llx s5 0x%llx s6 0x%llx s7 0x%llx s8 0x%llx s9 0x%llx s10 0x%llx s11 0x%llx\n",
         ctx.s0, ctx.s1, ctx.s2, ctx.s3, ctx.s4, ctx.s5, ctx.s6, ctx.s7, ctx.s8, ctx.s9, ctx.s10, ctx.s11);

  exit(0);
}
