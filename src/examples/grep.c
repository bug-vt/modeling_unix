#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

int
main (int argc, char** argv)
{
  int out_size = 16;

  int out_index = 0;

  char * out_buffer = malloc(sizeof(char) * out_size);

  char in_buffer;

  
  int target_size = 0;
  while (*(argv[1] + target_size) != '\0')
    target_size ++;

  int check_size = 0;
  
  bool has_target = false;

  while (read(STDIN_FILENO, &in_buffer, 1))
  { 
    
    out_size = append_to(&in_buffer, 1, &out_buffer, out_index, out_size);
    out_index += 1;
    
    if (in_buffer == *(argv[1] + check_size))
    {
      check_size++;
      if (check_size == target_size)
      {
	has_target = true;
      }
    }
    else
    {
      check_size = 0;

      if ((int)(in_buffer) == 13)
      {
	*(out_buffer - 1 + out_index) = '\0';
        if (has_target)
        {
	  printf("%s", out_buffer);
	}
	out_index = 0;
        has_target = false;
      }
    }
  }
  
  if (has_target)
  {
    in_buffer = '\0';
    out_size = append_to(&in_buffer, 1, &out_buffer, out_index, out_size);
    printf("%s", out_buffer);
  }

  return EXIT_SUCCESS;
}

int append_to(char* from, int from_index, char** to, int to_index, int to_size)
{
  //printf("Beggining append, from index %d, to index %d, to index %d\n", from_index, to_index, to_size);
  if (from_index + to_index > to_size)
  {
    //printf("Doubling buffer\n");
    char* new_buffer = malloc(sizeof(char) * to_size * 2);
    
    //printf("Transfering to new buffer\n");
    for (int index = 0; index < to_size; index++)
    	*(new_buffer + index) = *(*to + index);
   
    //printf("Freeing old buffer\n");
    free(*to);
    *to = new_buffer;
    to_size = from_index + to_index;
  }

  //printf("Appending from input to output\n");
  for (int index = 0; index < from_index; index++)
    *(*to + index + to_index) = *(from + index);
  
  //printf("Returning\n");
  return to_size;
}
