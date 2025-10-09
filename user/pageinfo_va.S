.globl pageinfo_va
/* include kernel syscall numbers so SYS_pageinfo_va is defined
   generated .S files use #include "kernel/syscall.h"
   follow that pattern (preprocessed via the C preprocessor because
   the file extension is .S) */
#include "kernel/syscall.h"

pageinfo_va:
    li a7, SYS_pageinfo_va
    ecall
    ret
