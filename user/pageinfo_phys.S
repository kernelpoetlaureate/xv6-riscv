.globl pageinfo_phys
/* include kernel syscall numbers so SYS_pageinfo_phys is defined
   generated .S files use #include "kernel/syscall.h"
   follow that pattern (preprocessed via the C preprocessor because
   the file extension is .S) */
#include "kernel/syscall.h"

pageinfo_phys:
    li a7, SYS_pageinfo_phys
    ecall
    ret