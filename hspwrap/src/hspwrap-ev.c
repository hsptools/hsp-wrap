#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

// SHM
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/stat.h>

#include <aio.h>
#include <ev/ev.h>

#include "errno.h"
#include "error.h"

#define NUM_PROCS 4
#define NUM_JOBS  100

#define HSP_ERROR 1
enum HspError {
  HSP_ERROR_FAILED
};

void print_tod ();

ev_io    input_watcher[NUM_PROCS];
struct   aiocb aios[NUM_PROCS];
int      fds[NUM_PROCS];
int      remaining = NUM_JOBS;

void
print_tod ()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  printf("[%ld.%06ld] ", tv.tv_sec, tv.tv_usec);
}


static void
input_cb (EV_P_ struct ev_io *w, int revents)
{
  int i = (int)w->data;
  if (remaining <= 0) {
	  printf("Killing watcher %d", i);
    // Done
    ev_io_stop(EV_A_ w);
    //ev_unloop(EV_A_ EVUNLOOP_ALL);
  } else {
#if 1
    write(w->fd, "That's all folks!\n", 18);
#else
    aios[i].aio_fildes = w->fd;
    aios[i].aio_buf    = "That's all folks!\n";
    aios[i].aio_nbytes = 18;
    aio_write(aios + i);
#endif
    remaining--;
  }
}


int
get_rundir()
{
  const char * const dirname = "hspwrap";
  char *rootdir;
  int   f_root, f_run;

  // TODO: Check for mkdirat and friends
  if (!(rootdir = getenv("XDG_RUNTIME_DIR"))) {
    return -1;
  }
  printf("Runtime dir: %s\n", rootdir);

  if ((f_root = open(rootdir, O_DIRECTORY)) < 0) {
    fprintf(stderr, "%s: could not open: %s\n", rootdir, strerror(errno));
    return -1;
  }

  if ((mkdirat(f_root, dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) != 0) {
    fprintf(stderr, "%s/%s: could not make directory: %s\n", rootdir, dirname, strerror(errno));
    return -1;
  }

  if ((f_run = openat(f_root, dirname, O_DIRECTORY)) < 0) {
    fprintf(stderr, "%s/%s: could not open: %s\n", rootdir, dirname, strerror(errno));
    return -1;
  }
  return f_run;
}

char *gnu_basename(char *path)
{
	    char *base = strrchr(path, '/');
	        return base ? base+1 : path;
}

int
main (int argc, char **argv)
{
  struct ev_loop *loop;
  struct timeval  tv[2];

  char dirname[10];
  int  rundir, rundirs[NUM_PROCS], i;
 
  rundir = get_rundir();
  
  // Prepare worker rundirs
  for (i=0; i<NUM_PROCS; ++i) {
    sprintf(dirname, "%d", i);
    if ((mkdirat(rundir, dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) != 0) {
      fprintf(stderr, "worker %d: could not make runtime directory: %s\n", i, strerror(errno));
      return -1;
    }
    if ((rundirs[i] = openat(rundir, dirname, O_DIRECTORY)) < 0) {
      fprintf(stderr, "worker %d: could not open runtime directory: %s\n", i, strerror(errno));
      return -1;
    }

    if ((mkfifoat(rundirs[i], "inputfile", S_IRUSR | S_IWUSR)) != 0) {
      fprintf(stderr, "%s: could not create FIFO: %s\n", "inputfile", strerror(errno));
    }
  }

  // use the default event loop unless you have special needs
  loop = ev_default_loop (EVBACKEND_SELECT); //EVFLAG_AUTO);

  //ev_set_timeout_collect_interval (loop, 0.0001);
  //ev_set_io_collect_interval (loop, 0.0001);

  // Start children
  for (i=0; i<NUM_PROCS; ++i) {
    pid_t pid = fork();
    if (pid == 0) {
      // Child
      fchdir(rundirs[i]);
      if (execle(argv[1], gnu_basename(argv[1]), "outputfile", "inputfile", NULL, NULL)) {
	fputs("Could not exec: ", stderr);
	fputs(strerror(errno), stderr);
	fputc('\n', stderr);
	exit(EXIT_FAILURE);
      }
    }
    else if (pid > 0) {
      // Parent
      print_tod(stderr);
    }
  }

  // Initialize watchers
  for (i=0; i<NUM_PROCS; ++i) {
    ev_io *wio = &input_watcher[i];

    fds[i] = openat(rundirs[i], "inputfile", O_WRONLY /*Consider simply O_WRONLY*/);
    wio->data = (void*)i;
    ev_io_init(wio, input_cb, fds[i], EV_WRITE);
    ev_io_start(loop, wio);
  }

  // now wait for events to arrive
  gettimeofday(tv+0, NULL);
  ev_loop (loop, 0);
  gettimeofday(tv+1, NULL);

  // Hang up
  puts("Closing pipes.");
  for (i=0; i<NUM_PROCS; ++i) {
    close(fds[i]);
  } 
  puts("Done.");

  // unloop was called, so exit
  long t = (tv[1].tv_sec - tv[0].tv_sec) * 1000000
           + (tv[1].tv_usec - tv[0].tv_usec);

  printf("Time taken: %lfs (%lfms average)\n",
      ((double)t) / 1000000.0,
      ((double)t) * NUM_PROCS / NUM_JOBS);

  puts("Waiting for children processes to terminate...");
  pid_t pid;
  do {
    pid = wait(NULL);
    if(pid == -1 && errno != ECHILD) {
      perror("Error during wait()");
      abort();
    }
  } while (pid > 0);
  
  puts("Done.");

  return 0;
}
