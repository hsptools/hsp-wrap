#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
  int c;

  while( (c=fgetc(stdin)) != EOF ) {
    if( c == 1 ) {
      break;
    } else {
      fputc(c,stdout);
    }
  }

  return 0;
}
