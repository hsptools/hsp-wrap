// Enable Linux sched support for cpu-affinity right now
#define _GNU_SOURCE 

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

// Linux-specific (TODO: make configurable)
#include <sched.h>

#include <hsp/process-control.h>

#define NUM_PROCS      2
#define NUM_JOBS       100
#define BUFFER_SIZE    (1L<<20)

#define MIN(a, b) (((a)<(b))?(a):(b))
#define MAX(a, b) (((a)>(b))?(a):(b))

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ZERO_ARRAY(a) memset(a, 0, sizeof(a))

#define HSP_ERROR 1
enum HspError {
  HSP_ERROR_FAILED
};


struct worker_process {
  wid_t wid;
  pid_t pid;
  int   status;
  sig_atomic_t status_changed;
};


void sigchld_handler (int signo);

void print_tod (FILE *f);

wid_t worker_for_pid (pid_t pid);
static void *create_shm (char *name, long shmsz, int *fd);
void fetch_work (wid_t wid, char *data);
int fork_worker (wid_t wid, char *exe);


struct process_control *ps_ctl;
int ps_ctl_fd;

struct worker_process worker_ps[MAX_PROCESSES];

/**
 * Wrapper-wide signal handler
 */
/*
void
sigchld_handler (int signo)
{
  int orig_errno = errno;
  register pid_t pid;
  int w;
  wid_t wid;

  while (1) {
    // Deal with interruptions and the pid not being available yet
    do {
      errno = 0;
      pid = waitpid (WAIT_ANY, &w, WNOHANG | WUNTRACED);
    } while (pid <= 0 && errno == EINTR);

    if (pid <= 0) {
      // No more stopped or terminated children left
      errno = orig_errno;
      return;
    }

    wid = worker_for_pid(pid);
    if (wid != BAD_WID) {
      worker_ps[wid].status = w;
      worker_ps[wid].status_changed = 1;
      sem_post(&ps_ctl->sem_service);
    }
  }
}
*/

/**
 *  Lookup process by worker-id
 */
wid_t
worker_for_pid (pid_t pid)
{
  int i;

  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    if (worker_ps[i].pid == pid) {
      return worker_ps[i].wid;
    }
  }
  return BAD_WID;
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


void
print_tod (FILE *f)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  fprintf(f, "[%ld.%06ld] ", tv.tv_sec, tv.tv_usec);
}


void
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


int
fork_worker (wid_t wid, char *exe)
{
  char env[2][40];
  char *env_list[3] = {env[0], env[1], NULL};

  // Start child
  pid_t pid = fork();

  if (pid == 0) {
    // Child
    snprintf(env[0], ARRAY_SIZE(env[0]), PID_ENVVAR "=%d", getppid());
    snprintf(env[1], ARRAY_SIZE(env[1]), WORKER_ID_ENVVAR "=%" PRI_WID "\n", wid);

    if (execle(exe, "test", "outputfile", "inputfile", NULL, env_list)) {
      fputs("Could not exec: ",stderr);
      fputs(strerror(errno),stderr);
      fputc('\n',stderr);
      exit(EXIT_FAILURE);
    }
  }
  else if (pid > 0) {
    // Parent
    worker_ps[wid].pid = pid;
    //worker_process[wid].status = 0;

    // Set CPU infinity to one core
    // FIXME This masking isn't exactly right, but works for nproc==ncpu
    cpu_set_t *mask = CPU_ALLOC(ps_ctl->nprocesses);
    size_t     size = CPU_ALLOC_SIZE(ps_ctl->nprocesses);
    CPU_ZERO_S(size, mask);
    CPU_SET_S(wid, size, mask);

    printf("W%u affinity: %d\n", wid, sched_setaffinity(pid, size, mask));

    CPU_FREE(mask);
    return 0;
  }
  return -1;
}


int
all_processes_running()
{
  int i;
  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    if (ps_ctl->process_state[i] != RUNNING) {
      return 0;
    }
  }
  return 1;
}

int
main (int argc, char **argv)
{
  //NihError *err;

  int forked = 0;
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
    worker_ps->wid = i;
    worker_ps->pid = 0;
    worker_ps->status = 0;
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
  printf("Process ID: %d\n", getpid());

  // Forker code
	// DUDE! Forker doesn't need to handle ps_ctl requests at all!
	// Instead, the forker is just responsible for waiting on child death and
	// respawn the process, lock ps_ctl, update struct, and signal for service.
	create_shm(
static void *create_shm (char *name, long shmsz, int *fd);

  // Initial distribution
  for (wid = 0; wid < ps_ctl->nprocesses; ++wid) {
    // Fetch work into buffers for worker wid (set FTE size and shm data)
    fetch_work(wid, "Hello World!\n");

    // Prepare process block
    ps_ctl->process_state[wid] = RUNNING;
    ps_ctl->process_cmd[wid]   = RUN;

    // Fork process, set env-vars, execute binary
    if (fork_worker(wid, argv[1])) {
      fprintf(stderr, "Failed to fork worker %" PRI_WID "\n", wid);
    } else {
      printf("Worker %" PRI_WID " started.\n", wid);
      forked++;
    }
  }

  fprintf(stderr, "Waiting for service requests. (%d processes)\n", forked);

  // Count number of tasks assigned to each worker
  unsigned worker_iterations[MAX_PROCESSES];
  ZERO_ARRAY(worker_iterations);

  gettimeofday(tv+0, NULL);
  for (i=NUM_JOBS; i;) {
    // Wait for a process to need service
    pthread_mutex_lock(&ps_ctl->lock);
    while (all_processes_running()) {
      pthread_cond_wait(&ps_ctl->need_service, &ps_ctl->lock);
    }

    // Now, service all processes
    for (wid = 0; wid < ps_ctl->nprocesses; ++wid) {
      // Set any state from ended processes
      if (worker_ps[wid].status_changed) {
        if (WIFSIGNALED(worker_ps[wid].status)) {
          ps_ctl->process_state[wid] = FAILED;
        } else if (WIFEXITED(worker_ps[wid].status)) {
          ps_ctl->process_state[wid] = DONE;
        }
        worker_ps[wid].status_changed = 0;
      }

      switch (ps_ctl->process_state[wid]) {
      case EOD:
        worker_iterations[wid]++;
        fetch_work(wid, "That's all folks!\n");
        // TODO: Move process_* changes to fetch_work(); ?
        ps_ctl->process_cmd[wid] = RUN;
        pthread_cond_signal(&ps_ctl->process_ready[wid]);
        i--;
        break;
      case NOSPACE:
        fprintf(stderr, "PROCESS NO SPACE\n");
      case FAILED:
        fprintf(stderr, "PROCESS FAILED\n");
        ps_ctl->process_state[wid] = IDLE;
        worker_ps[wid].pid = 0;
        worker_ps[wid].status = 0;
        break;
      case DONE:
        fprintf(stderr, "PROCESS QUIT\n");
        ps_ctl->process_state[wid] = IDLE;
        worker_ps[wid].pid = 0;
        worker_ps[wid].status = 0;
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

  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    printf("Worker %2u iterations: %5u\n", i, worker_iterations[i]);
  }

  printf("Time taken: %lfs (%lfms average)\n",
      ((double)t) / 1000000.0,
      ((double)t) * ps_ctl->nprocesses / NUM_JOBS);

  return 0;
}
