/* Try a read with overflowing size, which should return 0 without reading
   anything. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  int handle, byte_cnt;
  char buf;

  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");

  buf = 123;
  int overflow = 0xffffffff;
  byte_cnt = read (handle, &buf, overflow);
  if (byte_cnt != 0)
    fail ("read() returned %d instead of 0", byte_cnt);
  else if (buf != 123)
    fail ("read() with overflowing size modified buffer");
}

