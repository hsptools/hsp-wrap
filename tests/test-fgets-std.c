#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>

int
main(int argc, char **argv)
{
  FILE *outf, *inf;
  char *db;
  char  c[40];
  int   dbfd, i;
  
  srand(getpid());

  outf = fopen(argv[1], "w");
  inf  = fopen(argv[3], "r");
  dbfd = open(argv[2], O_RDONLY);

  if (!outf || !inf || !dbfd) {
    fprintf(stderr, "Could not open file(s)\n");
    exit(EXIT_FAILURE);
  }

  int pagesize = getpagesize();
  db = mmap(NULL, pagesize, PROT_READ, MAP_SHARED, dbfd, 0);

  for (i=0; fgets(c, sizeof(c), inf) != NULL; ++i) {
    puts("$ GOT SOME DATA");
    sleep(rand()%3);
    fputs(db, outf);
    fputc(' ', outf);
    fputs(c, outf);
  }

  fclose(inf);
  fclose(outf);

  return EXIT_SUCCESS;
}
