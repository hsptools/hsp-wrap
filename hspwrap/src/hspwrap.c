#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

// SHM
#include <glib.h>
#include <sys/shm.h>
#include <sys/stat.h>

#include "errno.h"
#include "error.h"

#define NUM_PROCS 8
#define NUM_JOBS  1000

#define HSP_ERROR 1
enum HspError {
  HSP_ERROR_FAILED
};

void print_tod ();
int create_shm (void **shm, int *fd, size_t size, GError **err);


/**
 * Create a shared memory segment, and mark it for removal.  As soon as all
 * attachments are gone, the segment will be destroyed by the OS.
 */
int
create_shm (void **shm, int *fd, size_t size, GError **err)
{
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
  (*shm) = shmat(shm_fd, NULL, 0);
  if ((*shm) == ((void *) -1)) {
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

int
main (int argc, char **argv)
{
  int forked = 0;
  int st;
  int j;

  // Issue all jobs
  for (j=0; j<NUM_JOBS; ++j) {
    pid_t pid = fork();
    if (pid == 0) {
      // Child 
      execl("/bin/echo", "echo", "Hello World!", NULL);
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
    }
  }

  // Done issuing jobs, wait for outstanding procs
  for(; forked>0; --forked) {
    wait(&st);
    print_tod();
    printf("Proc exited with status %d. (winding down)\n", st);
  }

  return 0;
}
