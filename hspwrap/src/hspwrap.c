#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mpi.h>

#include "hspwrap.h"
#include "master.h"
#include "slave.h"

#define NUM_PROCS      2

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

  if (rank) {
    slave_init(rank, ranks-1, NUM_PROCS);
  } else {
    master_init();
  }

  // Distribute DB files
  MPI_Barrier(MPI_COMM_WORLD);
  char *dbdir   = getenv("HSP_DBDIR");
  char *dbfiles = strdup(getenv("HSP_DBFILES"));
  char *fn, path[PATH_MAX];

  for (fn = strtok(dbfiles, ":"); fn; fn = strtok(NULL, ":")) {
    snprintf(path, sizeof(path), "%s/%s", dbdir, fn);    

    if (rank) {
      slave_broadcast_file(path);
    } else {
      master_broadcast_file(path);
    }
  }
  free(dbfiles);

  // FIXME: The order of things is generally wrong. Should be:
  // Fork Forker, MPI_Init, PS Ctl, EXE/DB distro, forking, main loop

  // Now print some stats
#if 0
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
