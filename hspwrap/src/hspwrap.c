#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

// SHM
#include <glib.h>
#include <sys/shm.h>
#include <sys/stat.h>

#include <semaphore.h>

#include "errno.h"
#include "error.h"

#define MAX_PROCESSES  8
#define NUM_PROCS      8
#define NUM_JOBS       8
#define MAX_DB_FILES   256
#define BUFFER_SIZE    (1L<<20)
#define MAX_FILE_PATH  256
#define PS_CTL_FD_ENVVAR "HSPWRAP_CTL_SHM_FD"
#define WORKER_ID_ENVVAR "HSPWRAP_WID"

#define HSP_ERROR 1
enum HspError {
  HSP_ERROR_FAILED
};

typedef uint16_t wid_t;

/**
 * File description for a virtual file
 */
typedef struct {
  // Public
  size_t shm_size;            // Size of the shm (needed for detach)
  int    shm_fd;              // File descriptor of the shm

  wid_t  wid;                 // Worker ID owning this file, -1 if shared
  size_t size;                // Size of the actual file data within the SHM
  char   name[MAX_FILE_PATH]; // Virtual name (path) of the file 
  // Private (TODO: Move outside of file_table)
  char  *shm;                 // Pointer to shared data
} file_table_entry_t;

/**
 * Table of all available virtual files
 */
typedef struct {
  int                nfiles;
  file_table_entry_t file[MAX_DB_FILES];
} file_table_t;

/**
 * Shared process control structure.  Layout of SHM.
 */
typedef struct {
  int   nprocesses;               // Number of processes
  sem_t sem_empty;                // Signal exhausted data to parent process
  sem_t sem_avail[MAX_PROCESSES]; // Signal available data to child processes

  file_table_t ft;                // File descriptors
} process_control_t;




void print_tod ();
int create_shm (void *shm, int *fd, size_t size, GError **err);


/**
 * Create a shared memory segment, and mark it for removal.  As soon as all
 * attachments are gone, the segment will be destroyed by the OS.
 */
int
create_shm (void *shm, int *fd, size_t size, GError **err)
{
  void **p = shm;
  int shm_fd;

  // Not accepting the returned buffer is a programming error
  g_assert(shm);

  // Create the SHM
  shm_fd = shmget(IPC_PRIVATE, size,
                  IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

  if (shm_fd < 0) {
    g_set_error(err, HSP_ERROR, HSP_ERROR_FAILED,
                "Failed to create SHM: %s", g_strerror(errno));
    return errno;
  }

  // Attach
  (*p) = shmat(shm_fd, NULL, 0);
  if ((*p) == ((void *) -1)) {
    g_set_error(err, HSP_ERROR, HSP_ERROR_FAILED,
                "Failed to create SHM: %s", g_strerror(errno));
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
print_tod ()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  printf("[%ld.%06ld] ", tv.tv_sec, tv.tv_usec);
}



process_control_t *ps_ctl;
int                ps_ctl_fd;

int
main (int argc, char **argv)
{
  GError *err;

  int forked = 0;
  int st;
  int i, j;

  // Create main "process control" structure
  if (create_shm(&ps_ctl, &ps_ctl_fd, sizeof(process_control_t), &err)) {
    fprintf(stderr, "Could not initalize file table: %s\n", err->message);
    exit(EXIT_FAILURE);
  }
  // Process control data
  ps_ctl->nprocesses = NUM_PROCS;
  sem_init(&ps_ctl->sem_empty, 0, 0);
  for (i=0; i<NUM_PROCS; ++i) {
    sem_init(&ps_ctl->sem_avail[i], 0, 0);
  }
  // Empty file table
  ps_ctl->ft.nfiles = 0;

  // Create bogus "input" and "output" buffers (one per process for testing)
  for (i=0; i<NUM_PROCS; ++i) {
    int   fd;
    void *shm;

    if (create_shm(&shm, &fd, BUFFER_SIZE, &err)) {
      fprintf(stderr, "Could not initalize file table entry: %s\n", err->message);
      exit(EXIT_FAILURE);
    }
    if ((j = ps_ctl->ft.nfiles) < MAX_DB_FILES) {
      ps_ctl->ft.file[j].shm      = shm;
      ps_ctl->ft.file[j].shm_fd   = fd;
      ps_ctl->ft.file[j].shm_size = BUFFER_SIZE;
      ps_ctl->ft.file[j].wid      = i;
      ps_ctl->ft.file[j].size     = 0;
      sprintf(ps_ctl->ft.file[j].name, "inputfile");

      memcpy(ps_ctl->ft.file[j].shm, "Hello World!\n", 13);
      ps_ctl->ft.file[j].size = 13;

      ps_ctl->ft.nfiles++;
    } else {
      fprintf(stderr, "Too many DB files; increase MAX_DB_FILES. Terminating.\n");
      exit(EXIT_FAILURE);
    }

    if (create_shm(&shm, &fd, BUFFER_SIZE, &err)) {
      fprintf(stderr, "Could not initalize file table entry: %s\n", err->message);
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

  char *env[3] = {NULL, NULL, NULL};

  // Issue all jobs
  for (j=0; j<NUM_JOBS; ++j) {
	  printf("ISSUING %d\n", j);
    pid_t pid = fork();
    if (pid == 0) {
      // Child
      env[0] = g_strdup_printf(PS_CTL_FD_ENVVAR "=%d\n", ps_ctl_fd);
      env[1] = g_strdup_printf(WORKER_ID_ENVVAR "=%d\n", j);
      if (execle("/home/llama/jics/hsp/build/tools/mcwcat", "mcwcat", "outputfile", "inputfile", NULL, env)) {
    	fprintf(stderr, "Could not exec: %s\n", g_strerror(errno));
	exit(EXIT_FAILURE);
      }
    }
    else if (pid > 0) {
      // Parent
      print_tod();
      printf("Child process %d started.\n", pid);

      if (++forked == NUM_PROCS) {
        // There are already enough pooled. wait for a "slot"
        wait(&st);
        print_tod();
        printf("Proc exited with status %d.\n", st);
        --forked;
      }
    }
    else {
      // Error
      g_assert_not_reached();
    }
  }

  // Done issuing jobs, wait for outstanding procs
  for(; forked>0; --forked) {
    wait(&st);
    print_tod();
    printf("Proc exited with status %d. (winding down)\n", st);
  }

  // Dump output
  for (i=0; i<ps_ctl->ft.nfiles; ++i) {
    file_table_entry_t *f = ps_ctl->ft.file + i;
    if (!strcmp(f->name, "outputfile")) {
      printf("%d: %d\n", i, f->size);
      fwrite(f->shm, 1, f->size, stdout);
      putchar('\n');
    }
  }

  return 0;
}
