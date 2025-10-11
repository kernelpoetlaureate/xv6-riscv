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
    if(color_output){
      const char *col = (st.type == T_DIR) ? "\x1b[36m" : (st.type == T_DEVICE) ? "\x1b[33m" : "\x1b[37m";
      fmtsize(st.size, sizebuf);
      printf("%s%s\x1b[0m %d %d %s\n", col, fmtname(path), st.type, st.ino, sizebuf);
    } else {
      printf("%s %d %d %d\n", fmtname(path), st.type, st.ino, (int) st.size);
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
      if(color_output){
        char sizebuf[16];
        const char *col = (st.type == T_DIR) ? "\x1b[36m" : (st.type == T_DEVICE) ? "\x1b[33m" : "\x1b[37m";
        fmtsize(st.size, sizebuf);
        printf("%s%s\x1b[0m %d %d %s\n", col, fmtname(buf), st.type, st.ino, sizebuf);
      } else {
        printf("%s %d %d %d\n", fmtname(buf), st.type, st.ino, (int) st.size);
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
