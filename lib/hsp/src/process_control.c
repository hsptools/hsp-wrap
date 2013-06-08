#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/shm.h>

#include "hsp/process_control.h"

static void *create_shm (const char *name, long shmsz, int *fd);

struct process_control *
ps_ctl_init (unsigned nprocesses, int *ps_ctl_fd)
{
  struct process_control *ps_ctl;
  pthread_mutexattr_t mattr;
  pthread_condattr_t  cattr;
  int fd, i;

  // inter-process mutexes
  if (pthread_mutexattr_init(&mattr)) {
    fprintf(stderr, "Could not initalize mutex attributes\n");
    exit(EXIT_FAILURE);
  }
  if (pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED)) {
    fprintf(stderr, "Could not set mutex attributes\n");
    exit(EXIT_FAILURE);
  }

  // inter-process condition variables
  if (pthread_condattr_init(&cattr)) {
    fprintf(stderr, "Could not initalize condition variable attributes\n");
    exit(EXIT_FAILURE);
  }
  if (pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED)) {
    fprintf(stderr, "Could not set condition variable attributes\n");
    exit(EXIT_FAILURE);
  }

  // Create main "process control" structure
  ps_ctl = create_shm(PS_CTL_SHM_NAME, sizeof(struct process_control), &fd);

  // Process control data
  ps_ctl->nprocesses = nprocesses;

  pthread_mutex_init(&ps_ctl->lock, &mattr);
  pthread_cond_init(&ps_ctl->need_service, &cattr);

  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    pthread_cond_init(&ps_ctl->process_ready[i], &cattr);
    ps_ctl->process_state[i] = DONE;
    ps_ctl->process_cmd[i] = QUIT;
  }

  // Cleanup
  if (pthread_mutexattr_destroy(&mattr)) {
    fprintf(stderr, "Could not free mutex attributes\n");
    exit(EXIT_FAILURE);
  }
  if (pthread_condattr_destroy(&cattr)) {
    fprintf(stderr, "Could not free condition variable attributes\n");
    exit(EXIT_FAILURE);
  }

  // Empty file table
  ps_ctl->ft.nfiles = 0;

  if (ps_ctl_fd) {
    *ps_ctl_fd = fd;
  }
  return ps_ctl;
}


void *
ps_ctl_add_file (struct process_control *ps_ctl, wid_t wid, const char *name, size_t sz)
{
    void *shm;
    int   fd, j;
    char  shmname[8];

    j = ps_ctl->ft.nfiles;
    snprintf(shmname, sizeof(shmname), "%d", j);
    shm = create_shm(shmname, sz, &fd);
    if (j < MAX_DB_FILES) {
      ps_ctl->ft.file[j].shm      = shm;
      ps_ctl->ft.file[j].shm_fd   = fd;
      ps_ctl->ft.file[j].shm_size = sz;
      ps_ctl->ft.file[j].wid      = wid;
      ps_ctl->ft.file[j].size     = (wid == -1) ? sz : 0;
      strcpy(ps_ctl->ft.file[j].name, name);

      ps_ctl->ft.nfiles++;
    } else {
      fprintf(stderr, "Too many DB files; increase MAX_DB_FILES. Terminating.\n");
      exit(EXIT_FAILURE);
    }
    return shm;
}


int
ps_ctl_all_done (struct process_control *ps_ctl)
{
  int i;
  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    if (ps_ctl->process_state[i] != DONE) {
      return 0;
    }
  }
  return 1;
}


int
ps_ctl_all_running (struct process_control *ps_ctl)
{
  int i;
  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    if (ps_ctl->process_state[i] != RUNNING) {
      return 0;
    }
  }
  return 1;
}


void
ps_ctl_print (struct process_control *ps_ctl, FILE *f)
{
  struct file_table_entry *fte;
  int i;

  for (i=0; i < ps_ctl->ft.nfiles; ++i) {
    fte = &ps_ctl->ft.file[i];
    fprintf(f, "  file: %4d wid: %4d path: %30s size: %zu\n",
            i, fte->wid, fte->name, fte->shm_size);
  }
}


/**
 * Create a shared memory segment and map it into virtual memory.
 */
static void *
create_shm (const char *name, long shmsz, int *fd)
{
  void *shm;
  int   shmfd;
  char  shmname[256];

  snprintf(shmname, 256, "/hspwrap.%d.%s", getpid(), name);

  // Create the shared memory segment, and then mark for removal.
  // As soon as all attachments are gone, the segment will be
  // destroyed by the OS.
  shmfd = shm_open(shmname, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (shmfd < 0) {
    fprintf(stderr, "Failed to make SHM of size %ld: %s. Terminating.\n",shmsz,strerror(errno));
    exit(EXIT_FAILURE);
  }
  if ((ftruncate(shmfd, shmsz)) != 0) {
    fprintf(stderr, "Failed to resize SHM (%d).  Terminating.\n",errno);
    exit(EXIT_FAILURE);
  }
  shm = mmap(NULL, shmsz, PROT_READ | PROT_WRITE,
             MAP_SHARED /*| MAP_LOCKED | MAP_HUGETLB*/,
             shmfd, 0);

  if (shm == MAP_FAILED) {
    fprintf(stderr, "Failed to attach SHM. Terminating.\n");
    exit(EXIT_FAILURE);
  }
  
  // Return the created SHM
  if (fd) {
    *fd = shmfd;
  }
  return shm;
}




