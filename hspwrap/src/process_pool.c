// Enable Linux sched support for cpu-affinity right now
#define _GNU_SOURCE 

#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
// For SHM/MMAP stuff
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <fcntl.h>

// Linux-specific (TODO: make configurable)
#include <sched.h>

#include <hsp/process_control.h>
#include "process_pool.h"

// TODO: Move to util lib
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

struct worker_process {
  wid_t wid;
  pid_t pid;
  int   status;
  sig_atomic_t status_changed;
};


static wid_t  worker_for_pid (pid_t pid);
static int    fork_worker (wid_t wid, const char *exe);
static struct process_pool_ctl *process_pool_ctl_init (pid_t hspwrap_pid);

//// State

// Process ID of HSP-wrap process
int hspwrap_pid;
// Number of processes (same as ps_ctl->nprocesses)
int nprocesses;
// Keep track of PIDs assigned to workers
struct worker_process worker_ps[MAX_PROCESSES];

//// Definitions


/**
 *  Lookup process by worker-id
 */
static wid_t
worker_for_pid (pid_t pid)
{
  int i;

  for (i = 0; i < nprocesses; ++i) {
    if (worker_ps[i].pid == pid) {
      return worker_ps[i].wid;
    }
  }
  return BAD_WID;
}


static int
fork_worker (wid_t wid, const char *exe)
{
  char env[2][40];
  //char *env_list[3] = {env[0], env[1], NULL};

  // Count environment variables
  int nenviron, i;
  for (nenviron=0; environ[nenviron]; ++nenviron) ;

  // Make new list FIXME: leak!
  char **env_list = malloc((nenviron + 3) * sizeof(char *));
  for (i=0; i<nenviron; ++i) {
    env_list[i] = environ[i];
  }
  env_list[i]   = env[0];
  env_list[i+1] = env[1];
  env_list[i+2] = NULL;

  // Start child
  pid_t pid = fork();

  if (pid == 0) {
    // Child
    snprintf(env[0], ARRAY_SIZE(env[0]), PID_ENVVAR "=%d", hspwrap_pid);
    snprintf(env[1], ARRAY_SIZE(env[1]), WORKER_ID_ENVVAR "=%" PRI_WID "\n", wid);

    // -p blastp -d nr-5m/nr-5m -i sample-16.fasta -o blast.out
    if (execle(exe, "blastall", "-p", "blastp", "-d", "nr-5m/nr-5m", "-i", "inputfile", "-o", "outputfile", "-m", "7", "-a", "1", NULL, env_list)) {
      fprintf(stderr, "Could not exec: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
    }
  }
  else if (pid > 0) {
    // Parent
    worker_ps[wid].pid = pid;
    worker_ps[wid].status = 1;

    // Set CPU infinity to one core
    // FIXME This masking isn't exactly right, but works for nproc==ncpu
    /*
    cpu_set_t *mask = CPU_ALLOC(nprocesses);
    size_t     size = CPU_ALLOC_SIZE(nprocesses);
    CPU_ZERO_S(size, mask);
    CPU_SET_S(wid, size, mask);
    CPU_FREE(mask);
    */
    return 0;
  }
  return -1;
}


static struct process_pool_ctl *
process_pool_ctl_init (pid_t hspwrap_pid)
{
  struct process_pool_ctl *pool_ctl;
  pthread_mutexattr_t mattr;
  pthread_condattr_t  cattr;
  char shmname[256];
  int fd;

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

  // Create SHM and mmap it
  snprintf(shmname, 256, "/hspwrap.%d.%s", hspwrap_pid, POOL_CTL_SHM_NAME);
  fd = shm_open(shmname, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (fd < 0) {
    fprintf(stderr, "Failed to make pool SHM: %s\n",strerror(errno));
    exit(EXIT_FAILURE);
  }
  if ((ftruncate(fd, sizeof(struct process_pool_ctl))) != 0) {
    fprintf(stderr, "Failed to resize pool SHM: %s\n",strerror(errno));
    close(fd);
    shm_unlink(shmname);
    exit(EXIT_FAILURE);
  }
  pool_ctl = mmap(NULL, sizeof(struct process_pool_ctl), PROT_READ | PROT_WRITE,
                  MAP_SHARED /*| MAP_LOCKED | MAP_HUGETLB*/,
                  fd, 0);

  if (pool_ctl == MAP_FAILED) {
    fprintf(stderr, "Failed to attach pool SHM. Terminating.\n");
    close(fd);
    shm_unlink(shmname);
    exit(EXIT_FAILURE);
  }

  // Initialize structure
  pool_ctl->nprocesses = 0;
  pool_ctl->ready = 0;
  pthread_mutex_init(&pool_ctl->lock, &mattr);
  pthread_cond_init(&pool_ctl->wait, &cattr);
  pthread_cond_init(&pool_ctl->run, &cattr);

  close(fd);
  shm_unlink(shmname);
  return pool_ctl;
}


struct process_pool_ctl *
process_pool_fork ()
{
  struct process_pool_ctl *pool_ctl;
  pid_t  tmp_pid, hspwrap_pid, pool_pid;

  hspwrap_pid = getpid();

  // Create control structure
  pool_ctl = process_pool_ctl_init(hspwrap_pid);

  // Daemonize the process pool
  if ((tmp_pid = fork()) > 0) {
    // parent process, return once pool is created
    pthread_mutex_lock(&pool_ctl->lock);
    while (!pool_ctl->ready) {
      pthread_cond_wait(&pool_ctl->wait, &pool_ctl->lock);
    }
    pthread_mutex_unlock(&pool_ctl->lock);

    return pool_ctl;

  } else if (!tmp_pid) {
    // Temporary process, reforks
    if ((pool_pid = fork()) > 0) {
      // Still in temporary process, kill ourself
      kill(getpid(), SIGKILL);
    } else if (!pool_pid) {
      // Forker process

      // Change file-mode mask to no permissions
      umask(0);

      // Run in a new session / process group
      if (setsid() < 0) {
        fprintf(stderr, "Couldn't successfully setsid()\n");
        exit(EXIT_FAILURE);
      }

      // Remap outputs (I want this thing to be totally independent)
      /*
      freopen("/dev/null", "r", stdin);
      freopen("/dev/null", "w", stdout);
      freopen("/dev/null", "w", stderr);
      */

      // Let main procces know we are ready, and wait for signal to spawn
      pthread_mutex_lock(&pool_ctl->lock);
      pool_ctl->ready = 1;
      pthread_cond_signal(&pool_ctl->wait);
      while (pool_ctl->nprocesses == 0) {
        pthread_cond_wait(&pool_ctl->run, &pool_ctl->lock);
      }
      pthread_mutex_unlock(&pool_ctl->lock);

      // Start child processes
      if (pool_ctl->nprocesses > 0) {
        fprintf(stderr, "Spawning process pool... (%d processes)\n", pool_ctl->nprocesses);
        process_pool_start(hspwrap_pid, pool_ctl->workdir, pool_ctl->nprocesses);
      } else {
        fprintf(stderr, "Killing process pool...\n");
      }
    } else {
      fprintf(stderr, "Could not fork process pool.  Terminating.\n");
      exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
  } else {
    fprintf(stderr, "Could not fork temporary process.  Terminating.\n");
    exit(EXIT_FAILURE);
  }
  return NULL;
}


int
process_pool_start (pid_t wrapper_pid, const char *workdir, int nproc)
{
  struct process_control *ps_ctl = NULL;

  pid_t exit_pid;
  wid_t wid;
  int forked, status;

  // Set global state for process_pool TL
  fprintf(stderr, "Process pool pid %d under wrapper pid %d is starting...\n"
      "cwd:       %s\n"
      "workdir:   %s\n"
      "processes: %d\n",
      getpid(), wrapper_pid, getcwd(NULL, 0), workdir, nproc);

  if (chdir(workdir)) {
    fprintf(stderr, "Pool failed to chdir: %s\n", strerror(errno));
  }
  hspwrap_pid = wrapper_pid;
  nprocesses  = nproc;

  // Attach ps_ctl TODO: Refactor (we use this here and in stdiowrap)
  {
    char shmname[256];
    int fd;
    snprintf(shmname, 256, "/hspwrap.%d.%s", hspwrap_pid, PS_CTL_SHM_NAME);
    fd = shm_open(shmname, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

    struct stat st;
    fstat(fd, &st);
    ps_ctl = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, 
                  MAP_SHARED /*| MAP_LOCKED | MAP_HUGETLB*/,
                  fd, 0);
    if (ps_ctl == MAP_FAILED) {
      fprintf(stderr,"process_pool: Failed to attach index SHM (%s): %s\n", shmname, strerror(errno));
      exit(1);
    }
  }

  // Initialize worker-process data
  for (wid = 0; wid < nprocesses; ++wid) {
    worker_ps[wid].wid = wid;
    worker_ps[wid].pid = 0;
    worker_ps[wid].status = 0;
  }

  // Fork all our children
  for (wid = 0, forked = 0; wid < nprocesses; ++wid) {
    if (fork_worker(wid, "exefile")) {
      fprintf(stderr, "Failed to fork worker %" PRI_WID "\n", wid);
    } else {
      fprintf(stderr, "Worker %" PRI_WID " started.\n", wid);
      forked++;
    }
  }

  // Wait on process change, flag ps_ctl as needed
  while (forked) {
    exit_pid = wait(&status);
    wid = worker_for_pid(exit_pid);
    fprintf(stderr, "Worker %d (pid %d) exited", wid, exit_pid);
    if (WIFEXITED(status)) {
      fprintf(stderr, " with status %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      fprintf(stderr, " with signal %d\n", WTERMSIG(status));
    } else {
      fputc('\n', stderr);
    }

    // Update state to "DONE"
    pthread_mutex_lock(&ps_ctl->lock);
    ps_ctl->process_state[wid] = DONE;
    ps_ctl->process_cmd[wid]   = NO_CMD;
    pthread_cond_signal(&ps_ctl->need_service);
    pthread_mutex_unlock(&ps_ctl->lock);

    forked--;
  }

  fprintf(stderr, "Process pool pid %d under wrapper pid %d is exiting...\n",
      getpid(), wrapper_pid);

  /*
  // Set any state from ended processes
  if (worker_ps[wid].status_changed) {
    if (WIFSIGNALED(worker_ps[wid].status)) {
      ps_ctl->process_state[wid] = FAILED;
    } else if (WIFEXITED(worker_ps[wid].status)) {
      ps_ctl->process_state[wid] = DONE;
    }
    worker_ps[wid].status_changed = 0;
  }
  */

  return 0;
}


void
process_pool_spawn (struct process_pool_ctl *pool_ctl, const char *workdir, int nprocs)
{
  pool_ctl->nprocesses = nprocs;
  strncpy(pool_ctl->workdir, workdir, PATH_MAX);
  pthread_cond_signal(&pool_ctl->run);
}

