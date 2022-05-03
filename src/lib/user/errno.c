#include "errno.h"
#include <stdio.h>


int errno = 0;

void
perror (const char *s)
{
  printf ("%s : ", s);

  switch (errno)
    {
      case 0:
        printf ("Success");
        break;

      case EINVF:
        printf ("Invalid file descriptor");
        break;

      case EBADF:
        printf ("Bad file descriptor");
        break;

      case EISDIR:
        printf ("Is a directory");
        break;

      case EMFILE:
        printf ("Too many open files");
        break;

      case ENAME:
        printf ("Invalid file name length");
        break;

      case ENOENT:
        printf ("No such file or directory");
        break;

      default:
        printf ("Unknown errno");
        break;
    }

  printf ("\n");
}
