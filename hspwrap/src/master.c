#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mpi.h>

#include "hspwrap.h"
#include "master.h"

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


void
master_init ()
{
  // NO-OP
}


void
master_broadcast_file (const char *path)
{
  struct stat st;
  void  *file;
  size_t sz;
  int    fd;

  // Open file
  if ((fd = open(path, O_RDONLY)) == -1) {
    fprintf(stderr, "%s: Could not open file: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Broadcast file size
  if (fstat(fd, &st) < 0) {
    close(fd);
    fprintf(stderr, "%s: Could not stat file: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }
  sz = st.st_size;
  MPI_Bcast(&sz, sizeof(sz), MPI_BYTE, 0, MPI_COMM_WORLD);

  // Read and broadcast data into slave's SHMs
  file = mmap(NULL, sz, PROT_READ, MAP_SHARED /*MAP_HUGETLB*/, fd, 0);
  if (file == MAP_FAILED) {
    close(fd);
    fprintf(stderr, "%s: Could not mmap file: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }

  // send data (MPI+mmap work-around)
  chunked_bcast(file, sz, 0, MPI_COMM_WORLD);

  // Done broadcasting file, sender will no longer need it (for now)
  fprintf(stderr, "master: broadcasted file %s with size %zu\n", path, sz);
  munmap(file, sz);
  close(fd);
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
    
  int max_nseqs, wu_nseqs, rc, slave_idx, seq_idx, i;

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
  // TODO: break "load, and map" code into a function
  in_path = getenv("HSP_QFILE");
  if ((in_fd = open(in_path, O_RDONLY)) == -1) {
    fprintf(stderr, "%s: Could not open file: %s\n", in_path, strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Get file size
  if (fstat(in_fd, &in_st) < 0) {
    close(in_fd);
    fprintf(stderr, "%s: Could not stat file: %s\n", in_path, strerror(errno));
    exit(EXIT_FAILURE);
  }
  in_size = in_st.st_size;

  // Memory map the file
  in_data = mmap(NULL, in_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE /*| MAP_HUGETLB*/, in_fd, 0);
  if (in_data == MAP_FAILED) {
    close(in_fd);
    fprintf(stderr, "%s: Could not mmap file: %s\n", in_path, strerror(errno));
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
  fprintf(stderr, "master: %u inputs\n", in_cnt);

  max_nseqs = in_cnt / nslaves;
  wu_nseqs  = 1;
  
  // Post a receive work request for each slave
  for (i=0; i<nslaves; i++) {
    rc = MPI_Irecv(&ctx.slaves[i].request, sizeof(struct request), MPI_BYTE,
                   ctx.slaves[i].rank, TAG_REQUEST, MPI_COMM_WORLD,
                   &ctx.mpi_req[i]);
  }

  // Hand out work units
  in_s = in_data;
  for (seq_idx=0; seq_idx < in_cnt; seq_idx += wu_nseqs) {
    // Wait until any slave requests a work unit
    MPI_Waitany(nslaves, ctx.mpi_req, &slave_idx, MPI_STATUSES_IGNORE);
    s = &ctx.slaves[slave_idx];

    // Wait on our sends to complete if needed
    wait_sends(&ctx, slave_idx);

    // Free old temporary buffer
    if (s->wu_data) {
      free(s->wu_data);
    }

    // Got a request, service it
    switch (s->request.type) {
    case REQ_WORKUNIT:
      // Figure out how many WUs to actually send (fair)
      wu_nseqs = s->request.count;
      if (wu_nseqs > max_nseqs) {
        fprintf(stderr, "Slave %d requested %d units, limiting to %d.\n",
                slave_idx, wu_nseqs, max_nseqs); 
        wu_nseqs = max_nseqs;
      } else {
        fprintf(stderr, "Slave %d requested %d units.\n", slave_idx, wu_nseqs);
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
      s->wu_data         = malloc(s->workunit.len);
      memcpy(s->wu_data, in_s, s->workunit.len);

      // Advance our iterator
      in_s = in_e;

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
    wait_sends(&ctx, i);
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
