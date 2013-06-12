#include <assert.h>
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
    
  int max_nseqs, wu_nseqs, slave_idx, seq_idx, rc, i;
  int req_idx, req_type, nreqs;

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
  }
  for (i=0; i<nreqs; ++i) {
    ctx.mpi_req[i] = MPI_REQUEST_NULL;
  } 

  // Load Query file
  // TODO: break "load, and map" code into a function
  in_path = getenv("HSP_QFILE");
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
  fprintf(stderr, "master: %u inputs\n", in_cnt);

  max_nseqs = in_cnt / nslaves;
  wu_nseqs  = 1;

  //MPI_Barrier(MPI_COMM_WORLD);
  fprintf(stderr, "master: past barrier 1\n");
  MPI_Barrier(MPI_COMM_WORLD);
  fprintf(stderr, "master: past barrier 2\n");
  
  // Post a receive work request for each slave
  for (i=0; i<nslaves; i++) {
    rc = MPI_Irecv(&ctx.slaves[i].request, sizeof(struct request), MPI_BYTE,
                   ctx.slaves[i].rank, TAG_REQUEST, MPI_COMM_WORLD,
                   &ctx.mpi_req[i]);
  }

  // Update slave states
  in_s = in_data;
  while (seq_idx < in_cnt) {
    // Wait until any request completes
    MPI_Waitsome(nreqs, ctx.mpi_req, &nidxs, idxs, MPI_STATUSES_IGNORE);

    for (i=0; i<nidxs; ++i) {
      req_idx = idxs[i];

      // Determine the slave and request-type associated with this request
      req_type  = req_idx / nslaves;
      slave_idx = req_idx % nslaves;
      s = &ctx.slaves[slave_idx];

      // Wait on our sends to complete if needed
      //wait_sends(&ctx, slave_idx);

      fprintf(stderr, "master: MPI-Request completed (slave: %d, type: %d flag: %x -> ", slave_idx, req_type, s->sflag);
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
      fprintf(stderr, "%x)\n", s->sflag);
    }

    for (i=0; i<nslaves; ++i) {
      s = &ctx.slaves[i];
      if (!s->sflag) {
	
	// Hand out work units
	// Got a request, service it
	switch (s->request.type) {
	case REQ_WORKUNIT:
	  // Figure out how many WUs to actually send (fair)
	  wu_nseqs = s->request.count;
	  if (wu_nseqs > max_nseqs) {
	    fprintf(stderr, "master: Slave %d requested %d units, limiting to %d.\n",
		slave_idx, wu_nseqs, max_nseqs); 
	    wu_nseqs = max_nseqs;
	  } else {
	    fprintf(stderr, "master: Slave %d requested %d units.\n", slave_idx, wu_nseqs);
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
	  fprintf(stderr, "master: Slave %d send info: %d\n", slave_idx, rc);
	  // Send actual data
	  rc = MPI_Send(s->wu_data, s->workunit.len, MPI_BYTE, s->rank,
	      TAG_DATA, MPI_COMM_WORLD);
	  fprintf(stderr, "master: Slave %d send data (%d bytes, rank %d): %d\n", slave_idx, s->workunit.len, s->rank, rc);
	  // Finally, re-post a receive for this rank
	  rc = MPI_Irecv(&s->request, sizeof(struct request), MPI_BYTE, s->rank,
	      TAG_REQUEST, MPI_COMM_WORLD, &ctx.mpi_req[slave_idx]);
	  fprintf(stderr, "master: Slave %d irecv: %d\n", slave_idx, rc);

	  // Record that we are need to wait for sends and a new request before doing any action
	  s->sflag = 1;
	  // Advance the counter
	  seq_idx += wu_nseqs;
	  break;

	default:
	  // Unknown request
	  fprintf(stderr, "master: unknown request type from rank %d. Exiting.\n",
	      s->rank);
	  exit(EXIT_FAILURE);
	}
      }
    }
    
  } // End service loop

  fprintf(stderr, "master: Done issuing jobs\n");

  // TODO: Consider doing a waitany, then send the exit request immediately
  // instead of waiting on *all* the ranks. Should allow ranks to start writing
  // sooner, thus a chance of quicker shutdown

  // Done handing out units, wait for all slaves' final requests
  MPI_Waitall(nreqs, ctx.mpi_req, MPI_STATUSES_IGNORE);

  // Tell all slaves to exit (maybe can be done slave-by-slave)
  for (i=0; i<nslaves; ++i) {
    s = &ctx.slaves[i];
    s->workunit.type   = WU_TYPE_EXIT;
    s->workunit.len    = 0;
    s->workunit.blk_id = 0;
    MPI_Isend(&s->workunit, sizeof(struct workunit), MPI_BYTE, s->rank,
              TAG_WORKUNIT, MPI_COMM_WORLD, &ctx.mpi_req[nslaves + slave_idx]);
  }
  // Wait on all exit sends to complete
  MPI_Waitall(nreqs, ctx.mpi_req, MPI_STATUSES_IGNORE);

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
