/* Compares hit rate for cold cache vs cached file.
   Resets buffer cache, reads the contents of a test file
   sequentially, and measure cache hit rate for cold cache.
   Closes file, re-opens it, and reads it sequentially again. */

#include <random.h>
#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

static char buf[32768];

void
test_main (void)
{
  int fd;

  CHECK (create ("test", sizeof buf), "create \"%s\"", "test");
  CHECK ((fd = open ("test")) > 1, "open \"%s\"", "test");
  random_bytes (buf, sizeof buf);
  CHECK (write (fd, buf, sizeof buf) > 0, "write \"%s\"", "test");
  msg ("close \"%s\"", "test");
  close (fd);

  int disk_reads;
  int disk_writes;
  // reset buffer cache syscall
  cachereset ();

  // read 
  CHECK ((fd = open ("test")) > 1, "open \"%s\"", "test");
  for (i = 0; i < sizeof buf; i++)
    {
      char c;
      CHECK (read (fd, &c, 1) > 0, "read \"%s\"", "test");
      compare_bytes (&c, buf + i, 1, i, "test");
    }
  close (fd);

  // get cold cache stats

  disk_reads = pdiskread ();
  disk_writes = pdiskwrites ();

  CHECK ((fd = open ("test")) > 1, "open \"%s\"", "test");
  for (i = 0; i < sizeof buf; i++)
    {
      char c;
      CHECK (read (fd, &c, 1) > 0, "read \"%s\"", "test");
      compare_bytes (&c, buf + i, 1, i, "test");
    }
  close (fd);

  int diff_cachemiss = pdiskread () + pdiskwrites () - (disk_reads + disk_writes);

  if (diff_cachemiss > 10) {
    fail("Cache stats similar to cold cache.");
  }
}

