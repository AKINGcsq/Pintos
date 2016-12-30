/* Tests that tell correctly returns the position of the next byte to be 
read or written in open file fd, as it should */

#include <random.h>
#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

char buf1[1234];

void
test_main (void) 
{
  const char *file_name = "deleteme";
  int fd;
  
  CHECK (create (file_name, sizeof buf1), "create \"%s\"", file_name);
  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
  random_bytes (buf1, sizeof buf1);
  CHECK (write (fd, buf1, sizeof buf1) > 0, "write \"%s\"", file_name);
  CHECK (tell (fd) == 1234, "tell \"%s\"", file_name);
  msg ("seek \"%s\" to 0", file_name);
  seek (fd, 0);
  CHECK (tell (fd) == 0, "tell \"%s\"", file_name);
  msg ("seek \"%s\" to 197", file_name);
  seek (fd, 197);
  CHECK (tell (fd) == 197, "tell \"%s\"", file_name);
  msg ("close \"%s\"", file_name);
  close (fd);
}
