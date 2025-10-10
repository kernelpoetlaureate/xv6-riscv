#include "types.h"
#include "user.h"

int
main(int argc, char **argv)
{
  // dump_pages: user helper to trigger a kernel-side dump of the
  // pageinfo table. This causes the kernel to print metadata for every
  // tracked physical page to the kernel console (QEMU/serial). Use this
  // when you want a full snapshot of which pages the kernel believes are
  // in-use and who owns them.
  //
  // IMPORTANT SAFETY NOTES:
  //  - This program only triggers printing of metadata (no page contents).
  //    Attempting to read arbitrary physical memory from the kernel can
  //    cause traps; that's why the kernel dump is metadata-only.
  //  - The output can be very large on systems with many allocated pages.
  //    Prefer filters or pageinfo_va/phys helpers when possible.
  //
  // The syscall `pageinfo(dst_user_ptr, mode)` uses mode==2 to mean
  // "dump all" and returns the number of printed entries (or -1 on error).
  if(pageinfo(0, 2) < 0){
    printf("dump_pages: pageinfo syscall failed\n");
    return 1;
  }
  return 0;
}
