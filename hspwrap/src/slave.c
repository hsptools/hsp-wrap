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

struct cache_buffer
{
  struct cache_buffer *next;
  char *r_ptr;  // Current read pointer
  size_t size;  // Size of the buffer
  int count;    // Number of blocks in the buffer
  int len;      // Length of valid data
  char data[];  // The whole data buffer
};

static int  mkpath (const char *path, mode_t mode);
static int  request_work (struct cache_buffer **queue);
static void push_work (wid_t wid, const char *data, size_t len);
static void pull_results (struct writer_ctx *w, wid_t wid);
static void pull_worker_results (wid_t wid);
static int  create_stream_files (const char *files, size_t size,
                                 enum file_type type, char ***names);

// TODO: add slave_ctx to wrap this stuff
extern struct process_pool_ctl *pool_ctl;
struct writer_ctx *writers;
struct process_control *ps_ctl;
int    sid;
char  *outdir, *workdir;
char **infiles, **outfiles;
int    ninfiles, noutfiles;

void
slave_init (int slave_idx, int nslaves, int nprocesses)
{
  int rc, i;
  char *files;

  sid = slave_idx;

  // Create output directories
  outdir = getenv("HSP_OUTDIR");
  if (!outdir) {
    outdir = "hspwrap-out";
  }

  // 2 slashes, 2 char dir, 10 char rank, 1 NUL
  i = strlen(outdir) + 15;
  workdir = malloc(i);
  if (!workdir) {
    fprintf(stderr, "slave %d: Out of memory.\n", sid);
    exit(EXIT_FAILURE);
  }

  snprintf(workdir, i, "%s/%02d/%02d", outdir, slave_idx/100, slave_idx%100);
  if ((rc = mkpath(workdir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH))) {
    fprintf(stderr, "slave %d: Could not create work directory: %s\n", sid, strerror(rc));
    exit(EXIT_FAILURE);
  }

  // The output directory should exist, now change dir
  if (chdir(workdir)) {
    fprintf(stderr, "slave %d: Could not change to work directory: %s\n", sid, strerror(errno));
  }

  // Prepare process control structure and streaming SHMs
  ps_ctl = ps_ctl_init(nprocesses, NULL);

  // Create input file mappings
  files = getenv("HSP_INFILES");
  if (!files) files = "inputfile";
  // TODO: Support multiple input files
  if (strchr(files, ':')) {
    fprintf(stderr, "slave %d: Multiple input files are not yet supported.\n", sid);
    exit(EXIT_FAILURE);
  }
  ninfiles = create_stream_files(files, BUFFER_SIZE, FTE_INPUT, &infiles);

  // Create output files mappings
  files = getenv("HSP_OUTFILES");
  if (!files) files = "outputfile";
  noutfiles = create_stream_files(files, BUFFER_SIZE, FTE_OUTPUT, &outfiles);
}


// Cleanup
//free(workdir);


ssize_t
slave_broadcast_shared_file(const char *path)
{
  void  *shm;
  size_t sz;
  int rc;

  // Get file size
  MPI_Bcast(&sz, sizeof(sz), MPI_BYTE, 0, MPI_COMM_WORLD);

  // Get shared-file data, and add to file table
  shm = ps_ctl_add_file(ps_ctl, -1, path, sz, FTE_SHARED);

  // write data (MPI+mmap work-around)
  rc = chunked_bcast(shm, sz, 0, MPI_COMM_WORLD);
 
  if (rc == MPI_SUCCESS) {
    return sz;
  } else {
    return -1;
  }
}


ssize_t
slave_broadcast_work_file(const char *path)
{
  void   *file;
  size_t  sz;
  int     fd;
  int     rc;

  // Get file size
  trace("slave: Receiving work file size...\n");
  MPI_Bcast(&sz, sizeof(sz), MPI_BYTE, 0, MPI_COMM_WORLD);
  trace("slave: size: %zu bytes\n", sz);

  // Create file and size it
  if ((fd = open(path, O_CREAT | O_EXCL | O_RDWR,
                 S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) == -1) {
    fprintf(stderr, "slave: %s: Could not create file for writing: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }
  if ((ftruncate(fd, sz)) != 0) {
    fprintf(stderr, "slave: %s: Failed to resize output file: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }
  trace("slave: Created file %s with size: %zu bytes\n", path, sz);

  // mmap
  file = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED /*MAP_HUGETLB*/, fd, 0);
  if (file == MAP_FAILED) {
    close(fd);
    fprintf(stderr, "slave: %s: Could not mmap file: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }

  // write data (MPI+mmap work-around) FIXME: Handle return
  rc = chunked_bcast(file, sz, 0, MPI_COMM_WORLD);

  // unmap and such
  munmap(file, sz);
  close(fd);

  if (rc == MPI_SUCCESS) {
    return sz;
  } else {
    return -1;
  }
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

  // Spawn and initialize writer threads
  writers = malloc(noutfiles * sizeof(struct writer_ctx));
  for (i = 0; i < noutfiles; ++i) {
    writer_start(&writers[i], outfiles[i], BUFFER_SIZE * ps_ctl->nprocesses);
  }

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
  MPI_Barrier(MPI_COMM_WORLD);
  process_pool_spawn(pool_ctl, workdir, ps_ctl->nprocesses);
  MPI_Barrier(MPI_COMM_WORLD);

  trace("slave %d: Waiting for service requests.\n", sid);

  // Count number of tasks assigned to each worker
  unsigned worker_iterations[MAX_PROCESSES];
  ZERO_ARRAY(worker_iterations);

  /*
  fprintf(stderr, "slave %d: Waiting for all workers to initialize...\n", sid);
  pthread_mutex_lock(&ps_ctl->lock);
  while (!ps_ctl_all_waiting(ps_ctl)) {
    pthread_cond_wait(&ps_ctl->need_service, &ps_ctl->lock);
  }
  pthread_mutex_unlock(&ps_ctl->lock);
  fprintf(stderr, "slave %d: All workers are idle. Continuing\n", sid);
  */

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
    //fprintf(stderr, "slave %d: SERVICING WORKERS\n", sid);
    for (wid = 0; wid < ps_ctl->nprocesses; ++wid) {
      //fprintf(stderr, "slave %d: worker %d: state: %d command: %d\n", sid, wid,
      //        ps_ctl->process_state[wid], ps_ctl->process_cmd[wid]);

      switch (ps_ctl->process_state[wid]) {
      case EOD:
        // front buffer empty implies whole queue is empty
        if (!no_work && queue == NULL) {
          // No data in the buffer, must request now
          no_work = (request_work(&queue) == -1);
        }

        // Preemptively flush the results while the processes is stopped
        // FIXME: Don't do this if this is the initial EOD (first request for data)
        pull_worker_results(wid);

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
          trace("slave %d: sent new data to worker %d\n", sid, wid);

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
          trace("slave %d: Requesting worker %d to quit\n", sid, wid);
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
        pull_worker_results(wid);
        info("slave %d: Buffer almost overflowed, result data is probably interleaved\n", sid);
        ps_ctl->process_cmd[wid] = RUN;
        ps_ctl->process_state[wid] = RUNNING;
        pthread_cond_signal(&ps_ctl->process_ready[wid]);
        break;

      case FAILED:
        fprintf(stderr, "!!!!!! PROCESS FAILED !!!!!!\n");
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
      no_work = (request_work(&queue) == -1);
    }
  }

  MPI_Barrier(MPI_COMM_WORLD);
  gettimeofday(tv+1, NULL);

  MPI_Finalize();

  // Flush last bit of data
  pthread_mutex_lock(&ps_ctl->lock);
  for (wid = 0; wid < ps_ctl->nprocesses; ++wid) {
    pull_worker_results(wid);
  }
  pthread_mutex_unlock(&ps_ctl->lock);

  // Join with writer threads
  for (i = 0; i < noutfiles; ++i) {
    writers[i].running = 0;
    pthread_cond_signal(&writers[i].data_pending);
  }
  for (i = 0; i < noutfiles; ++i) {
    pthread_join(writers[i].thread, NULL);
    trace("slave %d: Writer thread %d successfully exited\n", sid, i);
  }

  long t = (tv[1].tv_sec - tv[0].tv_sec) * 1000000
           + (tv[1].tv_usec - tv[0].tv_usec);

  putchar('\n');
  for (wid = 0; wid < ps_ctl->nprocesses; ++wid) {
    info("slave %d: worker %2u: iterations: %5u\n", sid, wid, worker_iterations[wid]);
  }

  info("slave %d: Time taken: %lfs\n", sid, ((double)t) / 1000000.0);
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


static int
request_work (struct cache_buffer **queue)
{
  struct cache_buffer *b, *tail;
  struct request  req;
  struct workunit wu;
  int cnt, rc;

  // See how much data we have and find tail
  for (cnt = 0, tail = NULL, b = *queue; b; b = b->next) {
    cnt += b->count;
    tail = b;
  }

  // Don't actually *need* data right now.
  // FIXME: Consider better prefetching policy. We are always prefetching if we can,
  //        but we probably want to let requests add up, and request a bundle.
  if (cnt > ps_ctl->nprocesses/2) {
    return 0;
  }

  req.type  = REQ_WORKUNIT;
  req.count = ps_ctl->nprocesses - cnt;

  // request work units from master
  trace("slave %d: requesting work...\n", sid);
  rc = MPI_Send(&req, sizeof(struct request), MPI_BYTE, 0,
           TAG_REQUEST, MPI_COMM_WORLD);
  trace("slave %d: sent request %d\n", sid, rc);

  // Read work unit information
  trace("slave %d: receiving work unit...\n", sid);
  rc = MPI_Recv(&wu, sizeof(struct workunit), MPI_BYTE, 0,
           TAG_WORKUNIT, MPI_COMM_WORLD, MPI_STATUSES_IGNORE);

  //fprintf(stderr,"slave %d: waiting for response...\n", sid);
  //MPI_Waitall(2, mpi_req, MPI_STATUSES_IGNORE);
  trace("slave %d: received work unit (type: %d, size: %d) %d\n", sid, wu.type, wu.len, rc);

  // Figure out what to do with the response
  switch (wu.type) {
  case WU_TYPE_EXIT:
    return -1;

  case WU_TYPE_DATA:
    // Prepare buffer, and read data
    b = malloc(sizeof(struct cache_buffer) + wu.len);
    b->r_ptr = b->data;
    b->size  = wu.len;
    b->count = wu.count;
    b->len   = wu.len;
    b->next  = NULL;
    trace("slave %d: receiving work unit data...\n", sid);
    rc = MPI_Recv(b->data, wu.len, MPI_BYTE, 0,
             TAG_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    trace("slave %d: received work unit data %d\n", sid, rc);
    // malloc'd structure is freed once queue page is exhausted

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
    if (f->wid == wid && f->type == FTE_INPUT) {
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
 * be halted at this point). */
static void
pull_results (struct writer_ctx *ctx, wid_t wid)
{
  int i;
  struct file_table *ft;
  struct file_table_entry *f;

  ft = &ps_ctl->ft;
  for (i = 0; i < ft->nfiles; ++i) {
    f = &ft->file[i];
    if (f->wid == wid && !strcmp(f->name, ctx->name)) {
      // Write it out, it is an output file that belongs to us
      writer_write(ctx, f->shm, f->size);

      // Mark buffer as empty
      f->size = 0;
    }
  }
}

static void
pull_worker_results (wid_t wid)
{
  int i, j;
  struct file_table *ft;
  struct file_table_entry *f;
  struct writer_ctx *ctx;

  ft = &ps_ctl->ft;
  for (i = 0; i < ft->nfiles; ++i) {
    f = &ft->file[i];
    if (f->wid == wid && f->type == FTE_OUTPUT) {
      // Found a outfile matching this worker, find the writer
      for (j = 0; j < noutfiles; ++j) {
        ctx = &writers[j];
        if (!strcmp(f->name, ctx->name)) {
          // Write it out, it is an output file that belongs to us
          writer_write(ctx, f->shm, f->size);

          // Mark buffer as empty
          f->size = 0;
        }
      }
    }
  }
}



// User is responsible for freeing *names[0] and *names
static int
create_stream_files (const char *files, size_t size,
                     enum file_type type, char ***names)
{
  int i, j, cnt;
  char *fs, *fn;

  assert(names);

  // Count and create space for names
  for (i=0, cnt=1; files[i]; ++i) {
    if (files[i] == ':') ++cnt;
  }
  *names = malloc(cnt * sizeof(char *));

  // Now, parse string and create files
  fs = strdup(files);
  for (i=0, fn = strtok(fs, ":"); fn; fn = strtok(NULL, ":"), ++i) {
    for (j = 0; j < ps_ctl->nprocesses; ++j) {
      ps_ctl_add_file(ps_ctl, j, fn, size, type);
    }
    (*names)[i] = fn;
  }

  return cnt;
}
