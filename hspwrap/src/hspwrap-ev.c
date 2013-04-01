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


void
print_tod ()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  printf("[%ld.%06ld] ", tv.tv_sec, tv.tv_usec);
}

  // a single header file is required
  #include <ev/ev.h>

  // every watcher type has its own typedef'd struct
  // with the name ev_<type>
  ev_io stdin_watcher;
  ev_timer timeout_watcher;

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

  // another callback, this time for a time-out
  static void
  timeout_cb (EV_P_ struct ev_timer *w, int revents)
  {
    puts ("timeout");
    // this causes the innermost ev_loop to stop iterating
    ev_unloop (EV_A_ EVUNLOOP_ONE);
  }

  int
  main (void)
  {
    // use the default event loop unless you have special needs
    struct ev_loop *loop = ev_default_loop (0);
    
    ev_io    *w_in = &stdin_watcher;
    ev_timer *w_t  = &timeout_watcher;

    // initialise an io watcher, then start it
    // this one will watch for stdin to become readable
    ev_io_init (w_in, stdin_cb, STDIN_FILENO, EV_READ);
    ev_io_start (loop, &stdin_watcher);

    // initialise a timer watcher, then start it
    // simple non-repeating 5.5 second timeout
    ev_timer_init (w_t, timeout_cb, 5.5, 0.);
    ev_timer_start (loop, &timeout_watcher);

    // now wait for events to arrive
    ev_loop (loop, 0);

    // unloop was called, so exit
    return 0;
  }
/*

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


*/
