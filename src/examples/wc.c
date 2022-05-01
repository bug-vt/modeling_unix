#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static int flag = 0x7;      /* There will be four flags, 0x1, 0x2, 0x3, and 0x4. 
                               These flags will determine the output of the program. 
                               0x7: All of the output,
                               0x1: Only bytes,
                               0x2: Only characters,
                               0x4: Only lines. */
static int file_descriptor = 0;
static int file_name_index = 0;
static long byte_count = 0;
static long char_count = 0;
static long line_count = 0;

static void
parse_input (void)
{
  uint8_t *buf = malloc (sizeof (uint8_t));
  size_t size;
  while ((size = read (file_descriptor, buf, 1))) {
    byte_count ++;
    if (!(*buf & 0x80))
      char_count ++;
    if (*((char*)buf) == '\n')
      line_count ++;
  }
  free (buf);
}


int
main (int argc, char *argv[]) 
{
  for (int index = 1; index < argc; index ++)
    {
      if (argv[index][0] == '-')
        {
          if (argv[index][1] == 'c')
            flag = 0x1;
          else if (argv[index][1] == 'm')
            flag = 0x2;
          else if (argv[index][1] == 'l')
            flag = 0x4;
        }
      else
        {
          file_descriptor = open (argv[index]);
          file_name_index = index;
          if (file_descriptor == -1)
            {
              printf ("Error in opening file %s\n", argv[index]);
              exit (-1);
            }
        }
    }
  parse_input ();
  if (flag == 0x7)
    printf ("\t%ld\t%ld\t%ld", line_count, char_count, byte_count);
  if (flag == 0x1)
    printf ("\t%ld", byte_count);
  if (flag == 0x2)
    printf ("\t%ld", char_count);
  if (flag == 0x4)
    printf ("\t%ld", line_count);
  if (!file_descriptor)
    printf ("\t%s", argv[file_name_index]);
  printf("\n");
}