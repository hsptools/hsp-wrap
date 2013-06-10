#include <assert.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <mpi.h>
#include <hsp/process_control.h>

#include "process_pool.h"
#include "writer.h"
#include "hspwrap.h"

#include "slave.h"

#define BUFFER_SIZE (1L<<20)

struct cache_buffer
{
  struct cache_buffer *next;
  char *r_ptr;  // Current read pointer
  size_t size;  // Size of the buffer
  int count;    // Number of blocks in the buffer
  int len;      // Length of valid data
  char data[];  // The whole data buffer
};

static int mkpath (const char *path, mode_t mode);
static void fork_process_pool (const char *cmd);
static int  request_work (struct cache_buffer **queue);
static void push_work (wid_t wid, const char *data, size_t len);
static void pull_results (struct writer_ctx *w, wid_t wid);

// TODO: add slave_ctx to wrap this stuff
struct writer_ctx writer;
struct process_control *ps_ctl;

void
slave_init (int slave_idx, int nslaves, int nprocesses)
{
  char *outdir, *workdir;
  int rc, i;

  // Create output directories
  outdir = getenv("HSP_OUTDIR");
  if (!outdir) {
    outdir = "hspwrap-out";
  }

  // 2 slashes, 2 char dir, 10 char rank, 1 NUL
  i = strlen(outdir) + 15;
  workdir = malloc(i);
  if (!workdir) {
    fprintf(stderr, "Out of memory.\n");
    exit(EXIT_FAILURE);
  }

  snprintf(workdir, i, "%s/%02d/%02d", outdir, slave_idx/100, slave_idx%100);
  if ((rc = mkpath(workdir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH))) {
    fprintf(stderr, "Could not create work directory: %s\n", strerror(rc));
    exit(EXIT_FAILURE);
  }
    
  // The output directory should exist, now change dir
  if (chdir(workdir)) {
    fprintf(stderr, "Could not change to work directory: %s\n", strerror(errno));
  }

  // Cleanup
  free(workdir);

  // Prepare process control structure and streaming SHMs
  ps_ctl = ps_ctl_init(nprocesses, NULL);

  for (i = 0; i < nprocesses; ++i) {
    ps_ctl_add_file(ps_ctl, i, "inputfile", BUFFER_SIZE);
    ps_ctl_add_file(ps_ctl, i, "outputfile", BUFFER_SIZE);
  }
}


void
slave_broadcast_shared_file(const char *path)
{
  void  *data, *shm;
  size_t sz;

  // Get file size
  MPI_Bcast(&sz, sizeof(sz), MPI_BYTE, 0, MPI_COMM_WORLD);

  // Get shared-file data, and add to file table
  shm = ps_ctl_add_file(ps_ctl, -1, path, sz);

  // write data (MPI+mmap work-around)
  data = malloc(sz);
  fprintf(stderr, "slave: Receiving data for file: %s...\n", path);
  MPI_Bcast(data, sz, MPI_BYTE, 0, MPI_COMM_WORLD);
  memcpy(shm, data, sz);
  free(data);
}


void
slave_broadcast_work_file(const char *path)
{
  void  *data, *file;
  size_t sz;
  int    fd;

  // Get file size
  fprintf(stderr, "slave: Receiving work file size...\n");
  MPI_Bcast(&sz, sizeof(sz), MPI_BYTE, 0, MPI_COMM_WORLD);
  fprintf(stderr, "slave: size: %zu bytes\n", sz);

  // Create file and size it
  if ((fd = open(path, O_CREAT | O_EXCL | O_RDWR,
                 S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) == -1) {
    fprintf(stderr, "%s: Could not create file for writing: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }
  if ((ftruncate(fd, sz)) != 0) {
    fprintf(stderr, "%s: Failed to resize output file: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, "slave: Created file %s with size: %zu bytes\n", path, sz);

  // mmap
  file = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED /*MAP_HUGETLB*/, fd, 0);
  if (file == MAP_FAILED) {
    close(fd);
    fprintf(stderr, "%s: Could not mmap file: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, "slave: Memory mapped file %s\n", path);

  // write data (MPI+mmap work-around)
  data = malloc(sz);
  fprintf(stderr, "slave: Receiving data for file: %s...\n", path);
  MPI_Bcast(data, sz, MPI_BYTE, 0, MPI_COMM_WORLD);
  memcpy(file, data, sz);
  free(data);

  // unmap and such
  munmap(file, sz);
  close(fd);
}


int
slave_main (const char *cmd)
{
  struct cache_buffer *queue;

  struct timeval tv[2];
  int no_work, i;
  wid_t wid;

  no_work = 0;

  // TODO: Maybe move some stuff to a slave_init() function
  
  // Spawn and initialize writer thread
  writer_start(&writer,BUFFER_SIZE * ps_ctl->nprocesses);

  // Initial (empty) data
  for (wid = 0; wid < ps_ctl->nprocesses; ++wid) {
    // Fetch work into buffers for worker wid (set FTE size and shm data)
    push_work(wid, "", 0);

    // Prepare process block
    ps_ctl->process_state[wid] = RUNNING;
    ps_ctl->process_cmd[wid]   = RUN;
  }

  // Empty queue
  queue = NULL;

  // Consider reworking some of the logic here, especially if we add support
  // for spawning processes one at a time. We could start processing, or at
  // least cold-start the processes before receiving all the data.

  // Process control is setup, data is in place, start the processes
  fork_process_pool(cmd);

  fprintf(stderr, "Waiting for service requests.\n");

  // Count number of tasks assigned to each worker
  unsigned worker_iterations[MAX_PROCESSES];
  ZERO_ARRAY(worker_iterations);

  gettimeofday(tv+0, NULL);
  while (1) {
    pthread_mutex_lock(&ps_ctl->lock);
    if (ps_ctl_all_done(ps_ctl)) {
      // All processes are done! exit loop
      pthread_mutex_unlock(&ps_ctl->lock);
      break;
    }
    // Otherwise, wait for a process to need service
    while (ps_ctl_all_running(ps_ctl)) {
      pthread_cond_wait(&ps_ctl->need_service, &ps_ctl->lock);
    }

    // Now, service all processes
    for (wid = 0; wid < ps_ctl->nprocesses; ++wid) {
      switch (ps_ctl->process_state[wid]) {
      case EOD:
        // front buffer empty implies whole queue is empty
        if (!no_work && queue == NULL) {
          // No data in the buffer, must request now
          no_work = !request_work(&queue);
        }

        // Preemptively flush the results while the processes is stopped
        // FIXME: Don't do this if this is the initial EOD (first request for data)
        pull_results(&writer, wid);

        if (queue != NULL) {
          // Handle data request locally
          // Iterator code...
          char *end  = iter_next(queue->data, queue->data+queue->size, queue->r_ptr);
          off_t len  = end - queue->r_ptr;
          push_work(wid, queue->r_ptr, len);
          worker_iterations[wid]++;
          ps_ctl->process_cmd[wid] = RUN;
          ps_ctl->process_state[wid] = RUNNING;
          pthread_cond_signal(&ps_ctl->process_ready[wid]);
	  fprintf(stderr, "slave: sent new data to worker %d:\n", wid);
	  fwrite(queue->r_ptr, 1, len, stderr);
	  fputc('\n', stderr);

          // Now advance the iterator
          queue->len -= len;
          queue->count--;
          queue->r_ptr += len;
          // Advance to next buffer if needed
          if (queue->count == 0) {
            assert(queue->len == 0);
            struct cache_buffer *n = queue->next;
            free(queue);
            queue = n;
          }
        } else if (no_work) {
          // No more data, tell it to quit
          fprintf(stderr, "Requesting worker %d to quit\n", wid);
          ps_ctl->process_cmd[wid] = QUIT;
          ps_ctl->process_state[wid] = RUNNING;
          pthread_cond_signal(&ps_ctl->process_ready[wid]);
        } 
        break;

      case NOSPACE:
        // We always handle NOSPACE, even if there is no work left
        // FIXME: This will result in fragmentation of the output data
        // we need to maintain an index so output can be pieced back in
        // proper order by the 'gather' script
        pull_results(&writer, wid);
        fprintf(stderr, "Buffer almost overflowed, result data is probably interleaved\n");
        ps_ctl->process_cmd[wid] = RUN;
        ps_ctl->process_state[wid] = RUNNING;
        pthread_cond_signal(&ps_ctl->process_ready[wid]);
        break;

      case FAILED:
        fprintf(stderr, "PROCESS FAILED\n");
        break;
      case DONE:
        //fprintf(stderr, "PROCESS DONE\n");
        break;
      case IDLE:
      case RUNNING:
        // Not of interest, move on.
        break;
      }
    }
    // Done modifying process states
    pthread_mutex_unlock(&ps_ctl->lock);

    // Prefetch data if needed
    if (!no_work) {
      no_work = !request_work(&queue);
    }
  }

  gettimeofday(tv+1, NULL);

  // Flush last bit of data
  pthread_mutex_lock(&ps_ctl->lock);
  for (wid = 0; wid < ps_ctl->nprocesses; ++wid) {
    pull_results(&writer, wid);
  }
  pthread_mutex_unlock(&ps_ctl->lock);

  // Join with writer thread
  writer.running = 0;
  pthread_cond_signal(&writer.data_pending);
  pthread_join(writer.thread, NULL);
  fprintf(stderr, "Writer thread successfully exited\n");

  // Waiting on all ranks to acknowledge the EXIT
  MPI_Barrier(MPI_COMM_WORLD);

  long t = (tv[1].tv_sec - tv[0].tv_sec) * 1000000
           + (tv[1].tv_usec - tv[0].tv_usec);

  putchar('\n');
  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    printf("Worker %2u iterations: %5u\n", i, worker_iterations[i]);
  }

  printf("Time taken: %lfs\n",  ((double)t) / 1000000.0);
  /*
  printf("Time taken: %lfs (%lfms average)\n",
      ((double)t) / 1000000.0,
      ((double)t) * ps_ctl->nprocesses / NUM_JOBS);
  */

  return 0;
}


static int
mkpath (const char *path, mode_t mode)
{
  char *c, *p;
  int ret;

  // To be consistent with dirname, these all imply '.' or '/',
  // which must already exist
  if (!path || !path[0] || !strcmp(path, "/")) {
    return EEXIST;
  }

  if (!(p = strdup(path))) {
    return ENOMEM;
  }

  // Grow the string one section at a time and mkdir
  for (c = p+1; ; ++c) {
    if (*c == '/') {
      *c = '\0';
      if (mkdir(p, mode) && errno != EEXIST) {
        ret = errno;
        break;
      }
      *c = '/';
    } else if (*c == '\0') {
      ret = mkdir(p, mode) ? errno : 0;
      break;
    }
  }

  free(p);
  return ret;
}


static void
fork_process_pool (const char *cmd)
{
  pid_t tmp_pid, hspwrap_pid, pool_pid;

  hspwrap_pid = getpid();

  if ((tmp_pid = fork()) > 0) {
    // parent process (done)
    return;
  } else if (!tmp_pid) {
    // temporary process
    if ((pool_pid = fork()) > 0) {
      // still in temporary process, kill ourself
      kill(getpid(), SIGKILL);
    } else if (!pool_pid) {
      // forker process
      process_pool_start(hspwrap_pid, ps_ctl->nprocesses, cmd);
    } else {
      fprintf(stderr, "Could not fork process pool.  Terminating.\n");
      exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
  } else {
    fprintf(stderr, "Could not fork temporary process.  Terminating.\n");
    exit(EXIT_FAILURE);
  }
}


static int
request_work (struct cache_buffer **queue)
{
  struct cache_buffer *b, *tail;
  struct request  req;
  struct workunit wu;
  int cnt;

  // See how much data we have and find tail
  for (cnt = 0, tail = NULL, b = *queue; b; b = b->next) {
    cnt += b->count;
    tail = b;
  }

  // request work units from master
  req.type  = REQ_WORKUNIT;
  req.count = ps_ctl->nprocesses - cnt;

  MPI_Send(&req, sizeof(struct request), MPI_BYTE, 0,
           TAG_REQUEST, MPI_COMM_WORLD);

  // Read work unit information
  MPI_Recv(&wu, sizeof(struct workunit), MPI_BYTE, 0,
           TAG_WORKUNIT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

  // Figure out what to do with the response
  switch (wu.type) {
  case WU_TYPE_EXIT:
    return 0;

  case WU_TYPE_DATA:
    // Prepare buffer, and read data
    b = malloc(sizeof(struct cache_buffer) + wu.len);
    b->r_ptr = b->data;
    b->size  = wu.len;
    b->count = wu.count;
    b->len   = wu.len;
    b->next  = NULL;
    MPI_Recv(b->data, wu.len, MPI_BYTE, 0,
             TAG_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // Add to end of queue
    if (tail) {
      tail->next = b;
    } else {
      *queue = b;
    }
    return b->count;

  default:
    fprintf(stderr, "slave: unknown work unit type from master. Exiting.\n");
    exit(EXIT_FAILURE);
  }
}


static void
push_work (wid_t wid, const char *data, size_t len)
{
  int i;
  struct file_table *ft;
  struct file_table_entry *f;

  ft = &ps_ctl->ft;
  for (i = 0; i < ft->nfiles; ++i) {
    f = &ft->file[i];
    if (f->wid == wid && !strcmp(f->name, "inputfile")) {
      // Prepare data buffer(s)
      memcpy(f->shm, data, len);
      f->size = len;
    }
  }
}


/**
 * "Pull" results from a worker's output buffers and send to the writer.
 * This function must be called with a lock on the shared process-control
 * structure since it modifies a worker's buffers (although the worker should
 * be halted). */
static void
pull_results (struct writer_ctx *ctx, wid_t wid)
{
  int i;
  struct file_table *ft;
  struct file_table_entry *f;

  ft = &ps_ctl->ft;
  for (i = 0; i < ft->nfiles; ++i) {
    f = &ft->file[i];
    if (f->wid == wid && !strcmp(f->name, "outputfile")) {
      // Write it out, it is an output file that belongs to us
      writer_write(ctx, f->shm, f->size);

      // Mark buffer as empty
      f->size = 0;
    }
  }
}
