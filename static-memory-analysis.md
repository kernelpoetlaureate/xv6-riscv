# XV6 Kernel - Complete Static Memory Analysis

## Memory Layout Overview

### **Code & Read-Only Sections**
| Section | Start Address | Size | Purpose |
|---------|---------------|------|---------|
| `.text` | `0x80000000` | 28KB (0x7000) | Kernel executable code |
| `.rodata` | `0x80007000` | 2KB (0x820) | Read-only data (strings, constants) |
| `.eh_frame` | `0x80007820` | 10KB (0x29d4) | Exception handling frames |

### **Writable Data Sections**
| Section | Start Address | Size | Purpose |
|---------|---------------|------|---------|
| `.data` | `0x8000a1f4` | 28 bytes | Initialized global variables |
| `.got` | `0x8000a210` | 16 bytes | Global Offset Table |
| `.got.plt` | `0x8000a220` | 16 bytes | Procedure Linkage Table |
| `.bss` | `0x8000a230` | 103KB (0x19338) | Uninitialized global variables |

## **Large Data Structures in .bss Section**

### **Process Management (87KB total)**
1. **`proc[NPROC]`** - `0x80012788` (23,040 bytes = 22.5KB)
   - Array of 64 process control blocks
   - Each process: 360 bytes
   - Contains: PID, state, memory, registers, etc.

2. **`bcache`** - `0x800181a0` (34,496 bytes = 33.7KB) 
   - Buffer cache for disk blocks
   - Contains cached disk data for performance

3. **`stack0`** - `0x8000a260` (32,768 bytes = 32KB)
   - Global boot stack for all CPUs
   - 8 CPUs × 4KB each = 32KB total
   - Used during kernel initialization

### **File System (11KB total)**
4. **`itable`** - `0x80020880` (6,824 bytes = 6.7KB)
   - Inode table for file system
   - Caches file metadata in memory

5. **`ftable`** - `0x80022470` (4,024 bytes = 3.9KB)
   - File descriptor table
   - Tracks open files across all processes

### **CPU & Memory Management (1.2KB total)**
6. **`cpus[NCPU]`** - `0x80012388` (1,024 bytes = 1KB)
   - Per-CPU data structures
   - 8 CPUs × 128 bytes each

7. **`cons`** - `0x80012260` (168 bytes)
   - Console input/output buffer

8. **`log`** - `0x80022328` (168 bytes)
   - File system logging structure

### **Small Global Variables**
| Variable | Address | Size | Purpose |
|----------|---------|------|---------|
| `kernel_pagetable` | `0x8000a248` | 8 bytes | Kernel page table pointer |
| `initproc` | `0x8000a250` | 8 bytes | Init process pointer |
| `ticks` | `0x8000a258` | 4 bytes | System timer ticks |
| `nextpid` | `0x8000a204` | 4 bytes | Next process ID to assign |
| `panicked` | `0x8000a230` | 4 bytes | Kernel panic flag |
| `panicking` | `0x8000a234` | 4 bytes | Panic in progress flag |

## **Memory Usage Summary**

### **Total Static Memory: ~145KB**
- **Code**: 40KB (.text + .rodata + .eh_frame)
- **Data**: 103KB (.bss section)
- **Small data**: 60 bytes (.data + .got + .got.plt)

### **Process-Related Memory: 87KB (60% of static memory)**
- Process table: 22.5KB
- Buffer cache: 33.7KB  
- Boot stack: 32KB

### **File System Memory: 11KB (7.6% of static memory)**
- Inode cache: 6.7KB
- File table: 3.9KB

### **Key Insights**
1. **Most memory is for processes**: 60% dedicated to process management
2. **Large buffer cache**: 33.7KB for disk I/O performance
3. **Fixed limits**: 64 processes max, 8 CPUs max
4. **Boot stack**: 32KB shared across all CPUs during init
5. **Runtime stacks**: Not visible here - allocated dynamically per process

## **What's NOT Shown (Dynamic Memory)**
- Individual process kernel stacks (allocated by `proc_mapstacks()`)
- User process memory spaces 
- Heap memory allocated via `kalloc()`
- Page tables created at runtime
- Memory mapped I/O regions

This represents the "skeleton" of xv6 - the static foundation before dynamic memory allocation begins.




giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ ^C
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ grep -r "NPROC" kernel/
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++) {    
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++) {    
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++) {    
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++) {    
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++) {    
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++) {    
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++) {    
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++) {    
kernel/kernel.asm:    for(p = proc; p < &proc[NPROC]; p++) {  
kernel/kernel.asm:    for(p = proc; p < &proc[NPROC]; p++) {  
kernel/kernel.asm:    for(p = proc; p < &proc[NPROC]; p++) {  
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++) {    
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++) {    
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++) {    
kernel/kernel.asm:  for(pp = proc; pp < &proc[NPROC]; pp++){  
kernel/kernel.asm:  for(pp = proc; pp < &proc[NPROC]; pp++){  
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++){     
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++){     
kernel/kernel.asm:    for(pp = proc; pp < &proc[NPROC]; pp++){
kernel/kernel.asm:    for(pp = proc; pp < &proc[NPROC]; pp++){
kernel/kernel.asm:    for(pp = proc; pp < &proc[NPROC]; pp++){
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++){
kernel/kernel.asm:  for(p = proc; p < &proc[NPROC]; p++){
kernel/param.h:#define NPROC        64  // maximum number of processes
kernel/proc.c:struct proc proc[NPROC];
kernel/proc.c:  for(p = proc; p < &proc[NPROC]; p++) {
kernel/proc.c:  for(p = proc; p < &proc[NPROC]; p++) {
kernel/proc.c:  for(p = proc; p < &proc[NPROC]; p++) {
kernel/proc.c:  //but proc.h is just a blueprint, the structure comes alive in proc.c "struct proc proc[NPROC]; "
kernel/proc.c:  for(pp = proc; pp < &proc[NPROC]; pp++){
kernel/proc.c:    for(pp = proc; pp < &proc[NPROC]; pp++){
kernel/proc.c:    for(p = proc; p < &proc[NPROC]; p++) {
kernel/proc.c:  for(p = proc; p < &proc[NPROC]; p++) {
kernel/proc.c:  for(p = proc; p < &proc[NPROC]; p++){
kernel/proc.c:  for(p = proc; p < &proc[NPROC]; p++){
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ grep -r "KSTACKSIZE" kernel/
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -s kernel/kernel | grep stack
   172: 000000008000a260 32768 OBJECT  GLOBAL DEFAULT    7 stack0
   286: 000000008000173c   158 FUNC    GLOBAL DEFAULT    1 proc_mapstacks
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ riscv64-linux-gnu-objdump -d kernel/kernel | grep stack
    80001180:   5bc000ef                jal     8000173c <proc_mapstacks>
000000008000173c <proc_mapstacks>:
    8000178e:   c121                    beqz    a0,800017ce <proc_mapstacks+0x92>
    800017b2:   fd549be3                bne     s1,s5,80001788 <proc_mapstacks+0x4c>
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -S kernel/kernel | grep bss
  [ 7] .bss              NOBITS           000000008000a230  0000b230
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -S kernel/kernel | grep bss
  [ 7] .bss              NOBITS           000000008000a230  0000b230
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -x 7 kernel/kernel
Section '.bss' has no data to dump.
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ riscv64-linux-gnu-objdump -d kernel/kernel | grep -A 20 stack0
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -x 7 kernel/kernel
Section '.bss' has no data to dump.
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ riscv64-linux-gnu-objdump -d kernel/kernel | grep -A 20 stack0
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -s kernel/kernel | grep bss
     7: 000000008000a230     0 SECTION LOCAL  DEFAULT    7 .bss
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -l kernel/kernel

Elf file type is EXEC (Executable file)
Entry point 0x80000000
There are 3 program headers, starting at offset 64

Program Headers:
  Type           Offset             VirtAddr           PhysAddr
                 FileSiz            MemSiz              Flags  Align
  RISCV_ATTRIBUT 0x000000000000b230 0x0000000000000000 0x0000000000000000
                 0x000000000000006a 0x0000000000000000  R      0x1
  LOAD           0x0000000000001000 0x0000000080000000 0x0000000080000000
                 0x000000000000a230 0x0000000000023568  RWE    0x1000
  GNU_STACK      0x0000000000000000 0x0000000000000000 0x0000000000000000
                 0x0000000000000000 0x0000000000000000  RW     0x10

 Section to Segment mapping:
  Segment Sections...
   00     .riscv.attributes
   01     .text .rodata .eh_frame .data .got .got.plt .bss
   02
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ riscv64-linux-gnu-objdump -d kernel/kernel | grep stack
    80001180:   5bc000ef                jal     8000173c <proc_mapstacks>
000000008000173c <proc_mapstacks>:
    8000178e:   c121                    beqz    a0,800017ce <proc_mapstacks+0x92>
    800017b2:   fd549be3                bne     s1,s5,80001788 <proc_mapstacks+0x4c>
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ riscv64-linux-gnu-objdump -d kernel/kernel | grep -A 20 '<proc_mapstacks>'
    80001180:   5bc000ef                jal     8000173c <proc_mapstacks>
    80001184:   8526                    mv      a0,s1
    80001186:   60e2                    ld      ra,24(sp)
    80001188:   6442                    ld      s0,16(sp)
    8000118a:   64a2                    ld      s1,8(sp)
    8000118c:   6902                    ld      s2,0(sp)
    8000118e:   6105                    addi    sp,sp,32
    80001190:   8082                    ret

0000000080001192 <kvminit>:
    80001192:   1141                    addi    sp,sp,-16
    80001194:   e406                    sd      ra,8(sp)
    80001196:   e022                    sd      s0,0(sp)
    80001198:   0800                    addi    s0,sp,16
    8000119a:   f4dff0ef                jal     800010e6 <kvmmake>
    8000119e:   00009797                auipc   a5,0x9
    800011a2:   0aa7b523                sd      a0,170(a5) # 8000a248 <kernel_pagetable>
    800011a6:   60a2                    ld      ra,8(sp)
    800011a8:   6402                    ld      s0,0(sp)
    800011aa:   0141                    addi    sp,sp,16
    800011ac:   8082                    ret
--
000000008000173c <proc_mapstacks>:
    8000173c:   715d                    addi    sp,sp,-80
    8000173e:   e486                    sd      ra,72(sp)
    80001740:   e0a2                    sd      s0,64(sp)
    80001742:   fc26                    sd      s1,56(sp)
    80001744:   f84a                    sd      s2,48(sp)
    80001746:   f44e                    sd      s3,40(sp)
    80001748:   f052                    sd      s4,32(sp)
    8000174a:   ec56                    sd      s5,24(sp)
    8000174c:   e85a                    sd      s6,16(sp)
    8000174e:   e45e                    sd      s7,8(sp)
    80001750:   e062                    sd      s8,0(sp)
    80001752:   0880                    addi    s0,sp,80
    80001754:   8a2a                    mv      s4,a0
    80001756:   00011497                auipc   s1,0x11
    8000175a:   03248493                addi    s1,s1,50 # 80012788 <proc>
    8000175e:   8c26                    mv      s8,s1
    80001760:   a4fa57b7                lui     a5,0xa4fa5
    80001764:   fa578793                addi    a5,a5,-91 # ffffffffa4fa4fa5 <end+0xffffffff24f81a3d>
    80001768:   4fa50937                lui     s2,0x4fa50
    8000176c:   a5090913                addi    s2,s2,-1456 # 4fa4fa50 <_entry-0x305b05b0>
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ riscv64-linux-gnu-objdump -d kernel/kernel | grep -A 50 '<proc_mapstacks>'
    80001180:   5bc000ef                jal     8000173c <proc_mapstacks>
    80001184:   8526                    mv      a0,s1
    80001186:   60e2                    ld      ra,24(sp)
    80001188:   6442                    ld      s0,16(sp)
    8000118a:   64a2                    ld      s1,8(sp)
    8000118c:   6902                    ld      s2,0(sp)
    8000118e:   6105                    addi    sp,sp,32
    80001190:   8082                    ret

0000000080001192 <kvminit>:
    80001192:   1141                    addi    sp,sp,-16
    80001194:   e406                    sd      ra,8(sp)
    80001196:   e022                    sd      s0,0(sp)
    80001198:   0800                    addi    s0,sp,16
    8000119a:   f4dff0ef                jal     800010e6 <kvmmake>
    8000119e:   00009797                auipc   a5,0x9
    800011a2:   0aa7b523                sd      a0,170(a5) # 8000a248 <kernel_pagetable>
    800011a6:   60a2                    ld      ra,8(sp)
    800011a8:   6402                    ld      s0,0(sp)
    800011aa:   0141                    addi    sp,sp,16
    800011ac:   8082                    ret

00000000800011ae <uvmcreate>:
    800011ae:   1101                    addi    sp,sp,-32
    800011b0:   ec06                    sd      ra,24(sp)
    800011b2:   e822                    sd      s0,16(sp)
    800011b4:   e426                    sd      s1,8(sp)
    800011b6:   1000                    addi    s0,sp,32
    800011b8:   941ff0ef                jal     80000af8 <kalloc>
    800011bc:   84aa                    mv      s1,a0
    800011be:   c509                    beqz    a0,800011c8 <uvmcreate+0x1a>
    800011c0:   6605                    lui     a2,0x1
    800011c2:   4581                    li      a1,0
    800011c4:   ad9ff0ef                jal     80000c9c <memset>
    800011c8:   8526                    mv      a0,s1
    800011ca:   60e2                    ld      ra,24(sp)
    800011cc:   6442                    ld      s0,16(sp)
    800011ce:   64a2                    ld      s1,8(sp)
    800011d0:   6105                    addi    sp,sp,32
    800011d2:   8082                    ret

00000000800011d4 <uvmunmap>:
    800011d4:   7139                    addi    sp,sp,-64
    800011d6:   fc06                    sd      ra,56(sp)
    800011d8:   f822                    sd      s0,48(sp)
    800011da:   0080                    addi    s0,sp,64
    800011dc:   03459793                slli    a5,a1,0x34
    800011e0:   e38d                    bnez    a5,80001202 <uvmunmap+0x2e>
    800011e2:   f04a                    sd      s2,32(sp)
    800011e4:   ec4e                    sd      s3,24(sp)
    800011e6:   e852                    sd      s4,16(sp)
--
000000008000173c <proc_mapstacks>:
    8000173c:   715d                    addi    sp,sp,-80
    8000173e:   e486                    sd      ra,72(sp)
    80001740:   e0a2                    sd      s0,64(sp)
    80001742:   fc26                    sd      s1,56(sp)
    80001744:   f84a                    sd      s2,48(sp)
    80001746:   f44e                    sd      s3,40(sp)
    80001748:   f052                    sd      s4,32(sp)
    8000174a:   ec56                    sd      s5,24(sp)
    8000174c:   e85a                    sd      s6,16(sp)
    8000174e:   e45e                    sd      s7,8(sp)
    80001750:   e062                    sd      s8,0(sp)
    80001752:   0880                    addi    s0,sp,80
    80001754:   8a2a                    mv      s4,a0
    80001756:   00011497                auipc   s1,0x11
    8000175a:   03248493                addi    s1,s1,50 # 80012788 <proc>
    8000175e:   8c26                    mv      s8,s1
    80001760:   a4fa57b7                lui     a5,0xa4fa5
    80001764:   fa578793                addi    a5,a5,-91 # ffffffffa4fa4fa5 <end+0xffffffff24f81a3d>
    80001768:   4fa50937                lui     s2,0x4fa50
    8000176c:   a5090913                addi    s2,s2,-1456 # 4fa4fa50 <_entry-0x305b05b0>
    80001770:   1902                    slli    s2,s2,0x20
    80001772:   993e                    add     s2,s2,a5
    80001774:   040009b7                lui     s3,0x4000
    80001778:   19fd                    addi    s3,s3,-1 # 3ffffff <_entry-0x7c000001>
    8000177a:   09b2                    slli    s3,s3,0xc
    8000177c:   4b99                    li      s7,6
    8000177e:   6b05                    lui     s6,0x1
    80001780:   00017a97                auipc   s5,0x17
    80001784:   a08a8a93                addi    s5,s5,-1528 # 80018188 <tickslock>
    80001788:   b70ff0ef                jal     80000af8 <kalloc>
    8000178c:   862a                    mv      a2,a0
    8000178e:   c121                    beqz    a0,800017ce <proc_mapstacks+0x92>
    80001790:   418485b3                sub     a1,s1,s8
    80001794:   858d                    srai    a1,a1,0x3
    80001796:   032585b3                mul     a1,a1,s2
    8000179a:   2585                    addiw   a1,a1,1
    8000179c:   00d5959b                slliw   a1,a1,0xd
    800017a0:   875e                    mv      a4,s7
    800017a2:   86da                    mv      a3,s6
    800017a4:   40b985b3                sub     a1,s3,a1
    800017a8:   8552                    mv      a0,s4
    800017aa:   915ff0ef                jal     800010be <kvmmap>
    800017ae:   16848493                addi    s1,s1,360
    800017b2:   fd549be3                bne     s1,s5,80001788 <proc_mapstacks+0x4c>
    800017b6:   60a6                    ld      ra,72(sp)
    800017b8:   6406                    ld      s0,64(sp)
    800017ba:   74e2                    ld      s1,56(sp)
    800017bc:   7942                    ld      s2,48(sp)
    800017be:   79a2                    ld      s3,40(sp)
    800017c0:   7a02                    ld      s4,32(sp)
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ ^C
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -S kernel/kernel
There are 21 section headers, starting at offset 0x42560:

Section Headers:
  [Nr] Name              Type             Address           Offset
       Size              EntSize          Flags  Link  Info  Align
  [ 0]                   NULL             0000000000000000  00000000
       0000000000000000  0000000000000000           0     0     0
  [ 1] .text             PROGBITS         0000000080000000  00001000
       0000000000007000  0000000000000000  AX       0     0     16
  [ 2] .rodata           PROGBITS         0000000080007000  00008000
       0000000000000820  0000000000000000   A       0     0     8
  [ 3] .eh_frame         PROGBITS         0000000080007820  00008820
       00000000000029d4  0000000000000000   A       0     0     8
  [ 4] .data             PROGBITS         000000008000a1f4  0000b1f4
       000000000000001c  0000000000000000  WA       0     0     4
  [ 5] .got              PROGBITS         000000008000a210  0000b210
       0000000000000010  0000000000000008  WA       0     0     8
  [ 6] .got.plt          PROGBITS         000000008000a220  0000b220
       0000000000000010  0000000000000008  WA       0     0     8
  [ 7] .bss              NOBITS           000000008000a230  0000b230
       0000000000019338  0000000000000000  WA       0     0     16
  [ 8] .riscv.attributes RISCV_ATTRIBUTE  0000000000000000  0000b230
       000000000000006a  0000000000000000           0     0     1
  [ 9] .comment          PROGBITS         0000000000000000  0000b29a
       000000000000001f  0000000000000001  MS       0     0     1
  [10] .debug_line       PROGBITS         0000000000000000  0000b2b9
       000000000000ad1c  0000000000000000           0     0     1
  [11] .debug_line_str   PROGBITS         0000000000000000  00015fd5
       000000000000004b  0000000000000001  MS       0     0     1
  [12] .debug_info       PROGBITS         0000000000000000  00016020
       0000000000013217  0000000000000000           0     0     1
  [13] .debug_abbrev     PROGBITS         0000000000000000  00029237
       0000000000003980  0000000000000000           0     0     1
  [14] .debug_aranges    PROGBITS         0000000000000000  0002cbc0
       00000000000004e0  0000000000000000           0     0     16
  [15] .debug_str        PROGBITS         0000000000000000  0002d0a0
       000000000000115a  0000000000000001  MS       0     0     1
  [16] .debug_loc        PROGBITS         0000000000000000  0002e1fa
       000000000001158e  0000000000000000           0     0     1
  [17] .debug_ranges     PROGBITS         0000000000000000  0003f788
       00000000000008f0  0000000000000000           0     0     1
  [18] .symtab           SYMTAB           0000000000000000  00040078
       0000000000001b90  0000000000000018          19    98     8
  [19] .strtab           STRTAB           0000000000000000  00041c08
       0000000000000888  0000000000000000           0     0     1
  [20] .shstrtab         STRTAB           0000000000000000  00042490
       00000000000000d0  0000000000000000           0     0     1
Key to Flags:
  W (write), A (alloc), X (execute), M (merge), S (strings), I (info),
  L (link order), O (extra OS processing required), G (group), T (TLS),
  C (compressed), x (unknown), o (OS specific), E (exclude),
  D (mbind), p (processor specific)
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -s kernel/kernel | grep -E "GLOBAL|WEAK" | head -50
    97: 000000008000a210     0 OBJECT  LOCAL  DEFAULT    5 _GLOBAL_OFFSET_TABLE_
    98: 000000008000297a   160 FUNC    GLOBAL DEFAULT    1 sys_pause
    99: 0000000080001008   182 FUNC    GLOBAL DEFAULT    1 mappages
   100: 00000000800014ae   160 FUNC    GLOBAL DEFAULT    1 copyinstr
   101: 0000000080000176   260 FUNC    GLOBAL DEFAULT    1 consoleread
   102: 0000000080000dee    54 FUNC    GLOBAL DEFAULT    1 safestrcpy
   103: 0000000080004bf8    66 FUNC    GLOBAL DEFAULT    1 sys_close
   104: 0000000080001e9c    44 FUNC    GLOBAL DEFAULT    1 yield
   105: 0000000080022328   168 OBJECT  GLOBAL DEFAULT    7 log
   106: 0000000080012338    32 OBJECT  GLOBAL DEFAULT    7 kmem
   107: 000000008000083e    86 FUNC    GLOBAL DEFAULT    1 uartinit
   108: 000000008000221e    74 FUNC    GLOBAL DEFAULT    1 either_copyout
   109: 000000008000001c    72 FUNC    GLOBAL DEFAULT    1 timerinit
   110: 0000000080012788 23040 OBJECT  GLOBAL DEFAULT    7 proc
   111: 00000000800040d8   190 FUNC    GLOBAL DEFAULT    1 fileread
   112: 00000000800004fa   740 FUNC    GLOBAL DEFAULT    1 printf
   113: 00000000800028fe   124 FUNC    GLOBAL DEFAULT    1 sys_sbrk
   114: 0000000080006000     0 NOTYPE  GLOBAL DEFAULT    1 trampoline
   115: 000000008000a230     4 OBJECT  GLOBAL DEFAULT    7 panicked
   116: 000000008000541c    32 FUNC    GLOBAL DEFAULT    1 plic_claim
   117: 00000000800053ce    26 FUNC    GLOBAL DEFAULT    1 plicinit
   118: 000000008000212a   244 FUNC    GLOBAL DEFAULT    1 kwait
   119: 000000008000543c    38 FUNC    GLOBAL DEFAULT    1 plic_complete
   120: 0000000080001de2   186 FUNC    GLOBAL DEFAULT    1 sched
   121: 0000000080000d00    96 FUNC    GLOBAL DEFAULT    1 memmove
   122: 000000008000282a   100 FUNC    GLOBAL DEFAULT    1 syscall
   123: 000000008000188a    20 FUNC    GLOBAL DEFAULT    1 cpuid
   124: 0000000080003626   260 FUNC    GLOBAL DEFAULT    1 writei
   125: 00000000800028c8    20 FUNC    GLOBAL DEFAULT    1 sys_fork
   126: 00000000800181a0 34496 OBJECT  GLOBAL DEFAULT    7 bcache
   127: 000000008000505e    72 FUNC    GLOBAL DEFAULT    1 sys_mkdir
   128: 00000000800011d4   138 FUNC    GLOBAL DEFAULT    1 uvmunmap
   129: 000000008000372a    22 FUNC    GLOBAL DEFAULT    1 namecmp
   130: 00000000800053e8    52 FUNC    GLOBAL DEFAULT    1 plicinithart
   131: 0000000080001f7e    86 FUNC    GLOBAL DEFAULT    1 reparent
   132: 0000000080002802    40 FUNC    GLOBAL DEFAULT    1 argstr
   133: 000000008000125e    68 FUNC    GLOBAL DEFAULT    1 uvmdealloc
   134: 0000000080003f72    70 FUNC    GLOBAL DEFAULT    1 filedup
   135: 00000000800039cc    26 FUNC    GLOBAL DEFAULT    1 namei
   136: 0000000080002a76   134 FUNC    GLOBAL DEFAULT    1 binit
   137: 0000000080001484    42 FUNC    GLOBAL DEFAULT    1 uvmclear
   138: 0000000080004b68    72 FUNC    GLOBAL DEFAULT    1 sys_read
   139: 00000000800045f4   856 FUNC    GLOBAL DEFAULT    1 kexec
   140: 0000000080003494   114 FUNC    GLOBAL DEFAULT    1 fsinit
   141: 0000000080000d60    20 FUNC    GLOBAL DEFAULT    1 memcpy
   142: 00000000800010be    40 FUNC    GLOBAL DEFAULT    1 kvmmap
   143: 0000000080000a16   102 FUNC    GLOBAL DEFAULT    1 kfree
   144: 000000008000189e    32 FUNC    GLOBAL DEFAULT    1 mycpu
   145: 0000000080003324   136 FUNC    GLOBAL DEFAULT    1 iput
   146: 00000000800010e6   172 FUNC    GLOBAL DEFAULT    1 kvmmake
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -s kernel/kernel | grep "OBJECT.*GLOBAL" | awk '{print $2, $3, $8}' | sort -k2 -nr
00000000800181a0 34496 bcache
000000008000a260 32768 stack0
0000000080012788 23040 proc
0000000080020880 6824 itable
0000000080022470 4024 ftable
0000000080012388 1024 cpus
0000000080022328 168 log
0000000080012260 168 cons
00000000800223d0 160 devsw
0000000080020860 32 sb
0000000080012338 32 kmem
0000000080018188 24 tickslock
0000000080012370 24 wait_lock
0000000080012358 24 pid_lock
000000008000a250 8 initproc
000000008000a248 8 kernel_pagetable
000000008000a258 4 ticks
000000008000a234 4 panicking
000000008000a230 4 panicked
000000008000a204 4 nextpid
000000008000a210 0 _GLOBAL_OFFSET_TABLE_
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -S kernel/kernel | grep -E "PROGBITS|NOBITS" | awk '{print $4, $5, $6, $2}' | sort
0000000000000000 0000b2b9  .debug_line
0000000000000000 00015fd5  .debug_line_str
0000000000000000 00016020  .debug_info
0000000000000000 00029237  .debug_abbrev
0000000000000000 0002cbc0  .debug_aranges
000000008000a204 4 nextpid
000000008000a210 0 _GLOBAL_OFFSET_TABLE_
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -S kernel/kernel | grep -E "PROGBITS|NOBITS" | awk '{print $4, $5, $6, $2}' | sort
0000000000000000 0000b2b9  .debug_line
0000000000000000 00015fd5  .debug_line_str
0000000000000000 00016020  .debug_info
0000000000000000 00029237  .debug_abbrev
000000008000a204 4 nextpid
000000008000a210 0 _GLOBAL_OFFSET_TABLE_
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -S kernel/kernel | grep -E "PROGBITS|NOBITS" | awk '{print $4, $5, $6, $2}' | sort
0000000000000000 0000b2b9  .debug_line
0000000000000000 00015fd5  .debug_line_str
0000000000000000 00016020  .debug_info
000000008000a204 4 nextpid
000000008000a210 0 _GLOBAL_OFFSET_TABLE_
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -S kernel/kernel | grep -E "PROGBITS|NOBITS" | awk '{print $4, $5, $6, $2}' | sort
0000000000000000 0000b2b9  .debug_line
0000000000000000 00015fd5  .debug_line_str
000000008000a204 4 nextpid
000000008000a210 0 _GLOBAL_OFFSET_TABLE_
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -S kernel/kernel | grep -E "PROGBITS|NOBITS" | awk '{print $4, $5, $6, $2}' | sort
0000000000000000 0000b2b9  .debug_line
0000000000000000 00015fd5  .debug_line_str
0000000000000000 00016020  .debug_info
0000000000000000 00029237  .debug_abbrev
000000008000a204 4 nextpid
000000008000a210 0 _GLOBAL_OFFSET_TABLE_
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -S kernel/kernel | grep -E "PROGBITS|NOBITS" | awk '{print $4, $5, $6, $2}' | sort
0000000000000000 0000b2b9  .debug_line
000000008000a204 4 nextpid
000000008000a210 0 _GLOBAL_OFFSET_TABLE_
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -S kernel/kernel | grep -E "PROGBITS|NOBITS" | awk '{print $4, $5, $6, $2}' | sort
0000000000000000 0000b2b9  .debug_line
0000000000000000 00015fd5  .debug_line_str
000000008000a204 4 nextpid
000000008000a210 0 _GLOBAL_OFFSET_TABLE_
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -S kernel/kernel | grep -E "PROGBITS|NOBITS" | awk '{print $4, $5, $6, $2}' | sort
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ readelf -S kernel/kernel | grep -E "PROGBITS|NOBITS" | awk '{print $4, $5, $6, $2}' | sort
0000000000000000 0000b2b9  .debug_line
0000000000000000 00015fd5  .debug_line_str
0000000000000000 00016020  .debug_info
0000000000000000 00029237  .debug_abbrev
}' | sort
0000000000000000 0000b2b9  .debug_line
0000000000000000 00015fd5  .debug_line_str
0000000000000000 00016020  .debug_info
0000000000000000 00029237  .debug_abbrev
0000000000000000 0002cbc0  .debug_aranges
0000000000000000 0000b2b9  .debug_line
0000000000000000 00015fd5  .debug_line_str
0000000000000000 00016020  .debug_info
0000000000000000 00029237  .debug_abbrev
0000000000000000 0002cbc0  .debug_aranges
0000000000000000 0002d0a0  .debug_str
0000000000000000 0002e1fa  .debug_loc
0000000000000000 00016020  .debug_info
0000000000000000 00029237  .debug_abbrev
0000000000000000 0002cbc0  .debug_aranges
0000000000000000 0002d0a0  .debug_str
0000000000000000 0002e1fa  .debug_loc
0000000000000000 0003f788  .debug_ranges
0000000000000000 0002cbc0  .debug_aranges
0000000000000000 0002d0a0  .debug_str
0000000000000000 0002e1fa  .debug_loc
0000000000000000 0003f788  .debug_ranges
0000000000000000 0002d0a0  .debug_str
0000000000000000 0002e1fa  .debug_loc
0000000000000000 0003f788  .debug_ranges
0000000000000000 0003f788  .debug_ranges
NOBITS 000000008000a230 0000b230 7]
PROGBITS 0000000000000000 0000b29a 9]
PROGBITS 0000000080000000 00001000 1]
PROGBITS 0000000080007000 00008000 2]
PROGBITS 0000000080007820 00008820 3]
PROGBITS 000000008000a1f4 0000b1f4 4]
PROGBITS 000000008000a210 0000b210 5]
PROGBITS 000000008000a220 0000b220 6]
giorgi@DESKTOP-5CG7AFG:/mnt/c/Users/giorgi/Downloads/xv6-riscv$ 
























