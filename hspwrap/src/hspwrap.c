#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mpi.h>

#include "hspwrap.h"
#include "process_pool.h"
#include "master.h"
#include "slave.h"

struct process_pool_ctl *pool_ctl;

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

  if (argc != 2) {
    fputs("Invalid number of arguments\n", stderr);
    fputs("usage: hspwrap EXEFILE\n", stderr);
    exit(EXIT_FAILURE);
  }

  // Pre-fork process pool (even on master)
  pool_ctl = process_pool_fork();

  // Initialize MPI
  int rank, ranks;
  if (MPI_Init(NULL, NULL) != MPI_SUCCESS) {
    fprintf(stderr, "Error initialize MPI.\n");
    return EXIT_FAILURE;
  }

  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &ranks);

  // Initialize our state
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
  char *dbdir   = getenv("HSP_DBDIR");
  char *dbfiles = strdup(getenv("HSP_DBFILES"));
  char *fn, path[PATH_MAX];

  for (fn = strtok(dbfiles, ":"); fn; fn = strtok(NULL, ":")) {
    snprintf(path, sizeof(path), "%s/%s", dbdir, fn);    

    if (rank) {
      slave_broadcast_shared_file(path);
    } else {
      master_broadcast_file(path);
    }
  }
  free(dbfiles);

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

  MPI_Barrier(MPI_COMM_WORLD);

  if (rank) {
    slave_main(argv[1]);
  } else {
    master_main(ranks-1);
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
  return iter_fasta_next(s, e, i);
}

int chunked_bcast (void *buffer, int count, int root, MPI_Comm comm)
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
  chunk = malloc(MIN(BCAST_CHUNK_SIZE, count));
  for (off=0; off<count; off+=BCAST_CHUNK_SIZE) {
    chunk_sz = MIN(BCAST_CHUNK_SIZE, count-off);
    // We are root, get chunk ready to send
    if (rank == root) {
      //fprintf(stderr,"  Sending chunk %u/%d\n", off/BCAST_CHUNK_SIZE + 1, count/BCAST_CHUNK_SIZE + 1);
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

