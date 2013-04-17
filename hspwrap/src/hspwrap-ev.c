#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>

// SHM
#include <fcntl.h>
#include <sys/stat.h>

#include <aio.h>
#include <ev/ev.h>

#include "errno.h"
#include "error.h"

#define NUM_PROCS 4
#define NUM_JOBS  20
#define NUM_SHMS  1

#define RUNTIME_DIR_USE_XDG     0
#define RUNTIME_DIR_TMPFS       "/dev/shm"
#define RUNTIME_DIR_BASENAME    "hspwrap"
#define SHARE_DIR_NAME          "share"

void print_tod ();

ev_io    input_watcher[NUM_PROCS];
ev_child child_watcher;
struct   aiocb aios[NUM_PROCS];
int      fifos[NUM_PROCS];
int      shms[NUM_SHMS];
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
  struct stat st;
  int i = (int)w->data;
  int err, avail;
  
  if (remaining <= 0) {
    // Done
    ev_io_stop(EV_A_ w);
    // FIXME: HACK!! belongs later
    close(fifos[i]);
    //ev_unloop(EV_A_ EVUNLOOP_ALL);
  } else {
#if 1
    fstat(w->fd, &st);
    err = ioctl(w->fd, FIONREAD, &avail);
    if (avail == 0) {
      printf("Worker %d: pipe size: %jd %d  (%d) ", i, st.st_size, avail, err);
      puts("Writing...");
      write(w->fd, "That's all folks!\n", 18);
      remaining--;
    } else {
      //puts("Waiting...");
    }
#else
    aios[i].aio_fildes = w->fd;
    aios[i].aio_buf    = "That's all folks!\n";
    aios[i].aio_nbytes = 18;
    aio_write(aios + i);
#endif
  }
}


static void
child_cb (EV_P_ ev_child *w, int revents)
{
  ev_child_stop (EV_A_ w);
  printf ("Process %d changed status to %x\n", w->rpid, w->rstatus);
}


int
get_rundir()
{
  char *rootdir, *dirname;
  int   f_root, f_run;

  // TODO: Check for mkdirat, openat, mkfifoat, asprintf, and friends
#if RUNTIME_DIR_USE_XDG == 1
  if (!(rootdir = getenv("XDG_RUNTIME_DIR"))) {
    return -1;
  }
#else
  rootdir = RUNTIME_DIR_TMPFS;
#endif

  // Form directory name
  asprintf(&dirname, RUNTIME_DIR_BASENAME ".%d", getpid());
	  
  printf("Runtime dir: %s/%s\n", rootdir, dirname);

  // Now open everything
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

  char  dirname[10];
  char *shmdata;
  int   rundir, rundirs[NUM_PROCS], sharedir;
  int   pagesize;
  int   i, j;

  // use the default event loop unless you have special needs
  loop = ev_default_loop (EVBACKEND_EPOLL); //EVFLAG_AUTO);
 
  rundir   = get_rundir();
  pagesize = getpagesize();
  
  // Prepare shared data directory
  if ((mkdirat(rundir, SHARE_DIR_NAME, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) != 0) {
    fprintf(stderr, "could not make share directory: %s\n", strerror(errno));
    return -1;
  }
  if ((sharedir = openat(rundir, SHARE_DIR_NAME, O_DIRECTORY)) < 0) {
    fprintf(stderr, "could not open share directory: %s\n", strerror(errno));
    return -1;
  }

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

  // Memory map some data;
  for (j=0; j<NUM_SHMS; ++j) {
    // Maybe just use ids for shared SHM names
    shms[j] = openat(sharedir, "dbfile", O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    ftruncate(shms[j], pagesize);
    shmdata = mmap((caddr_t)0, pagesize, PROT_WRITE, MAP_SHARED, shms[j], 0);
    strcpy(shmdata, "Very important DB data.");
    
    // Now "share" it
    for (i=0; i<NUM_PROCS; ++i) {
      linkat(sharedir, "dbfile", rundirs[i], "dbfile", 0);
    }
  }

  //ev_set_timeout_collect_interval (loop, 0.0001);
  //ev_set_io_collect_interval (loop, 0.0001);

  // Start children
  for (i=0; i<NUM_PROCS; ++i) {
    pid_t pid = fork();
    if (pid == 0) {
      // Child
      fchdir(rundirs[i]);
      if (execle(argv[1], gnu_basename(argv[1]), "outputfile", "dbfile", "inputfile", NULL, NULL)) {
	fputs("Could not exec: ", stderr);
	fputs(strerror(errno), stderr);
	fputc('\n', stderr);
	exit(EXIT_FAILURE);
      }
    }
    else if (pid > 0) {
      // Parent
    }
  }

  // Initialize watchers
  for (i=0; i<NUM_PROCS; ++i) {
    ev_io *wio = &input_watcher[i];

    fifos[i] = openat(rundirs[i], "inputfile", O_WRONLY);
    wio->data = (void*)i;
    ev_io_init(wio, input_cb, fifos[i], EV_WRITE);
    ev_io_start(loop, wio);
  }
  
  ev_child *wchld = &child_watcher;
  ev_child_init(wchld, child_cb, 0, 1);
  ev_child_start(loop, wchld);

  // now wait for events to arrive
  gettimeofday(tv+0, NULL);
  ev_loop (loop, 0);
  gettimeofday(tv+1, NULL);
  
  // unloop was called, so exit
  long t = (tv[1].tv_sec - tv[0].tv_sec) * 1000000
           + (tv[1].tv_usec - tv[0].tv_usec);

  printf("\nTime taken: %lfs (%lfms average)\n\n",
      ((double)t) / 1000000.0,
      ((double)t) * NUM_PROCS / NUM_JOBS);

  // Hang up
  /*
  puts("Closing pipes...");
  for (i=0; i<NUM_PROCS; ++i) {
    close(fifos[i]);
  }
  */

  puts("Waiting for children processes to terminate...");
  pid_t pid;
  do {
    pid = wait(NULL);
    if(pid == -1 && errno != ECHILD) {
      perror("Error during wait()");
      abort();
    }
  } while (pid > 0);

  // Cleanup shms
  puts("Cleaning SHMs...");
  for (j=0; j<NUM_SHMS; ++j) {
    ftruncate(shms[j], 0);
    close(shms[i]);
  }

  // Finally...
  puts("Done.");
  return 0;
}
