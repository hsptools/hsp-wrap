#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

void
usage (FILE *fp)
{
  fprintf(fp, "Usage: hspclean [DIRECTORY]\n");
}


int
main (int argc, char **argv)
{
  char *dirn;
  DIR  *dirp;
  int   fildes;
  struct dirent *dp;

  switch (argc) {
  case 1:
    dirn = "/dev/shm";
    break;
  case 2:
    dirn = argv[1];
    break;
  default:
    fprintf(stderr, "Incorrect number of arguments.\n");
    usage(stderr);
    exit(EXIT_FAILURE);
  }

  dirp = opendir(dirn);
  fildes = open(dirn, O_RDONLY);

  if (!dirp || fildes == -1) {
    fprintf(stderr, "%s: Could not open: %s\n", dirn, strerror(errno));
    return EXIT_FAILURE;
  }

  while (1) {
    errno = 0;
    if ((dp = readdir(dirp)) != NULL) {
      if (strncmp(dp->d_name, "hspwrap.", 8) == 0) {
        // Found one, remove it
        if (unlinkat(fildes, dp->d_name) == -1) {
          fprintf(stderr, "%s: Could not remove file: %s\n", dp->d_name, strerror(errno));
        }
      }
    } else if (errno == 0) {
      // End of list
      closedir(dirp);
      break;
    } else {
      // Error 
      fprintf(stderr, "%s: Could not read directory: %s\n", dirn, strerror(errno));
      closedir(dirp);
      return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}
