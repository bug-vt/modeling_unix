#include <stdio.h>
#include <syscall.h>
#include <string.h>


static void xor_encrypt_decrypt (char *msg);

int
main (int argc, char *argv[]) 
{
  bool success = true;
  int i;
  int fd = -1;

  if (argc > 2)
    {
      printf ("Usage: xor_cipher <file name>\n");
      return EXIT_FAILURE;
    }

  if (argc == 1)
    fd = STDIN_FILENO;
  
  for (i = 0; i < argc; i++) 
    {
      if (i == 0 && fd == -1)
        continue;

      if (i > 0)
        fd = open (argv[i]);

      if (fd < 0) 
        {
          printf ("%s: open failed\n", argv[i]);
          success = false;
          continue;
        }
      for (;;) 
        {
          char buffer[1024];
          int bytes_read = read (fd, buffer, sizeof buffer);
          if (bytes_read == 0)
            break;
          xor_encrypt_decrypt (buffer);
        }
      close (fd);
    }
  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void 
xor_encrypt_decrypt (char *msg)
{
  char xor_key = 'K';
  int len = strlen (msg);

  for (int i = 0; i < len; i++)
    {
      msg[i] = msg[i] ^ xor_key;
      printf ("%c", msg[i]);
    }
}
