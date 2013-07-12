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
  int   cleaned, failed;
  struct dirent *dp;

  switch (argc) {
  case 1:
    dirn = "/dev/shm";
    break;
  case 2:
    if (strcmp(argv[1], "--help") == 0) {
      usage(stdout);
      return EXIT_SUCCESS;
    }
    dirn = argv[1];
    break;
  default:
    fprintf(stderr, "hspclean: Incorrect number of arguments.\n");
    usage(stderr);
    exit(EXIT_FAILURE);
  }

  cleaned = failed = 0;
  dirp = opendir(dirn);

  if (!dirp) {
    fprintf(stderr, "hspclean: %s: Could not open: %s\n", dirn, strerror(errno));
    return EXIT_FAILURE;
  }

  chdir(dirn);

  while (1) {
    errno = 0;
    if ((dp = readdir(dirp)) != NULL) {
      if (strncmp(dp->d_name, "hspwrap.", 8) == 0) {
        // Found one, remove it
        if (unlink(dp->d_name) == -1) {
          fprintf(stderr, "hspclean: %s: Could not remove file: %s\n", dp->d_name, strerror(errno));
	  failed++;
        } else {
	  cleaned++;
	}
      }
    } else if (errno == 0) {
      // End of list
      closedir(dirp);
      break;
    } else {
      // Error 
      fprintf(stderr, "hspclean: %s: Could not read directory: %s\n", dirn, strerror(errno));
      closedir(dirp);
      return EXIT_FAILURE;
    }
  }

  if (failed) {
    printf("hspclean: Removed %d files, %d failed.\n", cleaned, failed);
  } else {
    printf("hspclean: Removed %d files\n", cleaned);
  }

  return EXIT_SUCCESS;
}
