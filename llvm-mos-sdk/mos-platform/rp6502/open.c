#include "rp6502.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>

int open(const char *name, int flags, ...) {
  size_t namelen = strlen(name);
  if (namelen > 255) {
    errno = EINVAL;
    return -1;
  }
  while (namelen)
    ria_push_char(name[--namelen]);
  ria_set_ax(flags);
  return ria_call_int(RIA_OP_OPEN);
}
