#include <assert.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <mpi.h>
#include <hsp/process-control.h>

#include "process_pool.h"

#define NUM_PROCS      2
#define NUM_JOBS       1000000
#define BUFFER_SIZE    (1L<<20)

// TODO: Move to util lib
#define MIN(a, b) (((a)<(b))?(a):(b))
#define MAX(a, b) (((a)>(b))?(a):(b))

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ZERO_ARRAY(a) memset(a, 0, sizeof(a))

void print_tod (FILE *f);

static void *ps_ctl_init ();
static void *ps_ctl_add_file (wid_t wid, char *name, size_t sz);
static int   ps_ctl_all_done ();
static int   ps_ctl_all_running ();

static void *create_shm (char *name, long shmsz, int *fd);
static void  broadcast_file(char *path, int rank);
static int   mkpath (char *path, mode_t mode);
static void  fork_process_pool ();
static void  push_work (wid_t wid, char *data, size_t len);

//// State

struct writer_ctx writer;
struct process_control *ps_ctl;
int ps_ctl_fd;

//// Definitions

static void
broadcast_file(char *path, int rank)
{
  struct stat st;
  void  *data;
  size_t sz;
  int    fd;

  // Broadcast file size
  if (!rank) {
    if ((fd = open(path, O_RDONLY)) == -1) {
      fprintf(stderr, "%s: Could not open file: %s\n", path, strerror(errno));
      exit(EXIT_FAILURE);
    }
    if (fstat(fd, &st) < 0) {
      close(fd);
      fprintf(stderr, "%s: Could not stat file: %s\n", path, strerror(errno));
      exit(EXIT_FAILURE);
    }
    sz = st.st_size;
  }
  MPI_Bcast(&sz, sizeof(sz), MPI_BYTE, 0, MPI_COMM_WORLD);

  // Read and broadcast data into slave's SHMs
  if (rank) {
    data = ps_ctl_add_file(-1, path, sz);
  } else {
    data = mmap(NULL, sz, PROT_READ, MAP_SHARED /*MAP_HUGETLB*/, fd, 0);
    if (data == MAP_FAILED) {
      close(fd);
      fprintf(stderr, "%s: Could not mmap file: %s\n", path, strerror(errno));
      exit(EXIT_FAILURE);
    }
  }
  MPI_Bcast(data, sz, MPI_BYTE, 0, MPI_COMM_WORLD);

  // Done broadcasting file, sender will no longer need it (for now)
  if (!rank) {
    munmap(data, sz);
    close(fd);
  }
}

static int
mkpath (char *path, mode_t mode)
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

// Tag for master messages
#define TAG_WORKUNIT  0
#define TAG_DATA      1
#define TAG_MASTERCMD 2
// Tag for slave messages
#define TAG_REQUEST 3

typedef uint32_t blockid_t;
typedef uint16_t blockcnt_t;

// Types of work units
enum workunit_type {
  WU_TYPE_EXIT,
  WU_TYPE_DATA
};


enum request_type {
  REQ_WORKUNIT,
};

struct workunit {
  enum workunit_type type;
  uint32_t           count;
  uint32_t           len;
  blockid_t          blk_id;
};

struct request {
  enum request_type  type;
  uint32_t           count;
};

struct slave_info {
  int rank;                    // Rank of this slave
  int sflag;                   // Send flag 0: idle 1: in flight
  struct request     request;  // The most recent request sent from this slave
  struct workunit    workunit; // Storage for work unit header on way to slave
  char              *wu_data;  // Storage for work unit data
};

struct master_ctx {
  struct slave_info *slaves;         // Slave states
  MPI_Request       *mpi_req;        // Outstanding MPI requests
  MPI_Request       *mpi_send_wu;    // Outstanding MPI WU send
  MPI_Request       *mpi_send_data;  // Outstanding MPI data send
  int                nslaves;        // Number of slaves
};

struct writer_ctx {
  char   *buf;                       // Output buffer
  char   *ptr;                       // Current pointer for writer
  size_t  size;                      // Total size of either buffer
  size_t  avail;                     // How much free space?

  char   *back_buf;                  // Back buffer (where we flush from)
  size_t  back_len;                  // Length of valid data in back buffer

  pthread_t       thread;
  pthread_mutex_t lock;              // Lock for entire context
  pthread_cond_t  space_avail;       // Signal to slave that there is space
  pthread_cond_t  data_avail;        // Signal to writer that there is data
};

static void pull_results (struct writer_ctx *w, wid_t wid);

void
master_wait_sends (struct master_ctx *ctx, int slave_idx)
{
  struct slave_info *s = &ctx->slaves[slave_idx];
  if (s->sflag) {
    // These should just return, but guarantee proper sequencing
    MPI_Wait(&ctx->mpi_send_wu[slave_idx],   MPI_STATUS_IGNORE);
    MPI_Wait(&ctx->mpi_send_data[slave_idx], MPI_STATUS_IGNORE);
    s->sflag = 0;
  }
}


int
master_main (int nslaves)
{
  struct master_ctx ctx;
  struct slave_info *s;
  int wu_nseqs, rc, slave_idx, seq_idx, i;

  // Init data structures
  ctx.nslaves = nslaves;
  ctx.slaves        = malloc(nslaves * sizeof(struct slave_info));
  ctx.mpi_req       = malloc(nslaves * sizeof(MPI_Request));
  ctx.mpi_send_wu   = malloc(nslaves * sizeof(MPI_Request));
  ctx.mpi_send_data = malloc(nslaves * sizeof(MPI_Request));

  if (!ctx.slaves || !ctx.mpi_req ||
      !ctx.mpi_send_wu || !ctx.mpi_send_data) {
    fprintf(stderr, "master: failed to allocate room for state\n");
    exit(EXIT_FAILURE);
  }

  for (i=0; i<nslaves; i++) {
    ctx.slaves[i].rank = i+1;
    ctx.slaves[i].sflag = 0;
    ctx.slaves[i].wu_data = NULL;
  }

  // Load Query file
  int nseqs = NUM_JOBS;
  int max_nseqs = 2;

  // Post a receive work request for each slave
  for (i=0; i<nslaves; i++) {
    rc = MPI_Irecv(&ctx.slaves[i].request, sizeof(struct request), MPI_BYTE,
                   ctx.slaves[i].rank, TAG_REQUEST, MPI_COMM_WORLD,
                   &ctx.mpi_req[i]);
  }

  // Hand out work units
  for (seq_idx=0; seq_idx < nseqs; seq_idx += wu_nseqs) {
    // Wait until any slave requests a work unit
    MPI_Waitany(nslaves, ctx.mpi_req, &slave_idx, MPI_STATUSES_IGNORE);
    s = &ctx.slaves[slave_idx];

    // Wait on our sends to complete if needed
    master_wait_sends(&ctx, slave_idx);

    // Got a request, service it
    switch (s->request.type) {
    case REQ_WORKUNIT:
      // Figure out how many WUs to actually send (fair)
      wu_nseqs = s->request.count;
      if (wu_nseqs > max_nseqs) {
        fprintf(stderr, "Slave %d requested %d units, limiting to %d.\n",
                slave_idx, wu_nseqs, max_nseqs); 
        wu_nseqs = max_nseqs;
      }


      // Build data (and leak memory)
      char *data = "Hello World!!!\n";
      s->wu_data = malloc(strlen(data) * wu_nseqs);
      s->wu_data[0] = '\0';
      for (i=wu_nseqs; i; --i) {
        strcat(s->wu_data, data);
      }

      // Prepare work unit for data
      s->workunit.type   = WU_TYPE_DATA;
      s->workunit.blk_id = seq_idx;
      s->workunit.count  = wu_nseqs;
      s->workunit.len    = strlen(s->wu_data); /*size of data in bytes*/;

      // Send work unit information
      MPI_Isend(&s->workunit, sizeof(struct workunit), MPI_BYTE, s->rank,
                TAG_WORKUNIT, MPI_COMM_WORLD, &ctx.mpi_send_wu[slave_idx]);
      // Send actual data
      MPI_Isend(s->wu_data, s->workunit.len, MPI_BYTE, s->rank,
                TAG_DATA, MPI_COMM_WORLD, &ctx.mpi_send_data[slave_idx]);
      // Record that the the two MPI requests are valid so we can wait later
      s->sflag = 1;
      // Finally, re-post a receive for this rank
      rc = MPI_Irecv(&s->request, sizeof(struct request), MPI_BYTE, s->rank,
                     TAG_REQUEST, MPI_COMM_WORLD, &ctx.mpi_req[slave_idx]);
      break;

    default:
      // Unknown request
      fprintf(stderr, "master: unknown request type from rank %d. Exiting.\n",
              s->rank);
      exit(EXIT_FAILURE);
    }
  }

  fprintf(stderr, "master: Done issuing jobs\n");

  // TODO: Consider doing a waitany, then send the exit request immediately
  // instead of waiting on *all* the ranks. Should allow ranks to start writing
  // sooner, thus a chance of quicker shutdown

  // Done handing out units, wait for all slaves' final requests
  MPI_Waitall(nslaves, ctx.mpi_req, MPI_STATUSES_IGNORE);
  // Wait for all our previous sends
  for (i=0; i<nslaves; ++i) {
    master_wait_sends(&ctx, i);
  }

  // Tell all slaves to exit (maybe can be done slave-by-slave)
  for (i=0; i<nslaves; ++i) {
    s = &ctx.slaves[i];
    s->workunit.type   = WU_TYPE_EXIT;
    s->workunit.len    = 0;
    s->workunit.blk_id = 0;
    MPI_Isend(&s->workunit, sizeof(struct workunit), MPI_BYTE, s->rank,
              TAG_WORKUNIT, MPI_COMM_WORLD, &ctx.mpi_send_wu[slave_idx]);
  }
  // Wait on all exit sends to complete
  MPI_Waitall(nslaves, ctx.mpi_send_wu, MPI_STATUSES_IGNORE);

  // Waiting on all ranks to acknowledge the EXIT
  MPI_Barrier(MPI_COMM_WORLD);

  fprintf(stderr, "master: All jobs complete, proceeding with shutdown\n");
  return 0;
}


static void *
writer_main (void *arg)
{
  struct writer_ctx *ctx = arg;

  while (1) {
    // Flush if buffer is at least half full
    pthread_mutex_lock(&ctx->lock);
    // TODO: maybe move this check to the pull function, so we don't have so many spurious wakeups
    // TODO: double-buffer instead of half-size shit
    while (ctx->avail > ctx->size/2) {
      pthread_cond_wait(&ctx->data_avail, &ctx->lock);
    }

    // TODO: Write the actual data
    fprintf(stderr, "Flushing: %zu / %zu bytes\n", ctx->avail, ctx->size);
    ctx->avail = ctx->size;
    ctx->ptr   = ctx->buf;

    pthread_mutex_unlock(&ctx->lock);
    
    // Inform slaves that there is room now
    pthread_cond_signal(&ctx->space_avail);

    // TODO: proper exit condition
  }
  
  // TODO: Flush everything that is left
}


static int
start_writer (struct writer_ctx *ctx)
{
  // TODO: error checking
  ctx->size  = BUFFER_SIZE * NUM_PROCS;
  ctx->avail = ctx->size;
  ctx->buf   = malloc(ctx->size);
  ctx->ptr   = ctx->buf;

  pthread_mutex_init(&ctx->lock, NULL);
  pthread_cond_init(&ctx->data_avail, NULL);
  pthread_cond_init(&ctx->space_avail, NULL);
  pthread_create(&ctx->thread, NULL, writer_main, ctx);
}


struct cache_buffer
{
  struct cache_buffer *next;
  char *r_ptr;  // Current read pointer
  size_t size;  // Size of the buffer
  int count;    // Number of blocks in the buffer
  int len;      // Length of valid data
  char data[];  // The whole data buffer
};


int
slave_request_work (struct cache_buffer **queue)
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

int
slave_main (int slave_idx, int nslaves, char *cmd)
{
  struct cache_buffer *queue;

  struct timeval tv[2];
  int rc, no_work, i;
  wid_t wid;

  no_work = 0;

  // TODO: Maybe move some stuff to a slave_init() function
  
  // Spawn and initialize writer thread
  start_writer(&writer);

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
    if (ps_ctl_all_done()) {
      // All processes are done! exit loop
      pthread_mutex_unlock(&ps_ctl->lock);
      break;
    }
    // Otherwise, wait for a process to need service
    while (ps_ctl_all_running()) {
      pthread_cond_wait(&ps_ctl->need_service, &ps_ctl->lock);
    }

    // Now, service all processes
    for (wid = 0; wid < ps_ctl->nprocesses; ++wid) {
      switch (ps_ctl->process_state[wid]) {
      case EOD:
        // front buffer empty implies whole queue is empty
        if (!no_work && queue == NULL) {
          // No data in the buffer, must request now
          no_work = !slave_request_work(&queue);
        }

        // Preemptively flush the results while the processes is stopped
        // FIXME: Don't do this if this is the initial EOD (first request for data)
        pull_results(&writer, wid);

        if (queue != NULL) {
          // Handle data request locally
          // Iterator code...
          char *end  = strchr(queue->r_ptr, '\n') + 1;
          off_t len  = end - queue->r_ptr;
          push_work(wid, queue->r_ptr, len);
          worker_iterations[wid]++;
          ps_ctl->process_cmd[wid] = RUN;
          ps_ctl->process_state[wid] = RUNNING;
          pthread_cond_signal(&ps_ctl->process_ready[wid]);

          // Now advance the iterator
          queue->len -= len;
          queue->count--;
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
      no_work = !slave_request_work(&queue);
    }
  }

  gettimeofday(tv+1, NULL);

  // Flush last bit of data
  for (wid = 0; wid < ps_ctl->nprocesses; ++wid) {
    pull_results(&writer, wid);
  }

  // Waiting on all ranks to acknowledge the EXIT
  MPI_Barrier(MPI_COMM_WORLD);

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

  putchar('\n');
  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    printf("Worker %2u iterations: %5u\n", i, worker_iterations[i]);
  }

  printf("Time taken: %lfs (%lfms average)\n",
      ((double)t) / 1000000.0,
      ((double)t) * ps_ctl->nprocesses / NUM_JOBS);

  return 0;
}


int
main (int argc, char **argv)
{
  int i, rc;

  if (argc != 2) {
    fputs("Invalid number of arguments\n", stderr);
    fputs("usage: hspwrap EXEFILE\n", stderr);
    exit(EXIT_FAILURE);
  }

  // Initialize MPI
  int rank, ranks;
  if (MPI_Init(&argc, &argv) != MPI_SUCCESS) {
    fprintf(stderr, "Error initialize MPI.\n");
    return EXIT_FAILURE;
  }

  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &ranks);

  // Slave Initialization
  if (rank) {
    // Create output directories
    char *outdir = getenv("HSP_OUTDIR");
    if (!outdir) {
      outdir = "hspwrap-out";
    }

    // 2 slashes, 2 char dir, 10 char rank, 1 NUL
    i = strlen(outdir) + 15;
    char *workdir = malloc(i);
    if (!workdir) {
      fprintf(stderr, "Out of memory.\n");
      return EXIT_FAILURE;
    }

    snprintf(workdir, i, "%s/%02d/%d", outdir, rank/100, rank);
    if ((rc = mkpath(workdir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH))) {
      fprintf(stderr, "Could not create work directory: %s\n", strerror(rc));
      return EXIT_FAILURE;
    }
      
    // The output directory should exist, now change dir
    if (chdir(workdir)) {
      fprintf(stderr, "Could not change to work directory: %s\n", strerror(errno));
    }

    // Cleanup
    free(workdir);

    // Prepare process control structure and streaming SHMs
    ps_ctl_init();

    for (i = 0; i < ps_ctl->nprocesses; ++i) {
      ps_ctl_add_file(i, "inputfile", BUFFER_SIZE);
      ps_ctl_add_file(i, "outputfile", BUFFER_SIZE);
    }
  }

  // Distribute DB files
  MPI_Barrier(MPI_COMM_WORLD);
  char *dbdir   = getenv("HSP_DBDIR");
  char *dbfiles = strdup(getenv("HSP_DBFILES"));
  char *fn, path[PATH_MAX];

  for (fn = strtok(dbfiles, ":"); fn; fn = strtok(NULL, ":")) {
    snprintf(path, sizeof(path), "%s/%s", dbdir, fn);    

    broadcast_file(path, rank);
  }
  free(dbfiles);

  // FIXME: The order of things is generally wrong. Should be:
  // Fork Forker, MPI_Init, PS Ctl, EXE/DB distro, forking, main loop

  // Now print some stats
  if (rank) {
    MPI_Barrier(MPI_COMM_WORLD);
    printf("Rank %d Processes: %d", rank, ps_ctl->nprocesses);
    printf("  Process ID: %d", getpid());
    printf("  Files: %d\n", ps_ctl->ft.nfiles);
  } else {
    printf("Ranks: %d\n\n", ranks);
    MPI_Barrier(MPI_COMM_WORLD);
  }

  MPI_Barrier(MPI_COMM_WORLD);

  if (rank) {
    slave_main(rank, ranks-1, argv[1]);
  } else {
    master_main(ranks-1);
  }

  MPI_Finalize();
  return 0;
}

/**
 * Print time of day to file for logging purposes
 */
void
print_tod (FILE *f)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  fprintf(f, "[%ld.%06ld] ", tv.tv_sec, tv.tv_usec);
}


/**
 * Create a shared memory segment and map it into virtual memory.
 */
static void *
create_shm (char *name, long shmsz, int *fd)
{
  void *shm;
  int   shmfd;
  char  shmname[256];

  snprintf(shmname, 256, "/mcw.%d.%s", getpid(), name);

  // Create the shared memory segment, and then mark for removal.
  // As soon as all attachments are gone, the segment will be
  // destroyed by the OS.
  shmfd = shm_open(shmname, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (shmfd < 0) {
    fprintf(stderr, "Failed to make SHM of size %ld: %s. Terminating.\n",shmsz,strerror(errno));
    exit(EXIT_FAILURE);
  }
  if ((ftruncate(shmfd, shmsz)) != 0) {
    fprintf(stderr, "Failed to resize SHM (%d).  Terminating.\n",errno);
    exit(EXIT_FAILURE);
  }
  shm = mmap(NULL, shmsz, PROT_READ | PROT_WRITE,
             MAP_SHARED /*| MAP_LOCKED | MAP_HUGETLB*/,
             shmfd, 0);

  if (shm == MAP_FAILED) {
    fprintf(stderr, "Failed to attach SHM. Terminating.\n");
    exit(EXIT_FAILURE);
  }
  
  // Return the created SHM
  if (fd) {
    *fd = shmfd;
  }
  return shm;
}


static void *
ps_ctl_init ()
{
  pthread_mutexattr_t mattr;
  pthread_condattr_t  cattr;
  int i;

  // inter-process mutexes
  if (pthread_mutexattr_init(&mattr)) {
    fprintf(stderr, "Could not initalize mutex attributes\n");
    exit(EXIT_FAILURE);
  }
  if (pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED)) {
    fprintf(stderr, "Could not set mutex attributes\n");
    exit(EXIT_FAILURE);
  }

  // inter-process condition variables
  if (pthread_condattr_init(&cattr)) {
    fprintf(stderr, "Could not initalize condition variable attributes\n");
    exit(EXIT_FAILURE);
  }
  if (pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED)) {
    fprintf(stderr, "Could not set condition variable attributes\n");
    exit(EXIT_FAILURE);
  }

  // Create main "process control" structure
  ps_ctl = create_shm(PS_CTL_SHM_NAME, sizeof(struct process_control), &ps_ctl_fd);

  // Process control data
  ps_ctl->nprocesses = MIN(NUM_PROCS, NUM_JOBS);

  pthread_mutex_init(&ps_ctl->lock, &mattr);
  pthread_cond_init(&ps_ctl->need_service, &cattr);

  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    pthread_cond_init(&ps_ctl->process_ready[i], &cattr);
    ps_ctl->process_state[i] = DONE;
    ps_ctl->process_cmd[i] = QUIT;
  }

  // Cleanup
  if (pthread_mutexattr_destroy(&mattr)) {
    fprintf(stderr, "Could not free mutex attributes\n");
    exit(EXIT_FAILURE);
  }
  if (pthread_condattr_destroy(&cattr)) {
    fprintf(stderr, "Could not free condition variable attributes\n");
    exit(EXIT_FAILURE);
  }

  // Empty file table
  ps_ctl->ft.nfiles = 0;

  return ps_ctl;
}


static void *
ps_ctl_add_file (wid_t wid, char *name, size_t sz)
{
    void *shm;
    int   fd, j;
    char  shmname[8];

    j = ps_ctl->ft.nfiles;
    snprintf(shmname, sizeof(shmname), "%d", j);
    shm = create_shm(shmname, sz, &fd);
    if (j < MAX_DB_FILES) {
      ps_ctl->ft.file[j].shm      = shm;
      ps_ctl->ft.file[j].shm_fd   = fd;
      ps_ctl->ft.file[j].shm_size = sz;
      ps_ctl->ft.file[j].wid      = wid;
      ps_ctl->ft.file[j].size     = 0;
      strcpy(ps_ctl->ft.file[j].name, name);

      ps_ctl->ft.nfiles++;
    } else {
      fprintf(stderr, "Too many DB files; increase MAX_DB_FILES. Terminating.\n");
      exit(EXIT_FAILURE);
    }
    return shm;
}


static int
ps_ctl_all_done ()
{
  int i;
  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    if (ps_ctl->process_state[i] != DONE) {
      return 0;
    }
  }
  return 1;
}


static int
ps_ctl_all_running ()
{
  int i;
  for (i = 0; i < ps_ctl->nprocesses; ++i) {
    if (ps_ctl->process_state[i] != RUNNING) {
      return 0;
    }
  }
  return 1;
}


static void
push_work (wid_t wid, char *data, size_t len)
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
      //fprintf(stderr, "trimming output. (%zu bytes)\n", f->size);
      
      // Make sure we have enough room to write the data
      pthread_mutex_lock(&ctx->lock);
      while (ctx->avail < f->size) {
        pthread_cond_wait(&ctx->space_avail, &ctx->lock);
      }

      // .. and write it
      memcpy(ctx->ptr, f->shm, f->size);
      ctx->ptr   += f->size;
      ctx->avail -= f->size;
      pthread_mutex_unlock(&ctx->lock);
      pthread_cond_signal(&ctx->data_avail);

      // Mark buffer as empty
      f->size = 0;
    }
  }
}


static void
fork_process_pool (char *cmd)
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

