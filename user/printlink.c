#include "types.h"
#include "user.h"

// Print an OSC 8 hyperlink sequence: https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#OSC-p
// Format: ESC ] 8 ; params ; URI ESC backslash  <text> ESC ] 8 ;; ESC backslash

int
main(int argc, char *argv[])
{
  const char *url = argc > 1 ? argv[1] : "https://example.com";
  const char *text = argc > 2 ? argv[2] : "click-me";

  // OSC 8 start
  printf("\x1b]8;;%s\x1b\\", url);
  // Text to display
  printf("%s", text);
  // OSC 8 end
  printf("\x1b]8;;\x1b\\\n");

  exit(0);
}
