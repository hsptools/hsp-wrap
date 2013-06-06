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

#define NUM_JOBS 5000

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
  void  *data;
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
  data = mmap(NULL, sz, PROT_READ, MAP_SHARED /*MAP_HUGETLB*/, fd, 0);
  if (data == MAP_FAILED) {
    close(fd);
    fprintf(stderr, "%s: Could not mmap file: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }
  MPI_Bcast(data, sz, MPI_BYTE, 0, MPI_COMM_WORLD);

  // Done broadcasting file, sender will no longer need it (for now)
  munmap(data, sz);
  close(fd);
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
    wait_sends(&ctx, slave_idx);

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
