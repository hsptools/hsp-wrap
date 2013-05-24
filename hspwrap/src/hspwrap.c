#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

// SHM
#include <sys/shm.h>
#include <sys/stat.h>

#include <string.h>

#include <errno.h>
#include <error.h>

#include <hsp/process-control.h>
#include "process_pool.h"

#define NUM_PROCS      2
#define NUM_JOBS       100
#define BUFFER_SIZE    (1L<<20)

// TODO: Move to util lib
#define MIN(a, b) (((a)<(b))?(a):(b))
#define MAX(a, b) (((a)>(b))?(a):(b))

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ZERO_ARRAY(a) memset(a, 0, sizeof(a))

void print_tod (FILE *f);

static void *create_shm (char *name, long shmsz, int *fd);
static void  fetch_work (wid_t wid, char *data);
static void  fork_process_pool ();
static int   all_processes_done();
static int   all_processes_running();

//// State

struct process_control *ps_ctl;
int ps_ctl_fd;

//// Definitions

int
main (int argc, char **argv)
{
  int i, j, wid;

  struct timeval tv[2];
  pthread_mutexattr_t mattr;
  pthread_condattr_t  cattr;

  if (argc != 2) {
    fputs("Invalid number of arguments\n", stderr);
    fputs("usage: hspwrap EXEFILE\n", stderr);
    exit(EXIT_FAILURE);
  }

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
  ps_ctl = create_shm(PS_CTL_SHM_NAME, sizeof(struct process_control), &ps_ctl_fd);

  // Process control data
  ps_ctl->nprocesses = MIN(NUM_PROCS, NUM_JOBS);

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

  // Create bogus "input" and "output" buffers (one per process for testing)
  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    int   fd;
    void *shm;
    char  shmname[10];

    j = ps_ctl->ft.nfiles;
    snprintf(shmname, 10, "%d", j);
    shm = create_shm(shmname, BUFFER_SIZE, &fd);
    if (j < MAX_DB_FILES) {
      ps_ctl->ft.file[j].shm      = shm;
      ps_ctl->ft.file[j].shm_fd   = fd;
      ps_ctl->ft.file[j].shm_size = BUFFER_SIZE;
      ps_ctl->ft.file[j].wid      = i;
      ps_ctl->ft.file[j].size     = 0;
      sprintf(ps_ctl->ft.file[j].name, "inputfile");

      ps_ctl->ft.nfiles++;
    } else {
      fprintf(stderr, "Too many DB files; increase MAX_DB_FILES. Terminating.\n");
      exit(EXIT_FAILURE);
    }

    j = ps_ctl->ft.nfiles;
    snprintf(shmname, 10, "%d", j);
    shm = create_shm(shmname, BUFFER_SIZE, &fd);
    if (j < MAX_DB_FILES) {
      ps_ctl->ft.file[j].shm      = shm;
      ps_ctl->ft.file[j].shm_fd   = fd;
      ps_ctl->ft.file[j].shm_size = BUFFER_SIZE;
      ps_ctl->ft.file[j].wid      = i;
      ps_ctl->ft.file[j].size     = 0;
      sprintf(ps_ctl->ft.file[j].name, "outputfile");
      ps_ctl->ft.nfiles++;
    } else {
      fprintf(stderr, "Too many DB files; increase MAX_DB_FILES. Terminating.\n");
      exit(EXIT_FAILURE);
    }
  }

  // Now print some stats
  printf("Processes: %d\n", ps_ctl->nprocesses);
  printf("Process ID: %d\n\n", getpid());

  // Initial distribution
  for (wid = 0; wid < ps_ctl->nprocesses; ++wid) {
    // Fetch work into buffers for worker wid (set FTE size and shm data)
    fetch_work(wid, "Hello World!\n");

    // Prepare process block
    ps_ctl->process_state[wid] = RUNNING;
    ps_ctl->process_cmd[wid]   = RUN;
  }

  // Process control is setup, data is in place, start the processes
  fork_process_pool(argv[1]);
  
  fprintf(stderr, "Waiting for service requests.\n");

  // Count number of tasks assigned to each worker
  unsigned worker_iterations[MAX_PROCESSES];
  ZERO_ARRAY(worker_iterations);

  gettimeofday(tv+0, NULL);
  i=NUM_JOBS;
  while (1) {
    pthread_mutex_lock(&ps_ctl->lock);
    if (all_processes_done()) {
      // All processes are done! exit loop
      pthread_mutex_unlock(&ps_ctl->lock);
      break;
    }
    // Otherwise, wait for a process to need service
    while (all_processes_running()) {
      pthread_cond_wait(&ps_ctl->need_service, &ps_ctl->lock);
    }

    // Now, service all processes
    for (wid = 0; wid < ps_ctl->nprocesses; ++wid) {
      switch (ps_ctl->process_state[wid]) {
      case EOD:
        if (i) {
          // Still have data to distribute
          worker_iterations[wid]++;
          fetch_work(wid, "That's all folks!\n");
          // TODO: Move process_* changes to fetch_work(); ?
          ps_ctl->process_cmd[wid] = RUN;
          ps_ctl->process_state[wid] = RUNNING;
          pthread_cond_signal(&ps_ctl->process_ready[wid]);
          i--;
        } else {
          // No more data, tell it to quit
          fprintf(stderr, "Requesting worker %d to quit\n", wid);
          ps_ctl->process_cmd[wid] = QUIT;
          ps_ctl->process_state[wid] = RUNNING;
          pthread_cond_signal(&ps_ctl->process_ready[wid]);
        }
        break;
      case NOSPACE:
        fprintf(stderr, "PROCESS NO SPACE\n");
      case FAILED:
        fprintf(stderr, "PROCESS FAILED\n");
        break;
      case DONE:
        break;
      case IDLE:
      case RUNNING:
        // Not of interest, move on.
        break;
      }
    }
    // Done modifying process states
    pthread_mutex_unlock(&ps_ctl->lock);
  }

  gettimeofday(tv+1, NULL);

  // Dump output
  /*
  for (i=0; i<ps_ctl->ft.nfiles; ++i) {
    struct file_table_entry *f = ps_ctl->ft.file + i;
    if (!strcmp(f->name, "outputfile")) {
      printf("%d: %zu\n", i, f->size);
      fwrite(f->shm, 1, f->size, stdout);
      putchar('\n');
    }
  }
  */

  long t = (tv[1].tv_sec - tv[0].tv_sec) * 1000000
           + (tv[1].tv_usec - tv[0].tv_usec);

  putchar('\n');
  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    printf("Worker %2u iterations: %5u\n", i, worker_iterations[i]);
  }

  printf("Time taken: %lfs (%lfms average)\n",
      ((double)t) / 1000000.0,
      ((double)t) * ps_ctl->nprocesses / NUM_JOBS);

  return 0;
}

/**
 * Print time of day to file for logging purposes
 */
void
print_tod (FILE *f)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  fprintf(f, "[%ld.%06ld] ", tv.tv_sec, tv.tv_usec);
}


/**
 * Create a shared memory segment and map it into virtual memory.
 */
static void *
create_shm (char *name, long shmsz, int *fd)
{
  void *shm;
  int   shmfd;
  char  shmname[256];

  snprintf(shmname, 256, "/mcw.%d.%s", getpid(), name);

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


static void
fetch_work (wid_t wid, char *data)
{
  int i;
  struct file_table *ft;
  struct file_table_entry *f;
  size_t cnt = strlen(data);

  ft = &ps_ctl->ft;
  for (i = 0; i < ft->nfiles; ++i) {
    f = &ft->file[i];
    if (f->wid == wid && !strcmp(f->name, "inputfile")) {
      // Prepare data buffer(s)
      memcpy(f->shm, data, cnt);
      f->size = cnt;
    }
  }
}

static void
fork_process_pool (char *cmd)
{
  pid_t tmp_pid, hspwrap_pid, pool_pid;

  hspwrap_pid = getpid();

  if ((tmp_pid = fork()) > 0) {
    // parent process (done)
    return;
  } else if (!tmp_pid) {
    // temporary process
    if ((pool_pid = fork()) > 0) {
      // still in temporary process, kill ourself
      kill(getpid(), SIGKILL);
    } else if (!pool_pid) {
      // forker process
      process_pool_start(hspwrap_pid, ps_ctl->nprocesses, cmd);
    } else {
      fprintf(stderr, "Could not fork process pool.  Terminating.\n");
      exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
  } else {
    fprintf(stderr, "Could not fork temporary process.  Terminating.\n");
    exit(EXIT_FAILURE);
  }
}

static int
all_processes_done ()
{
  int i;
  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    if (ps_ctl->process_state[i] != DONE) {
      return 0;
    }
  }
  return 1;
}
static int
all_processes_running ()
{
  int i;
  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    if (ps_ctl->process_state[i] != RUNNING) {
      return 0;
    }
  }
  return 1;
}
