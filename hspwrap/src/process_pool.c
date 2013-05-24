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

#include <hsp/process-control.h>
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
static int    fork_worker (wid_t wid, char *exe);

//// State

// Process ID of HSP-wrap process
int hspwrap_pid;
// Number of processes (same as ps_ctl->nprocesses)
int nprocesses;
// Keep track of PIDs assigned to workers
struct worker_process worker_ps[MAX_PROCESSES];

//// Definitions

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

  for (i = 0; i < nprocesses; ++i) {
    if (worker_ps[i].pid == pid) {
      return worker_ps[i].wid;
    }
  }
  return BAD_WID;
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
    snprintf(env[0], ARRAY_SIZE(env[0]), PID_ENVVAR "=%d", hspwrap_pid);
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
    worker_ps[wid].status = 1;

    // Set CPU infinity to one core
    // FIXME This masking isn't exactly right, but works for nproc==ncpu
    cpu_set_t *mask = CPU_ALLOC(nprocesses);
    size_t     size = CPU_ALLOC_SIZE(nprocesses);
    CPU_ZERO_S(size, mask);
    CPU_SET_S(wid, size, mask);
    CPU_FREE(mask);
    return 0;
  }
  return -1;
}


int
process_pool_start (pid_t wrapper_pid, int nproc, char *cmd)
{
  struct process_control *ps_ctl = NULL;

  pid_t exit_pid;
  wid_t wid;
  int forked, status;

  // Set global state for process_pool TL
  hspwrap_pid = wrapper_pid;
  nprocesses  = nproc;

  // Attach ps_ctl
  {
    char shmname[256];
    int fd;
    snprintf(shmname, 256, "/mcw.%d.%s", hspwrap_pid, PS_CTL_SHM_NAME);
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
    if (fork_worker(wid, cmd)) {
      fprintf(stderr, "Failed to fork worker %" PRI_WID "\n", wid);
    } else {
      printf("Worker %" PRI_WID " started.\n", wid);
      forked++;
    }
  }

  // Wait on process change, flag ps_ctl as needed
  while (forked) {
    exit_pid = wait(&status);
    wid = worker_for_pid(exit_pid);

    // Update state to "DONE"
    pthread_mutex_lock(&ps_ctl->lock);
    ps_ctl->process_state[wid] = DONE;
    ps_ctl->process_cmd[wid]   = NO_CMD;
    pthread_cond_signal(&ps_ctl->need_service);
    pthread_mutex_unlock(&ps_ctl->lock);
    
    forked--;
  }

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
