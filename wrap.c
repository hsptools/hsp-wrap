#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <mpi.h>


////////////////////////////////////////////////////////////


void Search(int rank, char *queryf, char *outf)
{
  pid_t pid;

  if( (pid=fork()) > 0 ) {
    // Parent
    int status;

    if( waitpid(pid,&status,0) < 0 ) {
      // Wait failed for some reason
      fprintf(stderr,"wait(child) failed on rank %d\n",rank);
      MPI_Abort(MPI_COMM_WORLD,1);
    }
    if( !(WIFEXITED(status) && !WEXITSTATUS(status)) ) {
      // There was an error with the child process
      fprintf(stderr,"child failed on rank %d\n",rank);
      // MPI_Abort(MPI_COMM_WORLD,1);
    }
  } else if( !pid ) {
    // Child
    char *argv[] = {"./blastall", "-p", "blastp", "-d", "/lustre/scratch/avose/dbs/pataa0/pataa", "-m", "7",
		    "-i", queryf, "-o", outf, NULL};
    char *envp[] = {"BLASTMAT=/lustre/scratch/avose/dbs/pataa0/",NULL};
    char  name[] = "/lustre/scratch/avose/wrap/blastall";

    if( execve(name,argv,envp) < 0 ) {
      fprintf(stderr,"exec failed on rank %d\n",rank);
      exit(1);
    }
    fprintf(stderr,"exec returned 0 on rank %d\n",rank);
    exit(1);
  }  else if( pid < 0 ) {
    // Error
    fprintf(stderr,"fork failed on rank %d\n",rank);
    MPI_Abort(MPI_COMM_WORLD,1);
  }
}


////////////////////////////////////////////////////////////


// Application entry point.
int main(int argc, char **argv)
{
  int  procs,rank;
  int  i;

  // Initialize MPI
  if( MPI_Init(&argc,&argv) != MPI_SUCCESS ) {
    fprintf(stderr,"Error starting MPI. Terminating.\n");
    return 1;
  }
  MPI_Comm_size(MPI_COMM_WORLD,&procs);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);

  // Be verbose
  if( !rank ) {
    fprintf(stderr,"Procs: %d.\n",procs);
  }

  // Run search
  {
    char qname[256],oname[256];

    sprintf(qname,"q%d.fasta",rank);
    sprintf(oname,"o%d.xml",rank);
    
    Search(rank,qname,oname);
  }

  // Done with MPI
  MPI_Finalize();

  // Return success
  return 0;
}
