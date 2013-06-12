#ifndef PROCESS_POOL_H__
#define PROCESS_POOL_H__

#include <limits.h> // PATH_MAX
#include <unistd.h> // pid_t

#define POOL_CTL_SHM_NAME "pool_ctl"

struct process_pool_ctl {
  pthread_mutex_t lock; // Structure lock
  pthread_cond_t  wait;	// Block on create until pool is started
  pthread_cond_t  run;	// Pool blocks until slave is ready to run
  int             ready;
  int             nprocesses;  // Command for run
  char            workdir[PATH_MAX];  // directory to switch to
};
  

int process_pool_start (pid_t wrapper_pid, const char *workdir, int nproc);
struct process_pool_ctl *process_pool_fork ();
void process_pool_spawn (struct process_pool_ctl *pool_ctl, const char *workdir, int nprocs);

#endif // PROCESS_POOL_H__
