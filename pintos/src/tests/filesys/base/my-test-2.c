/* Tests that tell correctly returns the position of the next byte to be 
read or written in open file fd, as it should */

#include <random.h>
#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

char buf1[64000];

void
test_main (void) 
{
  const char *file_name = "deleteme";
  int fd;
  
  CHECK (create (file_name, sizeof buf1), "create \"%s\"", file_name);

  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);

  int prevWriteCntBeforeTest = getWriteCnt (fd);
  random_bytes (buf1, sizeof buf1);
  CHECK (write (fd, buf1, sizeof buf1) > 0, "write \"%s\"", file_name);

  CHECK (tell (fd) == 64000, "tell \"%s\"", file_name);
  seek (fd, 0);
  CHECK (read (fd, buf1, sizeof buf1), "read \"%s\"", file_name);
  //msg("Write Cnt: %d", getWriteCnt(fd) - prevWriteCntBeforeTest);
  int writeCnt = getWriteCnt (fd) - prevWriteCntBeforeTest;
  if (writeCnt >= 256 || writeCnt <= 64) 
    {
      fail ("Does not Coalesce Writes");
    }

  close (fd);
}
