#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

// SHM
#include <sys/shm.h>
#include <sys/stat.h>

#include <semaphore.h>
#include <string.h>

#include <errno.h>
#include <error.h>

#include <hsp/process-control.h>

#define NUM_PROCS      2
#define NUM_JOBS       10000000
#define BUFFER_SIZE    (1L<<20)

#define MIN(a, b) (((a)<(b))?(a):(b))
#define MAX(a, b) (((a)>(b))?(a):(b))

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

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
int create_shm (void *shm, int *fd, size_t size);
void fetch_work (wid_t wid, char *data);
int fork_worker (wid_t wid, char *exe);
int wait_service ();


struct process_control *ps_ctl;
int ps_ctl_fd;

struct worker_process worker_ps[MAX_PROCESSES];

/**
 * Wrapper-wide signal handler
 */
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
 * Create a shared memory segment, and mark it for removal.  As soon as all
 * attachments are gone, the segment will be destroyed by the OS.
 */
int
create_shm (void *shm, int *fd, size_t size)
{
  void **p = shm;
  int shm_fd;

  // Not accepting the returned buffer is a programming error
  assert(shm);

  // Create the SHM
  shm_fd = shmget(IPC_PRIVATE, size,
                  IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

  if (shm_fd < 0) {
    //nih_error_raise_printf(HSP_ERROR_FAILED,
    //    "Failed to create SHM: %s", strerror(errno));
    return errno;
  }

  // Attach
  (*p) = shmat(shm_fd, NULL, 0);
  if ((*p) == ((void *) -1)) {
    //nih_error_raise_printf(HSP_ERROR_FAILED,
    //    "Failed to create SHM: %s", strerror(errno));
    return errno;
  }

  // Mark for removal
  shmctl(shm_fd, IPC_RMID, NULL);

  // Return the created SHM
  if (fd) {
    *fd = shm_fd;
  }

  return 0;
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
    snprintf(env[0], ARRAY_SIZE(env[0]), PS_CTL_FD_ENVVAR "=%d\n", ps_ctl_fd);
    snprintf(env[1], ARRAY_SIZE(env[1]), WORKER_ID_ENVVAR "=%" PRI_WID "\n", wid);

    if (execle(exe, "test", "outputfile", "inputfile", NULL, env_list)) {
      fputs("Could not exec: ",stderr);
      fputs(strerror(errno),stderr);
      fputc('\n',stderr);
      exit(EXIT_FAILURE);
    }
  }
  else if (pid > 0) {
    worker_ps[wid].pid = pid;
    //worker_process[wid].status = 0;
    // Parent
    print_tod(stderr);
    return 0;
  }
  return -1;
}


int
wait_service ()
{
  return sem_wait(&ps_ctl->sem_service);
}


int
main (int argc, char **argv)
{
  //NihError *err;

  int forked = 0;
  int i, j, wid;

  struct timeval tv[2];

  if (argc != 2) {
    fputs("Invalid number of arguments\n", stderr);
    fputs("usage: hspwrap EXEFILE\n", stderr);
    exit(EXIT_FAILURE);
  }

  // Register signal and signal handler
  signal(SIGCHLD, sigchld_handler);

  // Create main "process control" structure
  if (create_shm(&ps_ctl, &ps_ctl_fd, sizeof(struct process_control))) {
    //err = nih_error_get();
    //fprintf(stderr, "Could not initalize file table: %s\n", err->message);
    fprintf(stderr, "Could not initalize file table\n");
    exit(EXIT_FAILURE);
  }

  // Process control data
  ps_ctl->nprocesses = MIN(NUM_PROCS, NUM_JOBS);
  printf("Processes: %d\n", ps_ctl->nprocesses);

  sem_init(&ps_ctl->sem_service, 1, 0);
  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    sem_init(&ps_ctl->process_lock[i], 1, 1);
    sem_init(&ps_ctl->process_ready[i], 1, 0);
    ps_ctl->process_state[i] = DONE;
    ps_ctl->process_cmd[i] = QUIT;
    worker_ps->wid = i;
    worker_ps->pid = 0;
    worker_ps->status = 0;
  }

  // Empty file table
  ps_ctl->ft.nfiles = 0;

  // Create bogus "input" and "output" buffers (one per process for testing)
  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    int   fd;
    void *shm;

    if (create_shm(&shm, &fd, BUFFER_SIZE)) {
      //err = nih_error_get();
      //fprintf(stderr, "Could not initalize file table entry: %s\n", err->message);
      fprintf(stderr, "Could not initalize file table entry\n");
      exit(EXIT_FAILURE);
    }
    if ((j = ps_ctl->ft.nfiles) < MAX_DB_FILES) {
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

    if (create_shm(&shm, &fd, BUFFER_SIZE)) {
      fprintf(stderr, "Could not initalize file table entry. Terminating.\n");
      exit(EXIT_FAILURE);
    }
    if ((j = ps_ctl->ft.nfiles) < MAX_DB_FILES) {
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

  gettimeofday(tv+0, NULL);
  for (i=NUM_JOBS; i;) {
    // Wait for a process to need service
    wait_service();
    for (wid = 0; wid < ps_ctl->nprocesses; ++wid) {
      sem_wait(&ps_ctl->process_lock[wid]);

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
	fetch_work(wid, "That's all folks!\n");
	// TODO: Move process_* changes to fetch_work(); ?
	ps_ctl->process_cmd[wid] = RUN;
	sem_post(&ps_ctl->process_ready[wid]);
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
        // Not a process of interest, move on.
        break;
      }
      sem_post(&ps_ctl->process_lock[wid]);
    }
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

  printf("Time taken: %lfs (%lfms average)\n",
      ((double)t) / 1000000.0,
      ((double)t) * ps_ctl->nprocesses / NUM_JOBS);

  return 0;
}
