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

#include <ev/ev.h>

#include "errno.h"
#include "error.h"

#define NUM_PROCS 2
#define NUM_JOBS  10000000

#define HSP_ERROR 1
enum HspError {
  HSP_ERROR_FAILED
};

void print_tod ();

// every watcher type has its own typedef'd struct
// with the name ev_<type>
ev_timer timeout_watcher;
ev_io    stdin_watcher;
ev_io    input_watcher[NUM_PROCS];
int      fds[NUM_PROCS];
int      remaining = NUM_JOBS;

void
print_tod ()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  printf("[%ld.%06ld] ", tv.tv_sec, tv.tv_usec);
}


// all watcher callbacks have a similar signature
// this callback is called when data is readable on stdin
static void
stdin_cb (EV_P_ struct ev_io *w, int revents)
{
  puts ("stdin ready");
  // for one-shot events, one must manually stop the watcher
  // with its corresponding stop function.
  ev_io_stop (EV_A_ w);

  // this causes all nested ev_loop's to stop iterating
  ev_unloop (EV_A_ EVUNLOOP_ALL);
}

static void
input_cb (EV_P_ struct ev_io *w, int revents)
{
  write(w->fd, "That's all folks!\n", 18);
  remaining--;
  if (remaining <= 0) {
    // Done
    ev_io_stop (EV_A_ w);
    ev_unloop (EV_A_ EVUNLOOP_ALL);
  }
}

// another callback, this time for a time-out
static void
timeout_cb (EV_P_ struct ev_timer *w, int revents)
{
  puts ("timeout");
  // this causes the innermost ev_loop to stop iterating
  ev_unloop (EV_A_ EVUNLOOP_ONE);
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

int
main (int argc, char **argv)
{
  char dirname[10];
  int rundir, rundirs[NUM_PROCS], i;
  struct timeval tv[2];
 
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
      
    fds[i] = openat(rundirs[i], "inputfile", O_RDWR /*Consider simply O_RDONLY*/);
    write(fds[i], "Hello World!\n", 13);
  }

  // use the default event loop unless you have special needs
  struct ev_loop *loop = ev_default_loop (EVBACKEND_EPOLL);
  //ev_set_timeout_collect_interval (loop, 0.0001);
  //ev_set_io_collect_interval (loop, 0.0001);

  ev_io    *w_input;
  ev_io    *w_stdin = &stdin_watcher;
  ev_timer *w_t     = &timeout_watcher;

  // initialise an io watcher, then start it
  // this one will watch for stdin to become readable
  //ev_io_init (w_stdin, stdin_cb, STDIN_FILENO, EV_READ);
  //ev_io_start (loop, w_stdin);

  for (i=0; i<NUM_PROCS; ++i) {
    w_input = input_watcher + i;
    ev_io_init (w_input, input_cb, fds[i], EV_WRITE);
    ev_io_start (loop, w_input);
  }

  // Start children
  for (i=0; i<NUM_PROCS; ++i) {
    pid_t pid = fork();
    if (pid == 0) {
      // Child
      fchdir(rundirs[i]);
      if (execle(argv[1], "test", "outputfile", "inputfile", NULL, NULL)) {
	fputs("Could not exec: ",stderr);
	fputs(strerror(errno),stderr);
	fputc('\n',stderr);
	exit(EXIT_FAILURE);
      }
    }
    else if (pid > 0) {
      // Parent
      print_tod(stderr);
    }
  }

  // initialise a timer watcher, then start it
  // simple non-repeating 5.5 second timeout
  //ev_timer_init (w_t, timeout_cb, 5.5, 0.);
  //ev_timer_start (loop, &timeout_watcher);

  // now wait for events to arrive
  gettimeofday(tv+0, NULL);
  ev_loop (loop, 0);
  gettimeofday(tv+1, NULL);

  // unloop was called, so exit
  long t = (tv[1].tv_sec - tv[0].tv_sec) * 1000000
           + (tv[1].tv_usec - tv[0].tv_usec);

  printf("Time taken: %lfs (%lfms average)\n",
      ((double)t) / 1000000.0,
      ((double)t) * NUM_PROCS / NUM_JOBS);

  return 0;
}
