#include <assert.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mpi.h>

#include "hspwrap.h"
#include "master.h"
#include "process_pool.h"

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

static void wait_sends (struct master_ctx *ctx, int slave_idx);

extern struct process_pool_ctl *pool_ctl;
extern size_t bcast_chunk_size;

struct reader_ctl {
  pthread_cond_t cmd_ready;
  pthread_cond_t data_ready;
  pthread_mutex_t lock;

  ssize_t  read_cnt;
  char    *buf;
  int      fd;
};

pthread_t reader_thread;
struct reader_ctl reader;

void *
master_reader (void *arg)
{
  ssize_t cnt;
  while (1) {
    // Get cmd
    pthread_mutex_lock(&reader.lock);
    while (reader.read_cnt == 0) {
      pthread_cond_wait(&reader.cmd_ready, &reader.lock);
    }
    cnt = reader.read_cnt;
    pthread_mutex_unlock(&reader.lock);

    // Handle exit
    if (reader.read_cnt == -1) {
      break;
    }

    // Do the read

    pthread_mutex_lock(&reader.lock);
    // copy buffer
    pthread_cond_signal(&reader.data_ready);
    pthread_mutex_unlock(&reader.lock);
  }
  return NULL;
}

void
master_init ()
{
  int rc;

  // TODO: threaded read
  /*
  pthread_cond_init(&reader.cmd_ready, NULL);
  pthread_cond_init(&reader.data_ready, NULL);
  pthread_mutex_init(&reader.lock, NULL);
  reader.read_cnt = 0;
  reader.buf = malloc(bcast_chunk_size);

  rc = pthread_create(&reader_thread, NULL, master_reader, &reader);
  */
}


ssize_t
master_broadcast_file (const char *path)
{
  struct stat st;
  char  *chunk;
  size_t chunk_sz, next_sz, sz, br;
  ssize_t b;
  off_t  chunk_off, next_off;
  int    fd, rc;

  // Open file
  if ((fd = open(path, O_RDONLY)) == -1) {
    fprintf(stderr, "%s: Could not open file: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Get file size
  if (fstat(fd, &st) < 0) {
    fprintf(stderr, "%s: Could not stat file: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }
  sz  = st.st_size;
  lseek(fd, 0, SEEK_SET);

  // Tell reader where to read from
  // TODO: threaded read
  /*
  reader.fd = fd;
  */

  // We will read sequentially
  posix_fadvise(fd, 0, sz, POSIX_FADV_SEQUENTIAL);

  // Create buffer
  chunk = malloc(MIN(bcast_chunk_size, sz));
  if (!chunk) {
    fprintf(stderr, "Couldn't allocate buffer for broadcast: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Init counters  
  next_off = 0;
  next_sz = MIN(bcast_chunk_size, sz);

  // Page-in next chunk
  posix_fadvise(fd, next_off, next_sz, POSIX_FADV_WILLNEED);
  // TODO: threaded read
  /*
  pthread_mutex_lock(&reader.lock);
  reader.reader_cnt = next_sz;
  pthread_cond_signal(&reader.cmd_ready);
  pthread_mutex_unlock(&reader.lock);
  */

  // Broadcast the size
  MPI_Bcast(&sz, sizeof(sz), MPI_BYTE, 0, MPI_COMM_WORLD);

  while (next_off < sz) {
    // Read the current chunk
    chunk_off = next_off;
    chunk_sz  = next_sz;

    for (br=0; br<chunk_sz; br+=b) {
      b = read(fd, chunk+br, chunk_sz-br);
      if (b == -1) {
	fprintf(stderr, "Couldn't read file during broadcast: %s", strerror(errno));
	free(chunk);
	close(fd);
	exit(EXIT_FAILURE);
      }
    }

    // We don't need this data cached anymore, but need the next one
    next_off += chunk_sz;
    next_sz = MIN(bcast_chunk_size, sz - next_off);
    posix_fadvise(fd, chunk_off, chunk_sz, POSIX_FADV_DONTNEED);
    posix_fadvise(fd, next_off, next_sz, POSIX_FADV_WILLNEED);

    // Broadcast and handle error
    rc = MPI_Bcast(chunk, chunk_sz, MPI_BYTE, 0, MPI_COMM_WORLD);
    if (rc != MPI_SUCCESS) {
      free(chunk);
      close(fd);
      return -1;
    }
  }
  close(fd);
  free(chunk);

  if (rc == MPI_SUCCESS) {
    return sz;
  } else {
    return -1;
  }
}


int
master_main (int nslaves)
{
  struct master_ctx ctx;
  struct slave_info *s;

  char        *in_path;
  int          in_fd;
  struct stat  in_st;
  char        *in_data;
  size_t       in_size;
  unsigned     in_cnt;

  char *in_s, *in_e;

  int max_nseqs, wu_nseqs, slave_idx, seq_idx, rc, i;
  int req_idx, req_type, nreqs;
  int nrunning;
  int outstandings[nslaves], outstanding;
  int percent, last_percent;

  // Init data structures
  seq_idx     = 0;
  nreqs       = nslaves * 3;
  ctx.nslaves = nslaves;
  ctx.slaves        = malloc(nslaves * sizeof(struct slave_info));
  ctx.mpi_req       = malloc(nreqs   * sizeof(MPI_Request));

  int nidxs;
  int *idxs = malloc(nreqs*sizeof(int));

  if (!ctx.slaves || !ctx.mpi_req) {
    fprintf(stderr, "master: failed to allocate room for state\n");
    exit(EXIT_FAILURE);
  }

  for (i=0; i<nslaves; ++i) {
    ctx.slaves[i].rank = i+1;
    ctx.slaves[i].sflag = 1; // Waiting on 1:request 2:info 3:data
    ctx.slaves[i].wu_data = malloc(BUFFER_SIZE);
    outstandings[i] = 0;
  }
  outstanding = 0;
  for (i=0; i<nreqs; ++i) {
    ctx.mpi_req[i] = MPI_REQUEST_NULL;
  } 

  // Load Query file
  // TODO: break "load, and map" code into a function
  in_path = getenv("HSP_INFILES");
  if ((in_fd = open(in_path, O_RDONLY)) == -1) {
    fprintf(stderr, "master: %s: Could not open file: %s\n", in_path, strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Get file size
  if (fstat(in_fd, &in_st) < 0) {
    close(in_fd);
    fprintf(stderr, "master: %s: Could not stat file: %s\n", in_path, strerror(errno));
    exit(EXIT_FAILURE);
  }
  in_size = in_st.st_size;

  // Memory map the file
  in_data = mmap(NULL, in_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE /*| MAP_HUGETLB*/, in_fd, 0);
  if (in_data == MAP_FAILED) {
    close(in_fd);
    fprintf(stderr, "master: %s: Could not mmap file: %s\n", in_path, strerror(errno));
    exit(EXIT_FAILURE);
  }
  // Let kernel know we expect to use all the data, sequentially
  madvise(in_data, in_size, MADV_SEQUENTIAL | MADV_WILLNEED);

  // Count number of inputs
  in_cnt=1;
  in_s=in_data;
  while((in_e = iter_next(in_data, in_data+in_size, in_s)) < in_data+in_size) {
    in_cnt++;
    in_s = in_e;
  }
  info("master: Counted %u inputs\n", in_cnt);

  max_nseqs = in_cnt / nslaves;
  wu_nseqs  = 1;

  // FIXME: These Barriers are probably not needed anymore
  MPI_Barrier(MPI_COMM_WORLD);
  process_pool_spawn(pool_ctl, ".", -1);
  MPI_Barrier(MPI_COMM_WORLD);

  info("master: Issuing jobs...\n");
  
  // Post a receive work request for each slave
  for (slave_idx=0; slave_idx<nslaves; ++slave_idx) {
    rc = MPI_Irecv(&ctx.slaves[slave_idx].request, sizeof(struct request), MPI_BYTE,
                   ctx.slaves[slave_idx].rank, TAG_REQUEST, MPI_COMM_WORLD,
                   &ctx.mpi_req[slave_idx]);
    outstandings[slave_idx]++;
    outstanding++;
  }

  // Update slave states
  in_s = in_data;
  nrunning = nslaves;
  percent = last_percent = 0;
  while (seq_idx < in_cnt && nrunning == nslaves) {
    // Limit by max number of sequences
    if (max_nseqs > in_cnt-seq_idx) {
      max_nseqs = in_cnt-seq_idx;
    }

    // Wait until any request completes
    MPI_Waitany(nreqs, ctx.mpi_req, &req_idx, MPI_STATUSES_IGNORE);
    //MPI_Waitsome(nreqs, ctx.mpi_req, &nidxs, idxs, MPI_STATUSES_IGNORE);
    //for (i=0; i<nidxs; ++i) {
    //  req_idx = idxs[i];

      // Determine the slave and request-type associated with this request
      req_type  = req_idx / nslaves;
      slave_idx = req_idx % nslaves;
      outstandings[slave_idx]--;
      outstanding--;
      s = &ctx.slaves[slave_idx];

      // Wait on our sends to complete if needed
      //wait_sends(&ctx, slave_idx);

      trace("master: MPI-Request completed (slave: %d, type: %d flag: %x -> ", slave_idx, req_type, s->sflag);
      switch (req_type) {
      case 0:
        s->sflag &= ~1;
        break;
      case 1:
        s->sflag &= ~2;
        break;
      case 2:
        s->sflag &= ~4;
        break;
      default:
        // Programming error
        assert(0);
        break;
      }
      trace("%x)\n", s->sflag);

    //}
    //
    //for (i=0; i<nslaves; ++i) {
    //  s = &ctx.slaves[i];
      if (!s->sflag) {
        // Hand out work units
        // Got a request, service it
        switch (s->request.type) {
        case REQ_WORKUNIT:
          // Figure out how many WUs to actually send (fair)
          wu_nseqs = s->request.count;
          if (wu_nseqs > max_nseqs) {
            info("master: Slave %d requested %d units, limiting to %d.\n",
                slave_idx, wu_nseqs, max_nseqs); 
            wu_nseqs = max_nseqs;
          } else {
            trace("master: Slave %d requested %d units.\n", slave_idx, wu_nseqs);
          }

          // Determine size of all the data we need
          for (in_e=in_s, i=wu_nseqs; i; --i) {
            in_e = iter_next(in_data, in_data+in_size, in_e);
          }

          // Prepare work unit for data
          s->workunit.len    = in_e - in_s;
          s->workunit.type   = WU_TYPE_DATA;
          s->workunit.blk_id = seq_idx;
          s->workunit.count  = wu_nseqs;
          // mmap hack again
          memcpy(s->wu_data, in_s, s->workunit.len);

          // Advance our iterator
          in_s = in_e;

          // Send work unit information
          rc = MPI_Send(&s->workunit, sizeof(struct workunit), MPI_BYTE, s->rank,
              TAG_WORKUNIT, MPI_COMM_WORLD);
          // Send actual data
          rc = MPI_Send(s->wu_data, s->workunit.len, MPI_BYTE, s->rank,
              TAG_DATA, MPI_COMM_WORLD);
          trace("master: Slave %d send data (%d bytes, rank %d): %d\n", slave_idx, s->workunit.len, s->rank, rc);
          // Finally, re-post a receive for this rank
          rc = MPI_Irecv(&s->request, sizeof(struct request), MPI_BYTE, s->rank,
              TAG_REQUEST, MPI_COMM_WORLD, &ctx.mpi_req[slave_idx]);
          outstandings[slave_idx]++;
          outstanding++;

          // Record that we are need to wait for sends and a new request before doing any action
          s->sflag = 1;
          // Advance the counter
          seq_idx += wu_nseqs;
          break;

	case REQ_ABORT:
	  // A client is aborting, take down the whole system (for now)
	  // Do not send -- client is not receiving anymore!
          fprintf(stderr, "master: Slave %d aborted. Exiting.\n", s->rank);
	  nrunning--;
	  break;

        default:
          // Unknown request
          fprintf(stderr, "master: unknown request type from rank %d. Exiting.\n",
              s->rank);
          exit(EXIT_FAILURE);
        }
      }
      //}
      
    percent = 100 * seq_idx / in_cnt;
    if (percent != last_percent) {
      fprintf(stderr, "master: %d%% percent complete\n", percent);
      last_percent = percent;
    }

  } // End service loop

  info("master: Done issuing jobs\n");

  // Tell all slaves to exit (maybe can be done slave-by-slave)
  for (; nrunning; nrunning--) {
    trace("master: Waiting for %d slaves to exit.\n", nrunning);

    MPI_Waitany(nreqs, ctx.mpi_req, &req_idx, MPI_STATUSES_IGNORE);
    // Determine the slave and request-type associated with this request
    req_type  = req_idx / nslaves;
    slave_idx = req_idx % nslaves;
    s = &ctx.slaves[slave_idx];

    outstandings[slave_idx]--;
    outstanding--;

    switch (s->request.type) {
    case REQ_WORKUNIT:
      // Kill the slave
      trace("master: Terminating slave %d.\n", slave_idx);
      s->workunit.type   = WU_TYPE_EXIT;
      s->workunit.len    = 0;
      s->workunit.blk_id = 0;
      MPI_Send(&s->workunit, sizeof(struct workunit), MPI_BYTE, s->rank,
               TAG_WORKUNIT, MPI_COMM_WORLD);
      trace("master: Terminated slave %d.\n", slave_idx);
      break;

    case REQ_ABORT:
      // Another slave aborted, no need to do anything
      fprintf(stderr, "master: Slave %d aborted during shutdown.\n", s->rank);
      break;

    default:
      // Unknown request
      fprintf(stderr, "master: unknown request type from rank %d. Exiting.\n",
              s->rank);
      exit(EXIT_FAILURE);
    }
  }

  // Safety blanket
  /*
  fprintf(stderr, "master: Freeing any outstanding requests\n");
  for (i=0; i<nreqs; ++i) {
MPI_Request_free(&ctx.mpi_req[i]);
  }
  */

  MPI_Barrier(MPI_COMM_WORLD);
  info("master: All jobs complete, proceeding with shutdown\n");
  //sleep(5); // FIXME
  MPI_Finalize();
  return 0;
}


static void
wait_sends (struct master_ctx *ctx, int slave_idx)
{
  struct slave_info *s = &ctx->slaves[slave_idx];
  if (s->sflag) {
    // These should just return, but guarantee proper sequencing
    MPI_Wait(&ctx->mpi_send_wu[slave_idx],   MPI_STATUS_IGNORE);
    MPI_Wait(&ctx->mpi_send_data[slave_idx], MPI_STATUS_IGNORE);
    s->sflag = 0;
  }
}
