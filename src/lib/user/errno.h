#ifndef __LIB_USER_ERRNO_H
#define __LIB_USER_ERRNO_H

#define EINVF 13
#define EBADF 113
#define EISDIR 123
#define EMFILE 124

extern int errno;

void perror (const char *s);

#endif
