/* Passes in an overflowing buffer size.
   Write should silently exits or exit(-1). */

#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
	int handle, byte_cnt;

  CHECK (create ("test.txt", sizeof sample - 1), "create \"test.txt\"");
  CHECK ((handle = open ("test.txt")) > 1, "open \"test.txt\"");
  int overflow = 0xffffffff;
  byte_cnt = write (handle, sample, overflow);
  if (byte_cnt != 0) {
  	fail("write() returned %d instead of 0", byte_cnt);
  }
}
