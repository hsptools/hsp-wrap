#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <mpi.h>

#include "hspwrap.h"
#include "process_pool.h"
#include "master.h"
#include "slave.h"

//#define TIMING_MODE 1

// TODO: Move this timing code:
struct timing
{
  struct timespec db_start;
  struct timespec db_end;
  uintmax_t       db_kbytes;
};

double 
timeval_subtract (struct timespec *t1, struct timespec *t0)
{
  struct timespec rv;
  long sec;

  // Perform the carry for the later subtraction by updating t0. */
  if (t1->tv_nsec < t0->tv_nsec) {
    sec = (t0->tv_nsec - t1->tv_nsec) / 1000000000 + 1;
    t0->tv_nsec -= 1000000000 * sec;
    t0->tv_sec  += sec;
  }
  if (t1->tv_nsec - t0->tv_nsec > 1000000000) {
    sec = (t1->tv_nsec - t0->tv_nsec) / 1000000000;
    t0->tv_nsec += 1000000000 * sec;
    t0->tv_nsec -= sec;
  }


  // Compute the difference
  rv.tv_sec = t1->tv_sec - t0->tv_sec;
  rv.tv_nsec = t1->tv_nsec - t0->tv_nsec;

  return ((double)rv.tv_sec) + (rv.tv_nsec / 1000000000.0f);
}

void
timing_init(struct timing *t)
{
  memset(t, sizeof(struct timing), 0);
}

void
timing_record(struct timespec *ts)
{
  clock_gettime(CLOCK_MONOTONIC, ts);
}

void
timing_print(struct timing *t)
{
  double s;
  fputs("Timing Information:\n", stderr);

  s = timeval_subtract(&t->db_end, &t->db_start);
  fprintf(stderr, "  Database Broadcast: %8.2lf s (%.2lf KB/s)\n",
          s, ((double)t->db_kbytes)/s);

  /*
  fprintf(stderr, "%lld.%.9ld, %lld.%.9ld\n",
      (long long)t->db_start.tv_sec, t->db_start.tv_nsec,
      (long long)t->db_end.tv_sec, t->db_end.tv_nsec);/
  */
}

////

struct process_pool_ctl *pool_ctl;
struct timing timing;

// TODO: Remove me
size_t bcast_chunk_size;
char   input_fmt;

void
print_banner (FILE *f)
{
  fputs("  _  _ ___ ___\n"
        " | || / __| _ \\__ __ ___ _ __ _ _ __\n"
        " | __ \\__ \\  _/\\ \\V  \\V / '_/ _` | '_ \\\n"
        " |_||_|___/_|   \\_/\\_/|_| \\__,_| .__/\n"
        "   HSPwrap version 0.2.0       |_|\n\n", stderr);
}


void
print_banner_slant (FILE *f)
{
  fputs("    __ _________\n"
        "   / // / __/ _ \\_    _________ ____\n"
        "  / _  /\\ \\/ ___/ |/|/ / __/ _ `/ _ \\\n"
        " /_//_/___/_/   |__,__/_/  \\_,_/ .__/\n"
        "   HSPwrap version 0.2.0      /_/\n\n", stderr);
}


int
main (int argc, char **argv)
{
  char *ch;

  if (argc != 2) {
    fputs("Invalid number of arguments\n", stderr);
    fputs("usage: hspwrap EXEFILE\n", stderr);
    exit(EXIT_FAILURE);
  }

  ch = getenv("HSP_BCAST_CHUNK_SIZE");
  if (ch) {
    sscanf(ch, "%zu", &bcast_chunk_size);
  } else {
    bcast_chunk_size = 4L << 20;
  }

  ch = getenv("HSP_INPUT_FORMAT");
  if (!ch || ch[0] == '\0' || ch[0] == 'l') {
    info("Input format: Lines\n");
    input_fmt = 'l';
  } else if (ch[0] == 'f') {
    info("Input format: FASTA\n");
    input_fmt = 'f';
  } else {
    fputs("Invalid input format specified\n", stderr);
    exit(EXIT_FAILURE);
  }

  // Pre-fork process pool (even on master)
#ifndef TIMING_MODE
  sleep(1);
  pool_ctl = process_pool_fork();
  trace("Process pool created.\n");
  sleep(1);
#endif

  // Initialize MPI
  int rank, ranks;
  if (MPI_Init(NULL, NULL) != MPI_SUCCESS) {
    fprintf(stderr, "Error initialize MPI.\n");
    return EXIT_FAILURE;
  }

  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &ranks);
  trace("MPI Initialized.\n");

  // Initialize our state
  timing_init(&timing);
  if (rank) {
    slave_init(rank, ranks-1, NUM_PROCS);
  } else {
    print_banner_slant(stderr);
    master_init();
  }

  // Broadcast binary files first
  if (rank) {
    slave_broadcast_work_file("exefile");
  } else {
    master_broadcast_file(getenv("HSP_EXEFILE"));
  }

  // Distribute DB files
  MPI_Barrier(MPI_COMM_WORLD);
  timing_record(&timing.db_start);

  char *dbdir   = getenv("HSP_DBDIR");
  char *dbfiles = strdup(getenv("HSP_DBFILES"));
  char *fn, path[PATH_MAX];

  for (fn = strtok(dbfiles, ":"); fn; fn = strtok(NULL, ":")) {
    snprintf(path, sizeof(path), "%s/%s", dbdir, fn);    

    if (rank) {
      timing.db_kbytes += slave_broadcast_shared_file(path)/1024;
    } else {
      timing.db_kbytes += master_broadcast_file(path)/1024;
    }
  }
  free(dbfiles);

  MPI_Barrier(MPI_COMM_WORLD);
  timing_record(&timing.db_end);

#ifdef TIMING_MODE
  if (!rank) {
    timing_print(&timing);
  }
  MPI_Finalize();
  return 0;
#endif

  // FIXME: The order of things is generally wrong. Should be:
  // Fork Forker, MPI_Init, PS Ctl, EXE/DB distro, forking, main loop

#if 0
  // Now print some stats
  if (rank) {
    MPI_Barrier(MPI_COMM_WORLD);
    printf("Rank %d Processes: %d", rank, ps_ctl->nprocesses);
    printf("  Process ID: %d", getpid());
    printf("  Files: %d (", ps_ctl->ft.nfiles);
    for (i=0; i<ps_ctl->ft.nfiles; ++i) {
      printf("%s, ", ps_ctl->ft.file[i].name);
    }
    puts(")");
  } else {
    printf("Ranks: %d\n\n", ranks);
    MPI_Barrier(MPI_COMM_WORLD);
  }
#endif

  if (rank) {
    slave_main(argv[1]);
  } else {
    master_main(ranks-1);
    timing_print(&timing);
  }

  return 0;
}


// Line-based iterator
static char *
iter_line_next (char *s, char *e, char *i)
{
  char *c = i;
  for (c=i; c<e; ++c) {
    if (*c == '\n') {
      return c+1;
    }
  }
  return e;
}

// FASTA sequence iterator
static char *
iter_fasta_next (char *s, char *e, char *i)
{
  char last_c = ' ';
  char *c = i;

  for (c=i; c<e; ++c) {
    if (*c == '>' && last_c == '\n') {
      return c;
    }
    last_c = *c;
  }
  return e;
}

// Current iterator
char *
iter_next (char *s, char *e, char *i)
{
  switch (input_fmt) {
    case 'f':
      return iter_fasta_next(s, e, i);
    case 'l':
      return iter_line_next(s, e, i);
    default:
      return NULL;
  }
}

int
chunked_bcast (void *buffer, size_t count, int root, MPI_Comm comm)
{
  char   *chunk;
  size_t  off, chunk_sz;
  int     rc, rank;

  // Get rank to determine send vs recv
  rc = MPI_Comm_rank(comm, &rank);
  if (rc != MPI_SUCCESS) {
    return rc;
  }

  // write data (MPI+mmap work-around)
  chunk = malloc(MIN(bcast_chunk_size, count));
  if (!chunk) {
    fprintf(stderr, "Couldn't allocate buffer for broadcast: %s", strerror(errno));
  }
  for (off=0; off<count; off+=bcast_chunk_size) {
    chunk_sz = MIN(bcast_chunk_size, count-off);
    // We are root, get chunk ready to send
    if (rank == root) {
      memcpy(chunk, buffer+off, chunk_sz);
    }
    // Broadcast and handle error
    rc = MPI_Bcast(chunk, chunk_sz, MPI_BYTE, root, comm);
    if (rc != MPI_SUCCESS) {
      free(chunk);
      return rc;
    }
    // We are not root, copy chunk to destination
    if (rank != root) {
      memcpy(buffer+off, chunk, chunk_sz);
    }
  }

  // Done
  free(chunk);
  return MPI_SUCCESS;
}

