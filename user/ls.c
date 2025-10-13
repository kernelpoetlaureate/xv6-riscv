#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  buf[sizeof(buf)-1] = '\0';
  return buf;
}

// Extract the plain filename (no padding) from a path into out (size DIRSIZ+1).
static void
plain_name(const char *path, char *out)
{
  const char *p;
  int i = 0;
  for(p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;
  while(*p && i < DIRSIZ){
    out[i++] = *p++;
  }
  out[i] = '\0';
}

// Return a short human-friendly description for known utilities.
static const char*
descr_for(const char *name)
{
  if(strcmp(name, "README") == 0) return "project README";
  if(strcmp(name, "cat") == 0) return "concatenate and print files";
  if(strcmp(name, "echo") == 0) return "print arguments";
  if(strcmp(name, "forktest") == 0) return "fork/test concurrency";
  if(strcmp(name, "grep") == 0) return "search for pattern";
  if(strcmp(name, "init") == 0) return "initial user process";
  if(strcmp(name, "kill") == 0) return "send signal to process";
  if(strcmp(name, "ln") == 0) return "create a link";
  if(strcmp(name, "ls") == 0) return "list directory";
  if(strcmp(name, "mkdir") == 0) return "create directory";
  if(strcmp(name, "rm") == 0) return "remove file";
  if(strcmp(name, "sh") == 0) return "shell";
  if(strcmp(name, "stressfs") == 0) return "fs stress tester";
  if(strcmp(name, "usertests") == 0) return "suite of user tests";
  if(strcmp(name, "grind") == 0) return "stress test";
  if(strcmp(name, "wc") == 0) return "word/line/byte count";
  if(strcmp(name, "zombie") == 0) return "zombie process demo";
  if(strcmp(name, "logstress") == 0) return "logging stress";
  if(strcmp(name, "forphan") == 0) return "fork+orphan test";
  if(strcmp(name, "dorphan") == 0) return "double-orphan test";
  if(strcmp(name, "memdump") == 0) return "dump memory contents";
  if(strcmp(name, "memdump_phys") == 0) return "dump pageinfo for PA/VA";
  if(strcmp(name, "dump_pages") == 0) return "dump kernel pageinfo table";
  if(strcmp(name, "dumppi") == 0) return "dump pageinfo entries";
  if(strcmp(name, "memmap") == 0) return "scan physical memory map";
  if(strcmp(name, "alloctest") == 0) return "allocator test";
  if(strcmp(name, "dumpall") == 0) return "dump all memory info";
  if(strcmp(name, "htop") == 0) return "interactive process viewer";
  if(strcmp(name, "printlink") == 0) return "print link targets";
  if(strcmp(name, "console") == 0) return "serial console device";
  return "utility";
}

// Human-readable size formatting into buf (must be at least 16 bytes)
// Convert unsigned long long to decimal string, return pointer to end
static char*
uitoa(unsigned long long v, char *out)
{
  char tmp[32];
  int i = 0;
  if(v == 0){
    out[0] = '0';
    out[1] = '\0';
    return out + 1;
  }
  while(v){
    tmp[i++] = '0' + (v % 10);
    v /= 10;
  }
  int j = 0;
  while(i--){
    out[j++] = tmp[i];
  }
  out[j] = '\0';
  return out + j;
}

static void
fmtsize(uint64 size, char *buf)
{
  const char *units[] = {"B", "K", "M", "G"};
  uint64 s = size;
  int u = 0;

  // scale down s to be in [0,1024) and track unit
  while(s >= 1024 && u < 3){
    // keep one extra digit for tenths: multiply by 10 before dividing
    s = (s + 512) / 1024; // rounded divide to reduce error
    u++;
  }

  if(u == 0){
    // bytes as integer
    uitoa((unsigned long long)size, buf);
    int l = strlen(buf);
    strcpy(buf + l, units[u]);
  } else {
    // s currently holds the value scaled to the chosen unit (approx),
    // but we want to print one decimal place without floating point.
    // Recompute integer whole and tenths using integer arithmetic from original size.
  // compute scaled value with one decimal: (size * 10) / (1024^u)
    uint64 denom = 1;
    int i;
    for(i = 0; i < u; i++)
      denom *= 1024;

    uint64 scaled10 = (size * 10 + denom/2) / denom; // rounded
    uint64 whole = scaled10 / 10;
    uint64 tenths = scaled10 % 10;

  char *p = buf;
  p = uitoa((unsigned long long)whole, p);
  *p++ = '.';
  *p++ = '0' + (int)tenths;
  /* append unit string without using strcat to avoid including system headers */
  char *q = (char*)units[u];
  while(*q) *p++ = *q++;
  *p = '\0';
  }
}

// Global: color output enabled when -G flag is passed
static int color_output = 0;

void
ls(char *path)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_DEVICE:
  case T_FILE:
  {
    char sizebuf[16];
    char pname[DIRSIZ+1];
    plain_name(path, pname);
    const char *d = descr_for(pname);
    if(color_output){
      const char *col = (st.type == T_DIR) ? "\x1b[36m" : (st.type == T_DEVICE) ? "\x1b[33m" : "\x1b[37m";
      fmtsize(st.size, sizebuf);
      printf("%s%s\x1b[0m %d %d %s (%s)\n", col, fmtname(path), st.type, st.ino, sizebuf, d);
    } else {
      printf("%s %d %d %d (%s)\n", fmtname(path), st.type, st.ino, (int) st.size, d);
    }
    break;
  }

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf("ls: cannot stat %s\n", buf);
        continue;
      }
      char pname[DIRSIZ+1];
      plain_name(buf, pname);
      const char *d = descr_for(pname);
      if(color_output){
        char sizebuf[16];
        const char *col = (st.type == T_DIR) ? "\x1b[36m" : (st.type == T_DEVICE) ? "\x1b[33m" : "\x1b[37m";
        fmtsize(st.size, sizebuf);
        printf("%s%s\x1b[0m %d %d %s (%s)\n", col, fmtname(buf), st.type, st.ino, sizebuf, d);
      } else {
        printf("%s %d %d %d (%s)\n", fmtname(buf), st.type, st.ino, (int) st.size, d);
      }
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    ls(".");
    exit(0);
  }

  // Simple flag parsing: -G to force color
  int any = 0;
  for(i=1; i<argc; i++){
    if(strcmp(argv[i], "-G") == 0 || strcmp(argv[i], "--color") == 0){
      color_output = 1;
      continue;
    }
    any = 1;
    ls(argv[i]);
  }
  if(!any){
    ls(".");
  }
  exit(0);
}
