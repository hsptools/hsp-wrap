#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>


////////////////////////////////////////////////////////////


// I think it's crazy to make the user do this,
// but the documentation states that *I*, the user
// of semctl() needs to define this union.
union semun {
  int              val;    /* Value for SETVAL */
  struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
  unsigned short  *array;  /* Array for GETALL, SETALL */
  struct seminfo  *__buf;  /* Buffer for IPC_INFO
			      (Linux-specific) */
};


////////////////////////////////////////////////////////////


// SHM removal function.
//
// Removes SHM with key "key".
// Returns -1 on error.
int shmrm(key_t key)
{
  int id;

  // Get a handle to the SHM
  id = shmget(key, 0, 0);
  if( id == -1 ) {
    return -1;
  }
  
  // Tell the OS to remove the SHM
  return shmctl(id, IPC_RMID, NULL);
}


// sem removal function.
//
// Removes the semaphore set with the key "key".
// Returns -1 on error.
int semrm(key_t key)
{
  int id;

  // Get a handle to the sems
  id = semget(key, 0, 0);
  if( id == -1 ) {
    return -1;
  }

  // Tell the OS to remove the sems
  return semctl(id, 0, IPC_RMID);
}


////////////////////////////////////////////////////////////


// Application entry point.
//
// Gets a list of SHMs on the system.
// Then, removes them.
// Gets a list of sems on the system.
// Then, removes them.
// Prints out totals of removed objects.
int main(int argc, char **argv)
{
  int  procs,rank;
  int  shm_rm=0,sem_rm=0,shm_fl=0;
  int *rbuf=NULL;
  int  i;


  // Initialize MPI
  if( MPI_Init(&argc,&argv) != MPI_SUCCESS ) {
    fprintf(stderr,"Error starting MPI. Terminating.\n");
    return 1;
  }
  MPI_Comm_size(MPI_COMM_WORLD,&procs);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);

  // Remove all SHMs
  {
    struct shm_info shm_info;
    struct shmid_ds shmseg;
    int             maxid, shmid, id;

    // Get the largest SHM id on the system
    maxid = shmctl(0, SHM_INFO, (struct shmid_ds *) (void *) &shm_info);
    if( maxid < 0 ) {
      fprintf(stderr,"shmctl() says shared memory is not supported.\n");
      MPI_Abort(1,MPI_COMM_WORLD);
    }
    
    // Check each id less than the max
    for(id=shm_rm=shm_fl=0; id <= maxid; id++) {
      // Get info on the SHM
      shmid = shmctl(id, SHM_STAT, &shmseg);
      if( shmid < 0 ) {
	// No SHM with this id exists; keep trying the others.
	continue;
      }
      // Remove the SHM if it is not the system SHM (id == 0)
      if( shmseg.shm_perm.__key ) {
	if( shmrm(shmseg.shm_perm.__key) < 0 ) {
	  // The SHM could not be removed for some reason...
	  shm_fl++;
	  continue;
	}
	// The SHM was removed
	shm_rm++;
      }
    }
  }

  // Remove all semaphores
  {
    struct semid_ds semary;
    struct seminfo  seminfo;
    union  semun    arg;
    int             maxid, semid, id;

    // Get the largest sem id on the system
    arg.array = (unsigned short *) (void *) &seminfo;
    maxid = semctl(0, 0, SEM_INFO, arg);
    if( maxid < 0 ) {
      fprintf(stderr,"semctl() says semaphores are not supported.\n");
      MPI_Abort(1,MPI_COMM_WORLD);
    }

    // Check each id less than the max
    for(id=sem_rm=0; id <= maxid; id++) {
      // Get info on the sem
      arg.buf = (struct semid_ds *) &semary;
      semid = semctl(id, 0, SEM_STAT, arg);
      if( semid < 0 ) {
        // No semaphore with this id exists; keep trying the others.
        continue;
      }
      // Remove the semaphore set
      if( semrm(semary.sem_perm.__key) < 0 ) {
	// The semaphore set could not be removed for some reason...
	continue;
      }
      // The semaphore set was removed
      sem_rm++;
    }

  }

  // Only root needs the array (entry per node)
  if( !rank ) {
    if( !(rbuf=(int*)malloc(procs*sizeof(int))) ) {
      // Malloc failed
      MPI_Abort(1,MPI_COMM_WORLD);
    }
  }
   
  // Gather all removed SHM counts
  MPI_Gather(&shm_rm, 1, MPI_INT, rbuf, 1, MPI_INT, 0, MPI_COMM_WORLD);
  
  // Only root sums for total count
  if( !rank ) {
    for(i=shm_rm=0; i<procs; i++) {
      shm_rm += rbuf[i];
    }
  }

  // Gather all removed sem counts
  MPI_Gather(&sem_rm, 1, MPI_INT, rbuf, 1, MPI_INT, 0, MPI_COMM_WORLD);

  // Only root sums for total count
  if( !rank ) {
    for(i=sem_rm=0; i<procs; i++) {
      sem_rm += rbuf[i];
    }
  }

  // Gather all not removed shm counts
  MPI_Gather(&shm_fl, 1, MPI_INT, rbuf, 1, MPI_INT, 0, MPI_COMM_WORLD);

  // Only root sums for total count
  if( !rank ) {
    for(i=shm_fl=0; i<procs; i++) {
      shm_fl += rbuf[i];
    }
  }

  // Only root needs to report stats
  if( !rank ) {
    // Print out number of removed segments
    printf("Removed a total of %d SHMs for %d nodes.\n",shm_rm,procs);
    printf("Removed a total of %d sems for %d nodes.\n",sem_rm,procs);

    // Print out any failures
    if( shm_fl ) {
      printf("\n!! Failed to remove %d SHMs for %d nodes:\n",shm_fl,procs);
      for(i=0; i<procs; i++) {
	if( rbuf[i] ) {
	  printf("Rank %d still has %d SHMs.\n",i,rbuf[i]);
	}
      }
    }

    // Cleanup
    free(rbuf);
  }

  // Mark that we have cleaned
  if( !rank ) {
    FILE *f;

    if( !(f=fopen("cleaned","w")) ) {
      MPI_Abort(1,MPI_COMM_WORLD);
    }
    fclose(f);
  }

  // Done with MPI
  MPI_Finalize();

  // Return success
  return 0;
}
