#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mpi.h>

#include "hspwrap.h"
#include "master.h"
#include "slave.h"

#define NUM_PROCS      1

int
main (int argc, char **argv)
{

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

  // Initialize our state
  if (rank) {
    slave_init(rank, ranks-1, NUM_PROCS);
  } else {
    fprintf(stderr, "HSPwrap: Started\n");
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

  MPI_Finalize();
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
