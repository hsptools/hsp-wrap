#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "version.h"

#define PACKAGE_NAME "HSP ZSplit"
#define AUTHORS "Paul Giblock"
#define VERSION "0.1.0"

int
ioutil_open_w (const char *fpath, int force, int append)
{
  int flags = O_CREAT | O_WRONLY;
  int fd;

  // If not forced, then error if file exists
  if (!force) {
    flags |= O_EXCL;
  }
  // Append if request
  if (append) {
    flags |= O_APPEND;
  }

  fd = open(fpath, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IWUSR);

  if (fd < 0) {
    if (errno == EEXIST) {
      error(EXIT_FAILURE, 0, "%s: file exists, use --force to overwrite", fpath);
    } else {
      error(EXIT_FAILURE, 0, "%s: open failed", fpath);
    }
  }

  return fd;
}

void *
ioutil_mmap_r (char *fpath, off_t *size)
{
  int fd;
  struct stat st;
  void *start;

  fd = open(fpath, O_RDONLY);
  if (fd < 0) {
    error (EXIT_FAILURE, errno, "%s: open failed", fpath);
  }

  if (fstat(fd, &st) < 0) {
    close(fd);
    error (EXIT_FAILURE, errno, "%s: stat failed", fpath);
  }

  start = mmap(NULL, st.st_size,
	       PROT_READ, MAP_PRIVATE,
	       fd, 0);
  close(fd);

  if (start == MAP_FAILED) {
    error (EXIT_FAILURE, errno, "%s: mmap failed", fpath);
  }
  if (size) {
    *size = st.st_size;
  }
  return start;
}
