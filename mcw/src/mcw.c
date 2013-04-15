#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <mpi.h>
#include <math.h>
#include <signal.h>
#include <zlib.h>

#include "tscq.h"
#include "mcw.h"

#define UNUSED(param) (void)(param)
#define MCW_BIN       "mcw"
#define EXE_BASE      "task.bin"

extern char **environ;

////////////////////////////////////////////////////////////////////////////////
//                               Global State                                 //
////////////////////////////////////////////////////////////////////////////////


// CLI/env arg info
args_t args; 


// Master info struct
masterinfo_t MasterInfo;


// Slave info struct
slaveinfo_t SlaveInfo;


// Result buffer information
resultbuff_t *resultbuff;
pthread_t     resultbuff_thread;
volatile int  result_thread_error=0;


// Information about the DB files (static global input files)
// file_sizes[0] is the exe file
filesizes_t *file_sizes;
int          file_sizes_fd;
int          file_is_shm[MAX_DB_FILES];
void        *shm_exe;
long         shm_exe_sz;


// MPI rank
volatile int Rank;


// Use by both master and slave for timing
float init_time;


// !!av: Gating hack
char  **Cbuff;
uint32_t *Nc;
int    *Fd;
int    ConcurrentWriters = 512;

////////////////////////////////////////////////////////////////////////////////
//                                 Util Code                                  //
////////////////////////////////////////////////////////////////////////////////

// TODO: link with strutils.o
int
str_cnt_chr(const char *str, char chr) {
  int i = 0;
  while( str[i] ) {
    if( str[i] == chr ) {
      ++i;
    } else {
      ++str;
    }
  }
  return i;
}


static void Vprint(int sev, char *fmt, ...)
{
  va_list args;
  char    buf[4096];

  if( VERBOSITY >= sev ) {
    buf[0] = '\0';
#if DBGTIME
    sprintf(buf,"[%lu] ",((unsigned long)time(NULL)));
#endif
    va_start(args, fmt);
    vsnprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), fmt, args);
    va_end(args);
    fprintf(stderr,"%s",buf);
  }

}


static void Report_Timings(int rank, 
			   float i, float o, float ic, float oc, float vi, float vo, 
			   unsigned long b_i, unsigned long b_o, unsigned long b_id, unsigned long b_od)
{
  float t[10];
  float v[10];

  memset(t,0,sizeof(t));

  if( !rank ) {
    // Root uses an empty input (t) and fills (v)
    MPI_Reduce(t, v, 10, MPI_FLOAT, MPI_SUM, 0, MPI_COMM_WORLD);
    Vprint(SEV_NRML,"Real input time:           %.3f  (per thread: %.3f).\n",
	   v[0],(v[0]/(MasterInfo.nslaves*MCW_NCORES)));
    Vprint(SEV_NRML,"Real output time:          %.3f  (per thread: %.3f).\n",
	   v[1],(v[1]/(MasterInfo.nslaves*MCW_NCORES)));
    Vprint(SEV_NRML,"Input decompression time:  %.3f  (per thread: %.3f).\n",
	   v[2],(v[2]/(MasterInfo.nslaves*MCW_NCORES)));
    Vprint(SEV_NRML,"Output compression time:   %.3f  (per thread: %.3f).\n",
 	   v[3],(v[3]/(MasterInfo.nslaves*MCW_NCORES)));
    Vprint(SEV_NRML,"Virtual input time:        %.3f  (per thread: %.3f).\n",
	   v[4],(v[4]/(MasterInfo.nslaves*MCW_NCORES)));
    Vprint(SEV_NRML,"Virtual output time:       %.3f  (per thread: %.3f).\n",
	   v[5],(v[5]/(MasterInfo.nslaves*MCW_NCORES)));
    Vprint(SEV_NRML,"Compressed input:          %.3f  (per thread: %.3f).\n",
	   v[6],(v[6]/(MasterInfo.nslaves*MCW_NCORES)));
    Vprint(SEV_NRML,"Decompressed input:        %.3f  (per thread: %.3f).\n",
	   v[7],(v[7]/(MasterInfo.nslaves*MCW_NCORES)));
    Vprint(SEV_NRML,"Uncompressed output:       %.3f  (per thread: %.3f).\n",
	   v[8],(v[8]/(MasterInfo.nslaves*MCW_NCORES)));
    Vprint(SEV_NRML,"Compressed output:         %.3f  (per thread: %.3f).\n\n",
	   v[9],(v[9]/(MasterInfo.nslaves*MCW_NCORES)));
    fflush(stderr);
    fflush(stdout);
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
  } else {
    // Slaves fill in values
    v[0] = i;
    v[1] = o;
    v[2] = ic;
    v[3] = oc;
    v[4] = vi;
    v[5] = vo;
    v[6] = b_i;
    v[7] = b_id;
    v[8] = b_od;
    v[9] = b_o;
    MPI_Reduce(v, t, 10, MPI_FLOAT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
    // Slaves report their timings as well
#if 0
    Vprint(SEV_NRML,"{%d} Real input time:           %.3f\n", rank, v[0]);
    Vprint(SEV_NRML,"{%d} Real output time:          %.3f\n", rank, v[1]);
    Vprint(SEV_NRML,"{%d} Input decompression time:  %.3f\n", rank, v[2]);
    Vprint(SEV_NRML,"{%d} Output compression time:   %.3f\n", rank, v[3]);
    Vprint(SEV_NRML,"{%d} Virtual input time:        %.3f\n", rank, v[4]);
    Vprint(SEV_NRML,"{%d} Virtual output time:       %.3f\n", rank, v[5]);
    Vprint(SEV_NRML,"{%d} Compressed input:          %.3f\n", rank, v[6]);
    Vprint(SEV_NRML,"{%d} Decompressed input:        %.3f\n", rank, v[7]);
    Vprint(SEV_NRML,"{%d} Uncompressed output:       %.3f\n", rank, v[8]);
    Vprint(SEV_NRML,"{%d} Compressed output:         %.3f\n", rank, v[9]);
#endif
    printf("%d  %.4f  %.4f  %.4f  %.4f  %.4f  %.4f  %.4f  %.4f  %.4f  %.4f\n",
	   rank,
	   v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9]);
    MPI_Barrier(MPI_COMM_WORLD);
  }
}


////////////////////////////////////////////////////////////////////////////////
//                 Helpers for bad fork/free implementations                  //
////////////////////////////////////////////////////////////////////////////////

#if 0
#define fork_init_lock() pthread_mutex_init(&(SlaveInfo.fork_lock),  NULL)
#define fork_lock()      pthread_mutex_lock(&(SlaveInfo.fork_lock))
#define fork_unlock()    pthread_mutex_unlock(&(SlaveInfo.fork_lock))
#else
#define fork_init_lock()
#define fork_lock()
#define fork_unlock()
#endif

static int safe_inflateEnd(z_stream *strm)
{
  int ret;
  fork_lock();
  ret = inflateEnd(strm);
  fork_unlock();
  return ret;
}


static void safe_free(void *p)
{
  fork_lock();
  free(p);
  fork_unlock();
}
  

static void* safe_malloc(size_t sz)
{
  void *p;
  fork_lock();
  p = malloc(sz);
  fork_unlock();
  return p;
}
  


////////////////////////////////////////////////////////////////////////////////
//                                 Exit Code                                  //
////////////////////////////////////////////////////////////////////////////////


static void cleanup();


static void Abort(int arg)
{
  // Make sure our shared memory segments are removed
  cleanup();

  // Abort the MPI job
  MPI_Abort(MPI_COMM_WORLD,arg);
}


////////////////////////////////////////////////////////////////////////////////
//                              SHM Wrapper Code                              //
////////////////////////////////////////////////////////////////////////////////


static void* Create_SHM(char *name, long shmsz, int *fd)
{
  void *shm;
  int   shmfd;
  char  shmname[256];

  snprintf(shmname, 256, "/mcw.%d.%s", getpid(), name);

  // Create the shared memory segment, and then mark for removal.
  // As soon as all attachments are gone, the segment will be
  // destroyed by the OS.
  shmfd = shm_open(shmname, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if( shmfd < 0 ) {
    Vprint(SEV_ERROR,"Failed to make SHM of size %ld: %s. Terminating.\n",shmsz,strerror(errno));
    Abort(1);
  }
  if ((ftruncate(shmfd, shmsz)) != 0) {
    Vprint(SEV_ERROR,"Failed to resize SHM (%d).  Terminating.\n",errno);
    Abort(1);
  }
  shm = mmap(NULL, shmsz, PROT_READ | PROT_WRITE,
             MAP_SHARED /*| MAP_LOCKED | MAP_HUGETLB*/,
             shmfd, 0);

  if( shm == MAP_FAILED) {
    Vprint(SEV_ERROR,"Failed to attach SHM. Terminating.\n");
    Abort(1);
  }
  
  // Return the created SHM
  if( fd ) {
    *fd = shmfd;
  }
  return shm;
}


static void* Create_DBSHM(char *name, long shmsz)
{
  int    fd;
  void  *shm;
  char  shmname[10];

  snprintf(shmname, 10, "%d", file_sizes->nfiles);

  // Create the shared memory segment
  shm = Create_SHM(shmname, shmsz, &fd);  

  // Save some info about the SHM
  if( file_sizes->nfiles < MAX_DB_FILES ) {
    file_sizes->fs[file_sizes->nfiles].shm     = shm;
    file_sizes->fs[file_sizes->nfiles].shmsize = shmsz;
    file_sizes->fs[file_sizes->nfiles].size    = shmsz;
    file_sizes->fs[file_sizes->nfiles].fd      = fd;
    file_is_shm[file_sizes->nfiles]            = 1;
    sprintf(file_sizes->fs[file_sizes->nfiles].name,"%s",name);
    file_sizes->nfiles++;
  } else {
    Vprint(SEV_ERROR,"Too many DB files; increase MAX_DB_FILES. Terminating.\n");
    Abort(1);
  }

  // Return the newly created SHM
  return shm;
}


////////////////////////////////////////////////////////////////////////////////
//                               Master Code                                  //
////////////////////////////////////////////////////////////////////////////////


// Returns a pointer to the start of the next sequence
static seq_data_t Get_SequenceBlocks(int *size, int *blocks, int blks_per_wu)
{
  compressedb_t *cb;
  static int     qndx=0,f1=0,f2=0;
  double         pd;
  int            i,j,sz;
  off_t          off;

  // Make sure the current (next to be returned) is in bounds
  if( qndx >= MasterInfo.nqueries ) {
    // No more queries, return NULL
    (*size)   = 0;
    (*blocks) = 0;
    return NULL;
  }

  // See if there needs to be an adjustment to blocks returned
  pd = ((double)qndx)/(MasterInfo.nqueries-1);
  if( pd < 0.90 ) {
    // 0 - 90
    if( args.wum ) {
      (*blocks) *= args.wum;
    } 
  } else if( pd < 0.95 ) {
    // 90 - 95
    if( args.wum/2 ) {
      (*blocks) *= (args.wum/2);
      if( !f1 ) {
	f1 = 1;
	Vprint(SEV_NRML,"Master switching to smaller WUM: %d\n\n",(args.wum/2));
      }
    }
    if( !f1 ) {
      f1 = 1;
      Vprint(SEV_NRML,"Master switching to smaller WUM: 1\n\n");
    }
  } else {
    // 95 - 100
    if( !f2 ) {
      f2 = 1;
      Vprint(SEV_NRML,"Master switching to smaller WUM: 1\n\n",1);
    }
  }

  // "Aquire" the requested blocks
  for(i=sz=0; (i < (*blocks)) && (qndx < MasterInfo.nqueries); i++,qndx++) {
    for(j=0,off=0; j < blks_per_wu; j++) {
      // Nasty hack to iterate through compressed blocks to build transfer block
      cb = ((void *)MasterInfo.queries[qndx]) + off;
      off += cb->len + sizeof(cb->len);
    }
    sz += off;
  }

  // Return the blocks and metadata
  (*blocks) = i;
  (*size)   = sz;
  return (seq_data_t)MasterInfo.queries[qndx-i];
}


// Opens and maps the input query files
static void Init_Queries(char *fn)
{
  struct stat    statbf;
  compressedb_t *cb;
  int            f, i;
  

  // Open and memory map the input sequence file
  if( (f = open(fn,O_RDONLY)) < 0 ) {
    Vprint(SEV_ERROR,"Master could not open() query file. Terminating.\n");
    Abort(1);
  }
  if( fstat(f, &statbf) < 0 ) {
    close(f);
    Vprint(SEV_ERROR,"Master could not fstat() opened query file. Terminating.\n");
    Abort(1);
  }
  MasterInfo.qmap = mmap(NULL,statbf.st_size,PROT_READ|PROT_WRITE,MAP_PRIVATE,f,0);
  close(f);
  if( MasterInfo.qmap == MAP_FAILED ) {
    Vprint(SEV_ERROR,"Master could not mmap() opened query file. Terminating.\n");
    Abort(1);
  }

  // Build array of pointers to block structs from raw map
  MasterInfo.nqueries = 0;
  MasterInfo.queries  = NULL;
  for(i=0,cb=(compressedb_t*)MasterInfo.qmap;
      cb<(compressedb_t*)(MasterInfo.qmap+statbf.st_size);
      i++,cb=((void*)cb)+cb->len+sizeof(cb->len)) {

    // Only create a new query at the beginning of each "chunk"
    if (i%args.nq_files == 0) {
      // Start of new compressed block
      MasterInfo.nqueries++;
      if( !(MasterInfo.queries=realloc(MasterInfo.queries,MasterInfo.nqueries*sizeof(compressedb_t*))) ) {
        Vprint(SEV_ERROR,"Master failed to realloc() compressed input block array. Terminating.\n");
        Abort(1);
      }
      MasterInfo.queries[MasterInfo.nqueries-1] = cb;
    }
  }

  // Compute a reasonable size for a query block
  Vprint(SEV_NRML,"Compressed input block count: %d\n\n",MasterInfo.nqueries);
}


////////////////////////////////////////////////////////////////////////////////


static void Init_Master()
{
  int i;

  // Break query file into work units
  Init_Queries(args.queryf);

  // Allocate space for master to keep track of slaves
  if( !(MasterInfo.slaves=malloc(MasterInfo.nslaves*sizeof(slave_t))) ) {
    Vprint(SEV_ERROR,"Master failed to allocate slave array. Terminating.\n");
    Abort(1);
  }
  // Allocate space to hold irecv request fields 
  if( !(MasterInfo.mpi_req=malloc(MasterInfo.nslaves*3*sizeof(MPI_Request))) ) {
    Vprint(SEV_ERROR,"Master failed to allocate slave MPI request array. Terminating.\n");
    Abort(1);
  }

  // Init the array
  memset(MasterInfo.slaves,0,MasterInfo.nslaves*sizeof(slave_t));
  for(i=0; i<MasterInfo.nslaves; i++) {
    MasterInfo.slaves[i].rank = i+1;
  }
}


static void Master(int processes, int rank)
{
  masterinfo_t *mi=&MasterInfo; // Just to shorten
  seq_data_t    sequence;
  int           nseq,seqsz,blkseqs,index,i,fp,max_req;
  int           err;
  double        pd,lpd;
  long          st;

  // Record init start time
  st = time(NULL);

  // Initialize the master process
  mi->rank    = rank;
  mi->nprocs  = processes;
  mi->nslaves = processes-1;
  Vprint(SEV_NRML,"Slaves:   %d\t(processes)\n",mi->nslaves);
  Vprint(SEV_NRML,"Workers:  %d\t(threads)\n\n",mi->nslaves*MCW_NCORES);
  Init_Master();

	max_req = ceil((float)mi->nqueries/(mi->nslaves*MCW_NCORES));

  // Wait for all the ranks to fully init
  MPI_Barrier(MPI_COMM_WORLD);

  // Record the init end time
  init_time += time(NULL)-st;
  Vprint(SEV_NRML,"Finished with init in %f seconds.\n\n",init_time);

  // Post a receive work request from each slave
  for(i=0; i<mi->nslaves; i++) {
    err = MPI_Irecv(&(mi->slaves[i].request), sizeof(request_t), MPI_BYTE, mi->slaves[i].rank, 
              TAG_REQUEST, MPI_COMM_WORLD, &(mi->mpi_req[i]) );
    printf("MPI: Master Irecv for slave %d: %d\n", i, err);
  }

  // Hand out work units to requestors while there is work
  fp = 0;
  lpd = pd = 0;
  for(nseq=0; nseq < mi->nqueries; nseq+=blkseqs) {
    // Wait until a slave requests a work unit
    MPI_Waitany(mi->nslaves, mi->mpi_req, &index, MPI_STATUSES_IGNORE);
    // Wait for any previous work unit sends to this slave to complete
    if( mi->slaves[index].sflg ) {
      // These should return immediately.  This slave should not
      // request a new work unit until the send of the old one
      // (and work on it) completed.
      MPI_Wait(&(mi->mpi_req[mi->nslaves+index]),   MPI_STATUS_IGNORE);
      MPI_Wait(&(mi->mpi_req[mi->nslaves*2+index]), MPI_STATUS_IGNORE);
      mi->slaves[index].sflg = 0;
    }
    // We found a slave ready for work.  What is he requesting?
    blkseqs = 0;
    switch( mi->slaves[index].request.type ) {
    case RQ_WU:
      // Slave wants a work unit; send it to him

      // Limit number of work units to a fair amount
      blkseqs = mi->slaves[index].request.count;
      if (blkseqs > max_req) {
	Vprint(SEV_NRML,"Master: Slave %d requested %d units, limiting to %d.", index, blkseqs, max_req);
	//blkseqs = max_req;
      }
      sequence = Get_SequenceBlocks(&seqsz,&blkseqs,args.nq_files);

      // Send work unit (inform of incoming sequence)
      mi->slaves[index].workunit.type   = WU_TYPE_SEQS;
      mi->slaves[index].workunit.len    = seqsz;
      mi->slaves[index].workunit.blk_id = nseq;
      MPI_Isend(&(mi->slaves[index].workunit), sizeof(workunit_t), MPI_BYTE, 
                mi->slaves[index].rank, TAG_WORKUNIT, MPI_COMM_WORLD,
                &(mi->mpi_req[mi->nslaves+index]));
      // Now send the sequence data
      mi->slaves[index].sequence = sequence;
      MPI_Isend(mi->slaves[index].sequence, mi->slaves[index].workunit.len, MPI_BYTE,
                mi->slaves[index].rank, TAG_SEQDATA, MPI_COMM_WORLD,
                &(mi->mpi_req[mi->nslaves*2+index]));
      // Record that a send is in flight to this slave
      mi->slaves[index].sflg = 1;
      // And post a receive for another work unit request from this slave
      err = MPI_Irecv(&(mi->slaves[index].request), sizeof(request_t), MPI_BYTE,
                mi->slaves[index].rank, TAG_REQUEST, MPI_COMM_WORLD, 
                &(mi->mpi_req[index]));
      printf("MPI: Master Irecv for slave %d: %d\n", index, err);
      break;
    default:
      // Unknown request type
      Vprint(SEV_ERROR,"Master: Slave %d made unknown request type %d.  Terminating.\n",
             index,mi->slaves[index].request.type);
      Abort(1);
    }
    // Progress update print
    pd = (((double)nseq) / mi->nqueries) * 100.0;
    if( !fp || ((pd-lpd) >= 1.0) ) {
      // We made at least 1% progress since last update or is first
      fp = 1;
      Vprint(SEV_NRML,"Dispached workunits: %3.0lf%c\n\n",pd,'%');
      lpd = pd;
    }
  }

  // Be a little verbose
  Vprint(SEV_NRML,"Master is done handing out work units.\n");
  Vprint(SEV_NRML,"Master responding WU_TYPE_EXIT to requests.\n\n");

  // We are out of work units.
  MPI_Waitall(mi->nslaves, mi->mpi_req, MPI_STATUSES_IGNORE);
  // Be sure all WU sends to slaves are done; this should return immediately,
  // as slaves should not be ready for new WUs unless the sends of the old
  // work units completed.
  for(i=0; i<mi->nslaves; i++) {
    if( mi->slaves[i].sflg ) {
      MPI_Wait(&(mi->mpi_req[mi->nslaves+i]),   MPI_STATUS_IGNORE);
      MPI_Wait(&(mi->mpi_req[mi->nslaves*2+i]), MPI_STATUS_IGNORE);
      mi->slaves[index].sflg = 0;
    }
  }

  // Tell slaves to exit
  for(i=0; i<mi->nslaves; i++) {
    mi->slaves[i].workunit.type = WU_TYPE_EXIT;
    mi->slaves[i].workunit.len  = 0;
    MPI_Isend(&(mi->slaves[i].workunit), sizeof(workunit_t), MPI_BYTE, 
              mi->slaves[i].rank, TAG_WORKUNIT, MPI_COMM_WORLD,
              &(mi->mpi_req[index]));
  }
  // Wait for all EXIT flag sends to slaves to complete
  MPI_Waitall(mi->nslaves, mi->mpi_req, MPI_STATUSES_IGNORE);

  // !!av: Gating hack
  i = mi->nprocs / ConcurrentWriters;
  if( mi->nprocs%ConcurrentWriters ) {
    i++;
  }
  while( i-- ) {
    MPI_Barrier(MPI_COMM_WORLD);
  }

  // Wait for everyone to report that they are exiting
  MPI_Barrier(MPI_COMM_WORLD);

  // Report times (yay! this call is all zeros!)
  Report_Timings(0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0);

  // Report that shutdown (pretty much all, but I still call it
  // "initial") is complete.
  Vprint(SEV_NRML,"Master reports initial shutdown complete.\n\n");

}


static void Master_Flush()
{
	/*
  int i;
  char d = MASTERCMD_EXIT;

  MPI_Request req;
  
  // Supposedly the multi-thread support works for point-to-point
  // Unsure if it works for Bcast, so loop for safety.
  for(i=0; i<MasterInfo.nslaves; i++) {
    MPI_Isend(&d, sizeof(char), MPI_BYTE, MasterInfo.slaves[i].rank,
              TAG_MASTERCMD, MPI_COMM_WORLD, &req);
  }
	*/

  // FIXME: Hack, arbitrary timeout
  sleep(60);
}


////////////////////////////////////////////////////////////////////////////////
//                               Worker Code                                  //
////////////////////////////////////////////////////////////////////////////////


static int Worker_IsEnoughSpaceAvail()
{
  int n;

  for( n=0; n<SlaveInfo.nout_files; ++n ) {
    if( RESULTBUFF_GBL_SIZE - resultbuff[n].size < RESULTBUFF_SIZE ) {
      return 0;
    }
  }
  return 1;
}

static int Writer_IsDataAvailable()
{
  int n;

  for( n=0; n<SlaveInfo.nout_files; ++n ) {
    if( resultbuff[n].size) {
      return 1;
    }
  }
  return 0;
}


static void Worker_WriteResults(int *rndxs, int wid, int bid)
{
  filesize_t     *fs;
  int n;

  // Check result buffers for data
  for( n=0; n<SlaveInfo.nout_files; ++n ) {
    if( file_sizes->fs[rndxs[n]].size ) {
      break;
    }
  }
  // Skip if no data
  if( n==SlaveInfo.nout_files ) {
    return;
  }

  // Lock and wait for "not full" status
  Vprint(SEV_DEBUG,"Slave %d Worker %d about to write results to buffer.\n",
         SlaveInfo.rank,wid);
  pthread_mutex_lock(&(SlaveInfo.resultb_lock));
  
  // Wait while remaining space is less then worst possible local result buffer
  while( !Worker_IsEnoughSpaceAvail() ) {
    Vprint(SEV_DEBUG,"Slave %d Worker %d waiting for room to write results to buffer.\n",
	   SlaveInfo.rank,wid);
    pthread_cond_wait(&(SlaveInfo.resultb_nfull), &(SlaveInfo.resultb_lock));
  }

  // Write into result shms
  for( n=0; n<SlaveInfo.nout_files; ++n ) {
    fs = &(file_sizes->fs[rndxs[n]]);
    // Data
    memcpy((void*)(resultbuff[n].buff+resultbuff[n].size), fs->shm, fs->size);
    resultbuff[n].size += fs->size;
    // Block info FIXME: This is WRONG, only need BID history for slave, not per-buffer!
    resultbuff[n].bids[resultbuff[n].count]   = bid;
    resultbuff[n].bsizes[resultbuff[n].count] = fs->size;
    resultbuff[n].count++;
  }

  // Signal that the buffer is not empty
  pthread_cond_signal(&(SlaveInfo.resultb_nempty));

  // Unlock
  pthread_mutex_unlock(&(SlaveInfo.resultb_lock));
  Vprint(SEV_DEBUG,"Slave %d Worker %d done writing to result buffer.\n",
         SlaveInfo.rank,wid);
}


static float Worker_ChildIO(int rank, int pid, int wid, int bid, int *qndxs, int *rndxs)
{
  struct timeval  st,et;
  int             status,w;
  float           io_time=0.0f;

  // Wait for the child to finish
  if( (w=waitpid(pid,&status,0)) < 0 ) {
    // Wait failed for some reason
    // There was an error with the child DB process
    Vprint(SEV_ERROR,"Worker's wait on child failed.  Terminating.\n");
    Abort(1);
  }
  // Check child exit status
  if( w ) {
    if( WIFEXITED(status) && !WEXITSTATUS(status) ) {
      // The slave's child seemes to have finished correctly
      Vprint(SEV_DEBUG,"Slave %d Worker %d Child exited normally.\n",
	     rank,wid);		
    } else {
      // There was an error with the child DB process
      Vprint(SEV_ERROR,"Worker's child exited abnormally: %d.\n",WEXITSTATUS(status));
      if( WIFSIGNALED(status) ) {
	Vprint(SEV_ERROR,"Worker's child killed by signal: %d.\n",WTERMSIG(status));
      }
    } 
  }

  // The child process is gone:  Write any results to our node's buffer.
  gettimeofday(&st, NULL);
  Worker_WriteResults(rndxs, wid, bid);
  gettimeofday(&et, NULL);
  io_time += ((et.tv_sec*1000000+et.tv_usec) - 
             (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;

  // Return the total IO output time
  return io_time;
}


static void Worker_FreeArgv(char **argv)
{
  char **p;

  // Free all args, then free array of args.
  for(p=argv; *p; p++) {
    free(*p);
  }
  free(argv);
}

static char **Worker_BuildArgv(int rank, int wid)
{
  char **argv,*m,*w,*saveptr=NULL;
  int    na;

  // Put program name at the start of argv list
  if( !(argv=malloc(sizeof(char*))) ) {
    Vprint(SEV_ERROR, "Slave %d Worker %d Failed to create child's argv list.\n",rank,wid);
    Abort(1);
  }
  argv[0] = strdup(EXE_BASE);
  na = 1;

  // Now add the args from the env var mode / line
  m = strdup(args.mode);
  w = strtok_r(m, " \t\n", &saveptr);
  while( w ) {
    // Make room in array for another arg
    if( !(argv=realloc(argv,(na+1)*sizeof(char*))) ) {
      Vprint(SEV_ERROR, "Slave %d Worker %d Failed to grow child's argv list.\n",rank,wid);
      Abort(1);
    }
    argv[na] = w;
    na++;

    // Advance to next arg in list
    w = strtok_r(NULL, " \t\n", &saveptr);
  }

  // Add a null pointer to the end
  if( !(argv=realloc(argv,(na+1)*sizeof(char*))) ) {
    Vprint(SEV_ERROR, "Slave %d Worker %d Failed to grow child's argv list.\n",rank,wid);
    Abort(1);
  }
  argv[na] = NULL;

  // Return the created args list
  return argv;
}


#if 0
static void Worker_Child_MapFDs(int rank, int wid)
{
  static int  devnull=-1;
  char       *m,*w,*saveptr=NULL;
  int         from,to;


  // Get access to /dev/null if needed
  if( devnull == -1 ) {
    if( (devnull=open("/dev/null", O_RDWR)) < 0 ) {
      fprintf(stderr,"Slave %d Worker %d's Child failed to open(\"/dev/null\").\n",rank,wid);
      exit(1);
    }
  }

  // Now apply the mappings
  m = strdup(args.dup2);
  for( w = strtok_r(m, ":", &saveptr); w; w = strtok_r(NULL, " ", &saveptr) ) {

    // Parse this one mapping
    if( sscanf(w,"%d,%d",&from,&to) != 2 ) {
      Vprint(SEV_ERROR, "Slave %d Worker %d's Child failed to parse fd mapping work.\n",rank,wid);
      exit(1);
    }
    if( from == -1 ) { from = devnull; }
    if( to   == -1 ) { to   = devnull; }
    // Perform the mapping
    if( dup2(to, from) < 0 ) {
      fprintf(stderr,"Slave %d Worker %d's Child failed to dup2(%d,%d).\n",rank,wid,to,from);
      exit(1);
    }
  }
  safe_free(m);
}
#endif


static float Worker_SearchDB(int rank, int procs, int wid, char **argv, int bid, int *qndxs, int *rndxs)
{
  char  name[256],exe_name[256];
  int   pid,node,nodes,loadstride;
  float io_time=0.0f;

  // These will be needed later
  node       = rank;
  nodes      = procs;
  loadstride = nodes/args.ndbs;

  // Lock until child process signals us with SIGUSR1
  fork_lock();

  // Build a name for the exe
  sprintf(exe_name,"./%s",EXE_BASE);

  if( (pid=fork()) > 0 ) {
    // This is the MPI slave process (parent)
    Vprint(SEV_DEBUG, "Slave %d Worker %d's child's pid: %d.\n",SlaveInfo.rank,wid,pid);
    // Wait for child process to start
    // sleep(2);
    fork_unlock();
    // Wait for child to finish; handle its IO
    io_time = Worker_ChildIO(rank,pid,wid,bid,qndxs,rndxs);
  } else if( !pid ) {
    // This is the Child process
    //PG Worker_Child_MapFDs(rank,wid);
    
    // Setup the environment for the child process
    int nenv, i;
    char **env;
    for (nenv=0, env=environ; *env; nenv++, env++);
    
    // FIXME: LEAK!!
    char **new_environ = malloc(sizeof(char*) * (nenv+4));
    // Copy original values
    for (i=0; i < nenv; i++) {
      new_environ[i] = environ[i];
    }
    // Add our own
    asprintf(&(new_environ[i++]), "MCW_FI_SHM_FD=%d", file_sizes_fd);
    asprintf(&(new_environ[i++]), "MCW_DB_FULL_PATH=%s/%s%d/%s", 
	     args.db_path,args.db_prefix,node/loadstride,args.db_prefix);
    asprintf(&(new_environ[i++]), "MCW_WID=%d", wid);
    // Null terminate
    new_environ[i] = NULL;

    // Run the DB search
    if( execve(exe_name, argv, new_environ) < 0 ) {
      Vprint(SEV_ERROR,"Worker's child failed to exec DB.\n");
      perror(MCW_BIN);
      // FIXME: is this the child process? We should kill(getppid(),...)
      fork_unlock();
    }
    // FIXME: Is this code unreachable?
    Vprint(SEV_ERROR,"Worker's child failed to exec DB! (unreachable?)\n");
    perror(MCW_BIN);
    Abort(0);
  } else {
    // fork() returned an error code
    Vprint(SEV_ERROR,"Worker failed to start DB search.\n");
    perror(MCW_BIN);
    fork_unlock();
  }

  // Return the time it took to do IO.
  return io_time;
}


// TODO: PG: Move to libzutils
#define CHUNK 16384
int zinf_memcpy(unsigned char *dest, compressedb_t *cb, size_t *dcsz)
{
  z_stream       strm;
  unsigned char  in[CHUNK],out[CHUNK],*odest=dest;
  unsigned       have;
  long           bsz;
  int            ret;

  // Init inflate state
  strm.zalloc   = Z_NULL;
  strm.zfree    = Z_NULL;
  strm.opaque   = Z_NULL;
  strm.next_in  = Z_NULL;
  strm.avail_in = 0;
  ret = inflateInit(&strm);
  if( ret != Z_OK ) {
    fprintf(stderr, "inflate: Could not initialize!\n");
    return ret;
  }
  
  // Decompress until we have processed bsz bytes
  bsz = cb->len;
  do {
    if( !(strm.avail_in=((bsz>=CHUNK)?(CHUNK):(bsz))) ) {
      safe_inflateEnd(&strm);
      fprintf(stderr, "inflate: Could not read data chunk\n");
      return Z_DATA_ERROR;
    }
    memcpy(in, &(cb->data)+cb->len-bsz, strm.avail_in);
    strm.next_in = in;
    bsz -= strm.avail_in;
    // run inflate() on input until output buffer not full
    do {
      strm.avail_out = CHUNK;
      strm.next_out = out;
      ret = inflate(&strm, Z_NO_FLUSH);
      switch( ret ) {
      case Z_BUF_ERROR:
      case Z_NEED_DICT:
      case Z_DATA_ERROR:
      case Z_MEM_ERROR:
        fprintf(stderr, "inflate: Error!\n");
        safe_inflateEnd(&strm);
      case Z_STREAM_ERROR:
        fprintf(stderr, "inflate: Stream Error!\n");
        return ret;
      }
      have = CHUNK - strm.avail_out;
      memcpy(dest,out,have);
      dest += have;
    } while( strm.avail_out == 0 );
    // Done when inflate() says it's done
  } while( ret != Z_STREAM_END );

  // clean up
  safe_inflateEnd(&strm);

  // If the stream ended before using all the data
  // in the block, return error.
  if( bsz ) {
    fprintf(stderr, "inflate: Data Error!\n");
    return Z_DATA_ERROR;
  }
  
  // Return success
  (*dcsz) = dest-odest;
  return Z_OK;
}
  

static void GrowSeqData(int new_len)
{
  // Calculate a reasonable new size
  if( new_len > SlaveInfo.seq_data_len ) {
    if( new_len > (SlaveInfo.seq_data_len*2) ) {
      SlaveInfo.seq_data_len = new_len;
    } else {
      SlaveInfo.seq_data_len *= 2;
    }
  }

  // Realloc space for the sequence data
  if( !(SlaveInfo.seq_data = realloc(SlaveInfo.seq_data,SlaveInfo.seq_data_len*sizeof(char))) ) {
    Vprint(SEV_ERROR,"Slave failed to grow sequence array. Terminating.\n");
    Abort(1);
  }
}


static int find_file_by_name(char *n)
{
  int i;

  for (i = 0; i < file_sizes->nfiles; ++i) {
    if (!strcmp(n, file_sizes->fs[i].name)) {
      return i;
    }
  }
  return -1;
}


static void* Worker(void *arg)
{
  int             wid = (int)((long)arg);
  struct timeval  st,et,tv;
  compressedb_t  *cb;
  slaveinfo_t    *si=&SlaveInfo;
  request_t      *request;
  workunit_t     *workunit;
  size_t          dcsz;
  float           t_vi,t_vo,t_ic;
  int             f,q[args.nq_files],r[SlaveInfo.nout_files],done=0;
  long            ib,idb;
  char            name[256];
  char          **argv;

  // Find the in/out SHMs for this worker
  // Inputs
  for (f = 0; f < args.nq_files; ++f) {
    sprintf(name, ":MCW:W%d:IN%d", wid, f);
    if ((q[f] = find_file_by_name(name)) < 0) {
      Vprint(SEV_ERROR,"Slave %d Worker %d failed to find q I/O SHM. (Pseudo filename: %s)\n",SlaveInfo.rank,wid,name);
      Abort(1);
    }
  }
  // Outputs
  for (f = 0; f < SlaveInfo.nout_files; ++f) {
    sprintf(name,":MCW:W%d:OUT%d",wid,f);
    if ((r[f] = find_file_by_name(name)) < 0) {
      Vprint(SEV_ERROR,"Slave %d Worker %d failed to find r I/O SHM. (Pseudo filename: %s)\n",SlaveInfo.rank,wid,name);
      Abort(1);
    }
  }

  // Arguments for execv
  argv = Worker_BuildArgv(si->rank,wid);

  // Done with init, start processing loop
  while( !done ) {
    // Time the request + receive
    t_vi = t_vo = t_ic = 0.0f;
    ib = idb = 0;
    gettimeofday(&st, NULL);

    Vprint(SEV_TIMING,"[TIMING] Slave %d, Worker %d Requesting at %d.%06d.\n",
                 SlaveInfo.rank, wid, st.tv_sec, st.tv_usec);

    // Get a new request entry object
    Vprint(SEV_DEBUG,"Slave %d Worker %d getting request object.\n",SlaveInfo.rank,wid);
    request = tscq_entry_new(si->rq);

    // Fill it in
    memset(request,0,sizeof(request_t));
    request->type    = RQ_WU;
    request->count   = 1;

    // "Send" the request up the command chain
    Vprint(SEV_DEBUG,"Slave %d Worker %d asking for work.\n",SlaveInfo.rank,wid);
    tscq_entry_put(si->rq,request);
    
    // Get the work unit we requested
    Vprint(SEV_DEBUG,"Slave %d Worker %d waiting for work.\n",SlaveInfo.rank,wid);
    workunit = tscq_entry_get(si->wq);
    
    // Find total request + receive time
    gettimeofday(&et, NULL);
    t_vi += ((et.tv_sec*1000000+et.tv_usec) - 
	     (st.tv_sec*1000000+st.tv_usec)) / 1000000.0f;

    // Process the work unit
    switch( workunit->type ) {
    case WU_TYPE_EXIT:
      // Master has told us we are done
      done = 1;
      break;
    case WU_TYPE_SEQF:
      // Now that we have sequence data, search the DB with it
      Vprint(SEV_DEBUG,"Slave %d Worker %d starting search.\n",SlaveInfo.rank,wid);
      // Inflate the zlib compressed block(s) into the SHM
      gettimeofday(&st, NULL);
      Vprint(SEV_TIMING,"[TIMING] Slave %d, Worker %d inflating at %d.%06d.\n",
             SlaveInfo.rank, wid, st.tv_sec, st.tv_usec);

      // Map each input in multi-input mode
      for( f=0, cb=(compressedb_t*)(workunit->data);
           f<args.nq_files;
           ++f, cb=((void*)cb)+cb->len+sizeof(cb->len) ) {

        if( zinf_memcpy((unsigned char*)file_sizes->fs[q[f]].shm,cb,&dcsz) != Z_OK ) {
          Vprint(SEV_ERROR,"Slave %d Worker %d zlib inflate failed. Terminating.\n",SlaveInfo.rank,wid);
          si->worker_error = wid;
          return NULL;
        }
	Vprint(SEV_TIMING,"[TIMING] Slave %d, Worker %d File %d CSZ: %d DSZ: %d.\n",
	       SlaveInfo.rank, wid, f, cb->len, dcsz);

	file_sizes->fs[q[f]].size = dcsz;
        idb += dcsz;
      }
      ib = workunit->len;

      // Reset lengths on outputs
      for( f=0; f<SlaveInfo.nout_files; ++f ) {
	file_sizes->fs[r[f]].size = 0;
      }

      gettimeofday(&et, NULL);
      Vprint(SEV_TIMING,"[TIMING] Slave %d, Worker %d beginning search at %d.%06d.\n",
             SlaveInfo.rank, wid, et.tv_sec, et.tv_usec);
      t_ic += ((et.tv_sec*1000000+et.tv_usec) - 
              (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;

      if( dcsz > QUERYBUFF_SIZE ) {
        Vprint(SEV_ERROR,"Compressed block of size %ld is too large for query buffer maximum of %ld.\n",
            dcsz, QUERYBUFF_SIZE);
        si->worker_error = wid;
        return NULL;
      }

      t_vo = Worker_SearchDB(si->rank, si->nprocs, wid, argv, workunit->blk_id, q, r);
      // The producer of the work unit malloced this, so we need to free it.
      safe_free(workunit->data);
      gettimeofday(&tv, NULL);
      Vprint(SEV_TIMING,"[TIMING] Slave %d, Worker %d done with search at %d.%06d.\n",
             SlaveInfo.rank, wid, tv.tv_sec, tv.tv_usec);
      //Vprint(SEV_DEBUG,"Slave %d Worker %d done with search.\n",SlaveInfo.rank,wid);
      break;
    default:
      Vprint(SEV_ERROR,"Worker received unknown work unit type. Terminating.\n");
      si->worker_error = wid;
      return NULL;
    }
    
    // Release the work unit we are done with
    tscq_entry_free(si->wq,workunit);

    // Add time spent into node count
    // TODO: Bottleneck... just accumulate per-worker then add into slave on end of entire job
    pthread_mutex_lock(&(SlaveInfo.time_lock));
    SlaveInfo.t_ic += t_ic;
    SlaveInfo.t_vi += t_vi;
    SlaveInfo.t_vo += t_vo;
    SlaveInfo.b_i  += ib;
    SlaveInfo.b_id += idb;
    pthread_mutex_unlock(&(SlaveInfo.time_lock));
  }

  Vprint(SEV_DEBUG,"Slave %d Worker %d done; exiting.\n",SlaveInfo.rank,wid);
  //Fixme: for real
  //Worker_FreeArgv(argv);
  return NULL;
}


void Start_Workers()
{
  pthread_attr_t  attr;
  int             i;

  // Setup queues with which to interact with worker threads
  SlaveInfo.rq = tscq_new(1024*MCW_NCORES,sizeof(request_t));
  SlaveInfo.wq = tscq_new(1024*MCW_NCORES,sizeof(workunit_t));

  // Setup worker thread properties
  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

  // Start worker threads
  for(i=0; i<MCW_NCORES; i++) {
    int err = pthread_create(&(SlaveInfo.workers[i]), &attr, Worker, (void*)((long)i));
    if (err) {
      Vprint(SEV_ERROR,"Slave failed to start worker thread. %s. Terminating.\n", strerror(err));
      Abort(1);
    }
  }

}


////////////////////////////////////////////////////////////////////////////////
//                                Slave Code                                  //
////////////////////////////////////////////////////////////////////////////////


#define ZCHUNK 16384


// Compresses "sz" bytes from "source" to "dest", compressing
// the data with zlib compression strength "level".
//
// Special thanks to Mark Adler for providing the non-copyrighted
// public domain example program "zpipe.c", from which this function
// is based (Version 1.4  December 11th, 2005).
static long ZCompress(void *source, int sz, void *dest, int level)
{
  z_stream       strm;
  unsigned char  in[ZCHUNK],out[ZCHUNK];
  int            blk,tw=sz;
  int            rv,flush;
  unsigned       ndata;
  long           len=0;
  
  // Init zlib state
  strm.zalloc = Z_NULL;
  strm.zfree  = Z_NULL;
  strm.opaque = Z_NULL;
  rv = deflateInit(&strm, level);
  if( rv != Z_OK ) {
    Vprint(SEV_ERROR,"Slave %d's Writer failed to init zlib.  Terminating.\n",
           SlaveInfo.rank);
    result_thread_error = 1;
    pthread_exit(NULL);
  }

  // Compress while there is still data to write
  do {
    // Setup input
    blk = ((tw < ZCHUNK)?(tw):(ZCHUNK));
    memcpy(in, source+(sz-tw), blk);
    strm.avail_in = blk;
    strm.next_in  = in;
    flush = ((!(tw-blk))?(Z_FINISH):(Z_NO_FLUSH));
    do {
      // Setup output and compress
      strm.avail_out = ZCHUNK;
      strm.next_out  = out;
      rv = deflate(&strm, flush);
      if( rv == Z_STREAM_ERROR ) {
        Vprint(SEV_ERROR,"Slave %d's Writer failed to compress output block.  Terminating.\n",
               SlaveInfo.rank);
        result_thread_error = 1;
        pthread_exit(NULL);
      }
      // Copy compressed data to destination
      ndata = ZCHUNK - strm.avail_out;
      memcpy(dest+sizeof(len)+len,out,ndata);
      len += ndata;
    } while( strm.avail_out == 0 );
    // Sanity check
    if( strm.avail_in != 0 ) {
      Vprint(SEV_ERROR,"Slave %d's Writer did not fully compress block.  Terminating.\n",
             SlaveInfo.rank);
      result_thread_error = 1;
      pthread_exit(NULL);
    }
    // Update "to write" count
    tw -= blk;
  } while( flush != Z_FINISH );

  // Another sanity check
  if( rv != Z_STREAM_END ) {
    Vprint(SEV_ERROR,"Slave %d's Writer didn't finish compression properly.  Terminating.\n",
           SlaveInfo.rank);
    result_thread_error = 1;
    pthread_exit(NULL);
  }

  // Cleanup
  deflateEnd(&strm);

  // Now that the compressed data is coppied, copy the size of the compressed block to
  // the front of the block.  Then return the total size including the size field.
  return (*((long*)dest) = len) + sizeof(len);
}


static size_t Write(int fd, void *buf, size_t count)
{
  size_t tw,w;

  for(tw=count; tw; tw-=w) {
    w = write(fd, buf+(count-tw), tw);
    if( w == -1 ) {
      // Error writing, return -1
      perror("Write()");
      Vprint(SEV_ERROR,"write(%d,0x%X[0x%X],%lu) failed with %lu bytes written of %lu bytes total.\n",
             fd,buf,buf+(count-tw),count-tw,count);
      return -1;
    }
  }
  // Success, return count
  return count;
}


static uint32_t
CompressToBuffer(char *cbuff, resultbuff_t *ucbuff)
{
  // Prefix with:
  // BlkLen u32 | Chunk Type u8 | Chunk Len u32 | Chunk Data | Next Chunk ...
  // Chunks:
  // B: Block Info:
  //    Block Count u16 | Block Id u32 | Block Size u32 | Next Block ...
  //   
  // C: Check Sum:
  //    Checksum Type u8 | Sum
  //    c: crc32  
  //
  // D: Data:
  //    Data
  //
  // I | Cnt | BlkId0... | BlkSize... |
  //
  // application/blast+xml application/blast+text
  // chemical/seq-aa-fasta, chemical/seq-na-fasta 
  // chemical/x-mol2

  uint32_t header_size = sizeof(uint32_t) + sizeof(blockcnt_t) + (ucbuff->count * (sizeof(blockid_t) + sizeof(uint32_t)));
  uint32_t data_size   = ZCompress((void*)ucbuff->buff, ucbuff->size, ((void*)cbuff)+header_size, Z_DEFAULT_COMPRESSION);
  uint32_t block_size  = header_size + data_size;

  char *d = cbuff;
  
  // Write block size
  memcpy(d, &block_size, sizeof(uint32_t));
  d += sizeof(uint32_t);
  // Write count
  memcpy(d, (const char*)(&(ucbuff->count)), sizeof(blockcnt_t));
  d += sizeof(blockcnt_t);
  // Write block information
  memcpy(d, &(ucbuff->bids), ucbuff->count * sizeof(blockid_t));
  d += ucbuff->count * sizeof(blockid_t);
  memcpy(d, &(ucbuff->bsizes), ucbuff->count * sizeof(uint32_t));
  d += ucbuff->count * sizeof(uint32_t);

  ucbuff->size = ucbuff->count = 0;

  return block_size;
}


void* ResultWriter(void *arg)
{
  UNUSED(arg);
  struct timeval st,et;
  resultbuff_t  *ucbuff;
  char          *cbuff[SlaveInfo.nout_files];
  uint32_t       nc[SlaveInfo.nout_files];

  long           bw, bc;
  float          writet,compt=0.0f;
  int            i;
  int            f[SlaveInfo.nout_files];
  
  // Allocate space to hold the "double" of the double-buffer
  if( !(ucbuff=malloc(SlaveInfo.nout_files * sizeof(resultbuff_t))) ) {
    Vprint(SEV_ERROR,"Slave %d's Writer failed to allocate second buffer.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    return NULL;
  }

  for( i = 0; i < SlaveInfo.nout_files; ++i ) {
    // Allocate space to hold the compressed output buffer
    if( !(cbuff[i]=malloc(RESULTBUFF_GBL_SIZE*sizeof(char))) ) {
      Vprint(SEV_ERROR,"Slave %d's Writer failed to allocate compressed buffer.  Terminating.\n",
	     SlaveInfo.rank);
      result_thread_error = 1;
      return NULL;
    }

    // Setup/Open output file 
    if( (f[i]=open(SlaveInfo.out_files[i], O_CREAT|O_EXCL|O_WRONLY)) == -1) {
      Vprint(SEV_ERROR,"Slave %d's Writer failed to open result file '%s'.  Terminating.\n",
	     SlaveInfo.out_files[i], SlaveInfo.rank);
      result_thread_error = 1;
      return NULL;
    }

    // Reset counters
    ucbuff[i].size = ucbuff[i].count = nc[i] = 0;
  }

  // !!av: Gating hack
  Cbuff  = malloc(SlaveInfo.nout_files*sizeof(char*));
  Fd     = malloc(SlaveInfo.nout_files*sizeof(int));
  Nc     = malloc(SlaveInfo.nout_files*sizeof(uint32_t));
  if( !(Cbuff && Fd && Nc) ) {
    Vprint(SEV_ERROR,"Slave %d's Writer failed to allocate gating-hack storage.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    return NULL;
  }
  memcpy(Cbuff,  cbuff,  sizeof(cbuff));
  memcpy(Fd,     f,      sizeof(f));
  memcpy(Nc,     nc,     sizeof(nc));


  // Repeat until our controlling slave is done
  while( !SlaveInfo.done ) {
    // Lock
    pthread_mutex_lock(&(SlaveInfo.resultb_lock));
    // Wait for "not empty" status
    while( !Writer_IsDataAvailable() && !SlaveInfo.done ) {
      Vprint(SEV_DEBUG,"Slave %d's Writer waiting for data.\n",SlaveInfo.rank);
      pthread_cond_wait(&(SlaveInfo.resultb_nempty), &(SlaveInfo.resultb_lock));
    }
    compt = writet = 0.0f;
    bw = bc = 0;

    for( i=0; i<SlaveInfo.nout_files; ++i ) {

      // If there is not enough room, perform a flush of ucbuff first
      if( RESULTBUFF_GBL_SIZE <= (ucbuff[i].size + resultbuff[i].size) ) {
	gettimeofday(&st, NULL);
	bc    += ucbuff[i].size;
	nc[i] += CompressToBuffer(cbuff[i]+nc[i], &(ucbuff[i]));
	gettimeofday(&et, NULL);
	compt += ((et.tv_sec*1000000+et.tv_usec) -
		 (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
      }
    
      // Copy the data from the global buffer to our local buffer
      memcpy((char *)(ucbuff[i].buff + ucbuff[i].size),
	     (const char *)resultbuff[i].buff,
	     resultbuff[i].size);
      memcpy((char *)(ucbuff[i].bids + ucbuff[i].count),
	     (const char *)resultbuff[i].bids,
	     resultbuff[i].count * sizeof(blockid_t));
      memcpy((char *)(ucbuff[i].bsizes + ucbuff[i].count),
	     (const char *)resultbuff[i].bsizes,
	     resultbuff[i].count * sizeof(uint32_t));

      ucbuff[i].size  += resultbuff[i].size;
      ucbuff[i].count += resultbuff[i].count;

      // Now that we have a local copy, record that the global
      // buffer is no longer full
      resultbuff[i].size  = 0;
      resultbuff[i].count = 0;
    }

    // Done with global buffers, release the lock and
    // tell workers to continue
    pthread_cond_signal(&(SlaveInfo.resultb_nfull));
    pthread_mutex_unlock(&(SlaveInfo.resultb_lock));

    for( i=0; i<SlaveInfo.nout_files; ++i ) {
      // Flush uncompressed buffer if at least half full
      if( ucbuff[i].size >= (RESULTBUFF_GBL_SIZE/2) ) {
	gettimeofday(&st, NULL);
	bc    += ucbuff[i].size;
	nc[i] += CompressToBuffer(cbuff[i]+nc[i], &(ucbuff[i]));
	gettimeofday(&et, NULL);
	compt += ((et.tv_sec*1000000+et.tv_usec) -
		 (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
      }

      // Flush compressed buffer if at least half full
      if( nc[i] >= (RESULTBUFF_GBL_SIZE/2) ) {
	// Perform the actual write to the filesystem
	Vprint(SEV_DEBUG,"Slave %d's Writer writing %ld bytes.\n",SlaveInfo.rank,nc[i]);
	gettimeofday(&st, NULL);
	fprintf(stderr, "Writing at %s:%s:%d\n", __FILE__, __func__, __LINE__);
	if( Write(f[i],(void*)cbuff[i],nc[i]) != nc[i] ) {
	  Vprint(SEV_ERROR,"Slave %d's Writer failed to write to result file.  Terminating.\n",
		 SlaveInfo.rank);
	  result_thread_error = 1;
	  return NULL;
	}
	bw += nc[i];
	nc[i] = 0;
	gettimeofday(&et, NULL);
	writet += ((et.tv_sec*1000000+et.tv_usec) -
		  (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
      }
    }

    // Record timings
    pthread_mutex_lock(&(SlaveInfo.time_lock));
    SlaveInfo.t_o  += writet;
    SlaveInfo.t_oc += compt;
    SlaveInfo.b_od += bc;
    SlaveInfo.b_o  += bw;
    pthread_mutex_unlock(&(SlaveInfo.time_lock));
  }

  // Our parent slave is done; flush and exit
  writet = compt = 0.0f;
  bc = bw = 0;
  for( i=0; i<SlaveInfo.nout_files; ++i ) {
    if( ucbuff[i].size ) {
      gettimeofday(&st, NULL);
      bc    += ucbuff[i].size;
      nc[i] += CompressToBuffer(cbuff[i]+nc[i], &(ucbuff[i]));
      gettimeofday(&et, NULL);
      compt += ((et.tv_sec*1000000+et.tv_usec) -
	       (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
    }
    if( nc[i] ) {
      Vprint(SEV_DEBUG,"Slave %d's Writer writing %ld bytes.\n",SlaveInfo.rank,nc[i]);
      gettimeofday(&st, NULL);
      // !!av: Gating hack
      Nc[i] = nc[i];
      //if( Write(f,(void*)cbuff,nc) != nc ) {
      //  Vprint(SEV_ERROR,"Slave %d's Writer failed to write to result file.  Terminating.\n",
      //         SlaveInfo.rank);
      //  result_thread_error = 1;
      //  return NULL;
      //}
      bw += nc[i];
      nc[i] = 0;
      gettimeofday(&et, NULL);
      writet += ((et.tv_sec*1000000+et.tv_usec) -
	        (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
    }

    // !!av: gating hack
    // close(f[i]);
	
    // Don't need write exclusion anymore.  Make readable.
    fchmod(f[i], S_IRUSR | S_IWUSR | S_IRGRP);
  }

  // Record timings
  pthread_mutex_lock(&(SlaveInfo.time_lock));
  SlaveInfo.t_o  += writet;
  SlaveInfo.t_oc += compt;
  SlaveInfo.b_od += bc;
  SlaveInfo.b_o  += bw;
  pthread_mutex_unlock(&(SlaveInfo.time_lock));      

  // Exit as lock should not be held here; return value is ignored
  Vprint(SEV_DEBUG,"Slave %d's Writer done; exiting.\n", SlaveInfo.rank);
  return NULL;
}




// Create the SHM (and synchronization) used to managed the shared global result buffer.
// TODO: Pass in num-buffers instead of SlaveInfo?
static void CreateResultBuffers()
{
  pthread_attr_t attr;

  // Create and init the result buffer SHM
  if( !(resultbuff=malloc(SlaveInfo.nout_files * sizeof(resultbuff_t))) ) {
    Vprint(SEV_ERROR, "Failed to allocate shared result buffer. Terminating.\n");
    Abort(1);
  }
  // FIXME: PG: We should be able to only zero out the headers
  memset(resultbuff, 0, SlaveInfo.nout_files * sizeof(resultbuff_t));
  
  // Initialize result writer synchronization objects
  pthread_mutex_init(&(SlaveInfo.resultb_lock),  NULL);
  pthread_cond_init(&(SlaveInfo.resultb_nfull),  NULL);
  pthread_cond_init(&(SlaveInfo.resultb_nempty), NULL);

  // Spawn a thread to handle writing the shared result buffer to disk
  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
  if( pthread_create(&resultbuff_thread, &attr, ResultWriter, (void*)(&SlaveInfo)) < 0 ) {
    Vprint(SEV_ERROR,"Failed to start resultbuff thread. Terminating.\n");
    Abort(1);
  }

}


static void Init_Slave()
{
  char fn[1024], *files, *s, *saveptr;
  long wr;
  int  rv, fd, i;

  // Cache information about output filenames (perhaps move to args_t)
  files = strdup(args.out_files);
  // Count entries first to avoid reallocing over and over TODO: use str_cnt_chr
  i = str_cnt_chr(files, ':') + 1;
  SlaveInfo.nout_files = i;
  SlaveInfo.out_files  = malloc(i * sizeof(char *));
  // Now store entries, just carve them out of our dup'd files array
  for (s = strtok_r(files, ":", &saveptr), i = 0; s;
       s = strtok_r(NULL, ":", &saveptr), ++i ) {
    SlaveInfo.out_files[i] = s;
  }

  // Build a file name for our private exe copy
  sprintf(fn,"%s",EXE_BASE);
  // Open the file
  if( (fd=open(fn,O_WRONLY|O_CREAT|O_TRUNC,S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)) < 0 ) {
    Vprint(SEV_ERROR,"Failed to open exe for writing. Terminating.\n");
    Abort(1);
  }
  // Write the data into the destination
  wr = 0;
  do {
    rv = write(fd, shm_exe+wr, shm_exe_sz-wr);
    if( rv < 0 ) {
      Vprint(SEV_ERROR,"Failed to write SHM into exe. Terminating.\n");
      Abort(1);
    }
    wr += rv;
  } while( wr < shm_exe_sz );
  // Close the exe file
  close(fd);

  // Create result buffer (only for ranks responsible)
  CreateResultBuffers();

  // Initialize experimental fork lock 
  fork_init_lock();
}


static void Slave_Exit()
{
  struct timeval  st,et;
  int i, f;

  // Tell the writer thread it is done and wait for it
  Vprint(SEV_DEBUG,"Slave %d waiting for writer to finish.\n",SlaveInfo.rank);
  SlaveInfo.done = 1;
  pthread_mutex_lock(&(SlaveInfo.resultb_lock));
  pthread_cond_signal(&(SlaveInfo.resultb_nempty));
  pthread_mutex_unlock(&(SlaveInfo.resultb_lock));
  pthread_join(resultbuff_thread,NULL);
  
  // !!av: Gating hack
  i = SlaveInfo.nprocs / ConcurrentWriters;
  if( SlaveInfo.nprocs%ConcurrentWriters ) {
    i++;
  }

  gettimeofday(&st, NULL);
  while( i-- ) {
    if( !(i%SlaveInfo.rank) ) {
      for( f=0; f<SlaveInfo.nout_files; ++f ) {
	fprintf(stderr, "Writing at %s:%s:%d\n", __FILE__, __func__, __LINE__);
        Write(Fd[f], Cbuff[f], Nc[f]);
      }
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }
  gettimeofday(&et, NULL);
  SlaveInfo.t_o  = ((et.tv_sec*1000000+et.tv_usec) -
                   (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
}


static void* Slave_Listener(void* arg)
{
  UNUSED(arg);

  // FIXME: HACK, it is stupid to do this. We should just fix the messaging policy
  long st;

  // Record start time
  st = time(NULL);

  // Wait until our time limit is exceeded
  while( (time(NULL)-st) <= args.time_limit ) {
    sleep(1);
  }

  Vprint(SEV_NRML,"Slave noticed time limit exceeded.  Flushing.\n\n");
	Slave_Exit();

	// END HACK

  return NULL;

	/*
  char d;
  while (1) {
		Vprint(SEV_DEBUG,"Slave %d listener received command from Master", SlaveInfo.rank);
    MPI_Recv(&d, sizeof(char), MPI_BYTE, MASTER_RANK, 
             TAG_MASTERCMD, MPI_COMM_WORLD, MPI_STATUS_IGNORE );

    if (d == MASTERCMD_EXIT) {
      Slave_Exit();
    }
  }
	*/
}


static void Start_Listener()
{
  pthread_attr_t  attr;

  // Setup thread properties
  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

  // Start thread
  int err = pthread_create(&(SlaveInfo.listener), &attr, Slave_Listener, NULL);
  if (err) {
    Vprint(SEV_ERROR,"Slave failed to start worker thread. %s. Terminating.\n", strerror(err));
    Abort(1);
  }
}



static void Slave(int processes, int rank)
{
  struct timeval  tv;
  slaveinfo_t    *si=&SlaveInfo;
  compressedb_t  *cb, *cbi;
  workunit_t     *workunit;
  request_t      *request;
  int             qd=0,i,j,sz,base_id, err;

  gettimeofday(&tv, NULL);
  Vprint(SEV_TIMING,"[TIMING] Slave %d started at %d.%06d.\n",
               SlaveInfo.rank, tv.tv_sec, tv.tv_usec);
  
  memset(si,0,sizeof(slaveinfo_t));
  si->nprocs = processes;
  si->rank   = rank;
  
  Init_Slave();
  Start_Listener();
  Start_Workers();

  // Wait for all the ranks to fully init
  MPI_Barrier(MPI_COMM_WORLD);

  while( !si->done ) {
    // Check our writer's status for error
    if( result_thread_error ) {
      Vprint(SEV_ERROR,"Slave noticed that the writer hit an error. Terminating.\n");
      Abort(1);
    }

    // Get a request from our worker threads
    request = tscq_entry_get(si->rq);
    Vprint(SEV_DEBUG,"Slave %d found a worker in need of work.\n",SlaveInfo.rank);

    // See if we can handle this request without the master
    if( qd > 0 ) {
      // We already put a at least one "extra" work unit on the work queue.
      qd--;
      if( qd >= MCW_NCORES ) {
        // If the number of queued work units is more than
        // the number of cores, we won't request more work
        // from the master.  Else, we will get more work.
        tscq_entry_free(si->rq,request);
        Vprint(SEV_DEBUG,"Slave %d not forwarding request: %d WUs queued.\n",
               SlaveInfo.rank,qd);
        continue;
      }
    }

    // Request a work unit from the master
    Vprint(SEV_DEBUG,"Slave %d asking for more work from master (qd:%d).\n",
           SlaveInfo.rank,qd);
    request->count = MCW_NCORES-qd;
    err = MPI_Send(request, sizeof(request_t), MPI_BYTE, MASTER_RANK,
             TAG_REQUEST, MPI_COMM_WORLD);
    printf("MPI: Slave with rank %d sent request: %d\n", SlaveInfo.rank, err);
    tscq_entry_free(si->rq,request);

    // Read the work unit from master
    workunit = tscq_entry_new(si->wq);
    MPI_Recv(workunit, sizeof(workunit_t), MPI_BYTE, MASTER_RANK, 
             TAG_WORKUNIT, MPI_COMM_WORLD, MPI_STATUS_IGNORE );

    // Figure out what this work unit is telling us to do
    switch( workunit->type ) {
    case WU_TYPE_EXIT:
      // Master has told us we are done; send this to workers
      Vprint(SEV_DEBUG,"Slave %d told to exit by master.\n",SlaveInfo.rank);
      tscq_entry_free(si->wq,workunit);
      for(i=0; i<MCW_NCORES; i++) {
        workunit = tscq_entry_new(si->wq);
        memset(workunit,0,sizeof(workunit_t));
        workunit->type = WU_TYPE_EXIT;
        tscq_entry_put(si->wq,workunit);
      }

      // Wait for the worker threads to exit
      Vprint(SEV_DEBUG,"Slave %d waiting for workers to finish.\n",SlaveInfo.rank);
      for(i=0; i<MCW_NCORES; i++) {
        pthread_join(SlaveInfo.workers[i], NULL);
      }

      Slave_Exit();
      break;
    case WU_TYPE_SEQS:
      // Master is sending us sequence data; prepare for and read it
      GrowSeqData(workunit->len+1);
      MPI_Recv(si->seq_data, workunit->len, MPI_BYTE, MASTER_RANK, 
               TAG_SEQDATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      sz      = workunit->len;
      base_id = workunit->blk_id;
      tscq_entry_free(si->wq,workunit);
      // Break up the larger work unit into smaller units
      // and send them to worker threads.
      // ||Q0a|Q0b||Q1a|Q1b||...||QNa|QNb|
      for(i=0,cb=(compressedb_t*)si->seq_data; cb<(compressedb_t*)(si->seq_data+sz); cb=cbi,i++) {
        // Get queue entry, fill most of header
        workunit = tscq_entry_new(si->wq);
        workunit->type   = WU_TYPE_SEQF;
        workunit->blk_id = base_id + i;
        workunit->len    = 0;

        // Loop over blocks we need for multi-input-file support. Accumulate length
        for(j=0,cbi=cb; j<args.nq_files; ++j, cbi=((void*)cbi)+cbi->len+sizeof(cbi->len)) {
          workunit->len += cbi->len + sizeof(cbi->len);
        }

        // Fill data
        workunit->data   = safe_malloc(workunit->len);
        if( !workunit->data ) {
          Vprint(SEV_ERROR,"Slave failed to allocate work unit data for worker. Terminating.\n");
          Abort(1);
        }
        memcpy(workunit->data, cb, workunit->len);
        // Dispatch
        tscq_entry_put(si->wq,workunit);

        // Check for pre-caching case
        if( i ) {
          qd++;
        }
      }
      Vprint(SEV_DEBUG,"Slave %d turned WU into %d worker WUs (qd:%d).\n",
             SlaveInfo.rank,i,qd);
      break;
    default:
      Vprint(SEV_ERROR,"Slave received unknown work unit type. Terminating.\n");
      Abort(1);    
      break;
    }
  }

  // Wait for everyone to be ready to exit
  Vprint(SEV_DEBUG,"Slave %d done.\n",SlaveInfo.rank);
  MPI_Barrier(MPI_COMM_WORLD);

  // Now that everyone seems to be ready to exit, go ahead and report times
  Report_Timings(SlaveInfo.rank,
                 SlaveInfo.t_i,
                 SlaveInfo.t_o,
                 SlaveInfo.t_ic,
                 SlaveInfo.t_oc, 
                 SlaveInfo.t_vi,
                 SlaveInfo.t_vo,
                 SlaveInfo.b_i,
                 SlaveInfo.b_o,
                 SlaveInfo.b_id,
                 SlaveInfo.b_od);
  
}


////////////////////////////////////////////////////////////////////////////////
//                              DB Loading Code                               //
////////////////////////////////////////////////////////////////////////////////


static void Load_File(char *name, long size, void *dest)
{
  long rd;
  int  rv,f;

  // Open the DB file
  if( (f = open(name,O_RDONLY)) < 0 ) {
    Vprint(SEV_ERROR,"Failed to open() DB file. Terminating.\n");
    Abort(1);
  }

  // Read the DB into the destination
  rd = 0;
  do {
    rv = read(f, dest+rd, size-rd);
    if( rv < 0 ) {
      Vprint(SEV_ERROR,"Failed to read DB into SHM. Terminating.\n");
      Abort(1);
    }
    rd += rv;
  } while( rd < size );
  
  // Close the DB file
  close(f);
}


static char **Find_DBFiles(int rank, int *nfiles)
{
  UNUSED(rank);
  char  *fl,*f,**files,*saveptr=NULL;
  int    nf;

  files = NULL;
  nf = 0;

  // Build list of exe files from the file list
  fl = strdup(args.exes);
  f  = strtok_r(fl, ":", &saveptr);
  while( f ) {
    // Make room in array for another file
    if( !(files=realloc(files,(nf+1)*sizeof(char*))) ) {
      Vprint(SEV_ERROR,"Failed to enlarge file list. Terminating.\n");
      Abort(1);
    }
    // Put file in list
    files[nf] = strdup(f);
    nf++;
    // Advance to next file in list
    f = strtok_r(NULL, ":", &saveptr);
  }
  free(fl);
  Vprint(SEV_DEBUG, "Rank %d found EXE files\n", rank);
  
  // Build list of DB files from the file list
  fl = strdup(args.db_files);
  f  = strtok_r(fl, ":", &saveptr);
  while( f ) {
    // Make room in array for another file
    if( !(files=realloc(files,(nf+1)*sizeof(char*))) ) {
      Vprint(SEV_ERROR,"Failed to enlarge file list. Terminating.\n");
      Abort(1);
    }
    // Put file in list
    files[nf] = strdup(f);
    nf++;
    // Advance to next file in list
    f = strtok_r(NULL, ":", &saveptr);
  }
  free(fl);
  Vprint(SEV_DEBUG, "Rank %d found DB files\n", rank);

  // Check number of DB files
  if( nf > MAX_DB_FILES ) {
    Vprint(SEV_ERROR,"Too many DB files. Terminating.\n");
    Abort(1);    
  }

  // Return list of DB files
  *nfiles = nf;
  return files;
}


static void Init_DB(int procs, int rank, float *lt, float *ct)
{
  struct timeval  st,et;
  struct stat     statbf;
  void           *shm;
  long            shmsz;
  char          **files, name[1024], *s, *saveptr;
  int             i,j,rv,node,nodes,loadstride,range[1][3];
  int             nfiles,nout_files,nexe_files,ranks_exe;
  MPI_Group       oldgroup,group;
  MPI_Comm        newcomm=MPI_COMM_NULL,comm;

  // Get list of DB files
  files = Find_DBFiles(rank, &nfiles);

  // These will be used a lot later on
  node       = rank;
  nodes      = procs;
  loadstride = nodes/args.ndbs;

  nexe_files = str_cnt_chr(args.exes, ':') + 1;
  nout_files = str_cnt_chr(args.out_files, ':') + 1;

  // determine ranks_exe from rank and args.rank_exe
  for (s = strtok_r(args.rank_exe, ":", &saveptr), i = 0;
       s && i <=rank;
       s = strtok_r(NULL, ":", &saveptr), ++i ) {
    sscanf(s, "%d", &ranks_exe);
  }
  Vprint(SEV_DEBUG, "Rank %d will use exe file number %d\n", rank, ranks_exe);

  // This is just for timing data, really
  (*lt)=0.0f;
  (*ct)=0.0f;
  MPI_Barrier(MPI_COMM_WORLD);
  Vprint(SEV_DEBUG, "Rank %d past first barrier\n", rank);

  memset(file_is_shm, 0, sizeof(int)*MAX_DB_FILES);

  // Figure out our role w.r.t the DB loading
  if( loadstride > 1 ) {
    // A DB load will cover more than one node; comm group needed
    for(i=0; i<args.ndbs; i++) {
      // Each DB represents one of the new comm groups.
      //
      // Note that these calls are collective, so _every_ rank
      // in MPI_COMM_WORLD needs to make the same call, even if
      // one of the ranks is not going to be placed in the new
      // communicator.
      range[0][0] = i*loadstride;
      range[0][1] = range[0][0] + loadstride - 1;
      range[0][2] = 1;
      MPI_Comm_group(MPI_COMM_WORLD,&oldgroup);
      rv = MPI_Group_range_incl(oldgroup, 1, range, &group);
      if( rv != MPI_SUCCESS ) {
        Vprint(SEV_ERROR,"Failed to create commgroup for DB bcast. Terminating.\n");
        Abort(1);
      }
      rv = MPI_Comm_create(MPI_COMM_WORLD, group, &comm);
      if( rv != MPI_SUCCESS ) {
        Vprint(SEV_ERROR,"Failed to create comm for DB bcast. Terminating.\n");
        Abort(1);
      }
      if( i == node/loadstride ) {
        newcomm = comm;
      } else {
        // Cleanup
        //MPI_Comm_free(&comm);
        //MPI_Group_free(&group);
      }
    }
    if( !(node%loadstride) ) {
      Vprint(SEV_DEBUG, "Rank %d reports as bcast sender\n", rank);
      ////////////////////////////////////////////////////////////
      // We are a loading rank and we are a bcast send rank.
      ////////////////////////////////////////////////////////////
      // Get file size array ready
      file_sizes = Create_SHM("file_sizes",sizeof(filesizes_t),&file_sizes_fd);
      memset(file_sizes,0,sizeof(filesizes_t));
      // Create the in/out SHMs per core
      for(i=0; i<MCW_NCORES; i++) {
	for(j=0; j<args.nq_files; j++) {
	  sprintf(name,":MCW:W%d:IN%d",i,j);
	  Create_DBSHM(name,QUERYBUFF_SIZE);
	}
	for(j=0; j<nout_files; j++) {
	  sprintf(name,":MCW:W%d:OUT%d",i,j);
	  Create_DBSHM(name,RESULTBUFF_SIZE);
	}
      }
      // For each DB file
      for(i=0; i < nfiles; i++) {
        // Build full name
        if( i < nexe_files ) {
          // exe file
          sprintf(name,"%s",files[i]);
        } else {
          // DB file
          sprintf(name,"%s/%s%d/%s",
            args.db_path,args.db_prefix,node/loadstride,files[i]);
        }
        // Find size
        gettimeofday(&st, NULL);
        if( stat(name, &statbf) < 0 ) {
          Vprint(SEV_ERROR,"Failed to stat() DB file \"%s\". Terminating.\n",name);
          Abort(1);
        }
        gettimeofday(&et, NULL);
        (*lt) += ((et.tv_sec*1000000+et.tv_usec) - 
            (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
        // Bcast size
        shmsz = statbf.st_size;
        gettimeofday(&st, NULL);
        MPI_Bcast(&shmsz, sizeof(shmsz), MPI_BYTE, 0, newcomm);
        gettimeofday(&et, NULL);
        (*ct) += ((et.tv_sec*1000000+et.tv_usec) - 
                 (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
        // Load data
        shm = Create_DBSHM(name,shmsz);
        gettimeofday(&st, NULL);	
        Load_File(name, shmsz, shm);
        gettimeofday(&et, NULL);
        (*lt) += ((et.tv_sec*1000000+et.tv_usec) - 
                 (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
        // Bcast data
        gettimeofday(&st, NULL);
        MPI_Bcast(shm, shmsz, MPI_BYTE, 0, newcomm);
        (*ct) += ((et.tv_sec*1000000+et.tv_usec) - 
                 (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
        // Save exe SHM
        if( i == ranks_exe ) {
          // exe data
          shm_exe    = shm;
          shm_exe_sz = shmsz;
        }
      }
      // We are done loading the DB, we don't need the comm or group any more
      //MPI_Comm_free(&newcomm);
      //MPI_Group_free(&group);
    } else if ( node%loadstride ) {
      Vprint(SEV_DEBUG, "Rank %d reports as bcast receiver\n", rank);
      ////////////////////////////////////////////////////////////
      // We are a bcast receive rank.
      ////////////////////////////////////////////////////////////
      // Get file size array ready
      file_sizes = Create_SHM("file_sizes", sizeof(filesizes_t),&file_sizes_fd);
      memset(file_sizes,0,sizeof(filesizes_t));
      // Create the two in/out SHMs per core
      for(i=0; i<MCW_NCORES; i++) {
	for(j=0; j<args.nq_files; j++) {
          sprintf(name,":MCW:W%d:IN%d",i,j);
          Create_DBSHM(name,QUERYBUFF_SIZE);
        }
	for(j=0; j<nout_files; j++) {
	  sprintf(name,":MCW:W%d:OUT%d",i,j);
	  Create_DBSHM(name,RESULTBUFF_SIZE);
	}
      }
      // For each DB file
      for(i=0; i < nfiles; i++) {
        // Receive size
        gettimeofday(&st, NULL);
        MPI_Bcast(&shmsz, sizeof(shmsz), MPI_BYTE, 0, newcomm);
        gettimeofday(&et, NULL);
        (*ct) += ((et.tv_sec*1000000+et.tv_usec) - 
                 (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
        // Receive data
        if( i < nexe_files) {
          // exe data
          sprintf(name,"%s",files[i]);
        } else {
          // DB data
          sprintf(name,"%s/%s%d/%s",
                  args.db_path,args.db_prefix,node/loadstride,files[i]);
        }
        shm = Create_DBSHM(name,shmsz);
        gettimeofday(&st, NULL);
        MPI_Bcast(shm, shmsz, MPI_BYTE, 0, newcomm);
        gettimeofday(&et, NULL);
        (*ct) += ((et.tv_sec*1000000+et.tv_usec) - 
            (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
        // Save exe SHM
        if( i == ranks_exe ) {
          // exe data
          shm_exe    = shm;
          shm_exe_sz = shmsz;
        }
      }
      // We are done loading the DB, we don't need the comm or group any more
      //MPI_Comm_free(&newcomm);
      //MPI_Group_free(&group);
    } else {
      Vprint(SEV_DEBUG, "Rank %d reports impossible bcast situation\n", rank);
      ////////////////////////////////////////////////////////////
      // We are not involved in DB loading
      ////////////////////////////////////////////////////////////
    }
  } else {
    Vprint(SEV_DEBUG, "Rank %d reports no bcast necessary\n", rank);
    ////////////////////////////////////////////////////////////
    // A DB load will cover only one node.  Load once per node.
    ////////////////////////////////////////////////////////////
    // Get file size array ready
    file_sizes = Create_SHM("file_sizes", sizeof(filesizes_t),&file_sizes_fd);
    memset(file_sizes,0,sizeof(filesizes_t));
    // Create the two in/out SHMs per core
    for(i=0; i<MCW_NCORES; i++) {
      for(j=0; j<args.nq_files; j++) {
        sprintf(name,":MCW:W%d:IN%d",i,j);
        Create_DBSHM(name,QUERYBUFF_SIZE);
      }
      for(j=0; j<nout_files; j++) {
	sprintf(name,":MCW:W%d:OUT%d",i,j);
	Create_DBSHM(name,RESULTBUFF_SIZE);
      }
    }
    // For each DB file
    for(i=0; i < nfiles; i++) {
      // Build full path name
      if( i < nexe_files ) {
        // exe file
        sprintf(name,"%s",files[i]);
      } else {
        // DB file
        sprintf(name,"%s/%s%d/%s",
                args.db_path,args.db_prefix,node/loadstride,files[i]);
      }
      // Find size
      gettimeofday(&st, NULL);
      if( stat(name, &statbf) < 0 ) {
        Vprint(SEV_ERROR,"Failed to stat() DB file. Terminating.\n");
        Abort(1);
      }
      gettimeofday(&et, NULL);
      (*lt) += ((et.tv_sec*1000000+et.tv_usec) - 
               (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
      shmsz = statbf.st_size;
      // Load data
      shm = Create_DBSHM(name,shmsz);
      gettimeofday(&st, NULL);
      Load_File(name, shmsz, shm);
      gettimeofday(&et, NULL);
      (*lt) += ((et.tv_sec*1000000+et.tv_usec) - 
               (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
      // Save exe SHM
      if( i == ranks_exe ) {
        shm_exe    = shm;
        shm_exe_sz = shmsz;
      }
    }
  }

}


////////////////////////////////////////////////////////////////////////////////
//                       Application Entry + Helpers                          //
////////////////////////////////////////////////////////////////////////////////


static void Init_MPI(int *procs, int *rank, int *argc, char ***argv)
{
  int rc;

  // Init MPI, get the number of MPI processes and our rank
  rc = MPI_Init(argc,argv);
  /*
     rc = MPI_Init_thread(argc,argv,MPI_THREAD_MULTIPLE, &thread_support);
     if(thread_support != MPI_THREAD_MULTIPLE) {
     Vprint(SEV_ERROR,"MPI_THREAD_MULTIPLE not supported. Terminating.\n");
     Abort(rc);
     }
   */

  if(rc != MPI_SUCCESS) {
    Vprint(SEV_ERROR,"Error starting MPI program. Terminating.\n");
    Abort(rc);
  }
  MPI_Comm_size(MPI_COMM_WORLD,procs);
  MPI_Comm_rank(MPI_COMM_WORLD,rank);
  if( !(*rank) ) {
    Vprint(SEV_NRML,"MPI ranks: %d\n\n",*procs);
  }
  if( (*procs) < 2 ) {
    Vprint(SEV_ERROR,"At least two MPI processes required. Terminating.\n");
    Abort(rc);
  }

  // Save our rank to exclude/include our rank's print on caught signal
  Rank = *rank;

  // Get current working directory.
  if( !(*rank) ) {
    Vprint(SEV_NRML,"Setting up output directories.\n\n");
  }
  if( *rank ) {
    char fn[1024], *jn;
    
    // This slave has it's own output directory.
    // I don't want it touching anything that is shared in any way
    // more than it has to.  So, I will create the dir for the slave
    // on startup; after which it can just stay there.

    jn = getenv("MCW_JOB_NAME");
    if( jn ) {
      snprintf(fn, 1024, "job-%s", jn);
      mkdir(fn,S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);

      snprintf(fn, 1024, "job-%s/%02d", jn, (*rank)/100);
      mkdir(fn,S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);

      snprintf(fn, 1024, "job-%s/%02d/%d", jn, (*rank)/100, *rank);
      mkdir(fn,S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
    } else {
      snprintf(fn, 1024, "mcw-out");
      mkdir(fn,S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);

      snprintf(fn, 1024, "mcw-out/%02d", (*rank)/100);
      mkdir(fn,S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);

      snprintf(fn, 1024, "mcw-out/%02d/%d", (*rank)/100, *rank);
      mkdir(fn,S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
    }

    // The needed directories should now exist.
    // Make our directory the current working dir.
    if( chdir(fn) < 0 ) {
      Vprint(SEV_ERROR,"Slave failed to chdir to work directory. Terminating.\n");
      Abort(1);
    }
  }

  // Record our hostname and rank
  if( !(*rank) ) {
    Vprint(SEV_NRML,"Recording node hostnames.\n\n");
  }
  {
    FILE *f;
    char  b[256];

    if( (f=fopen("host","w")) ) {
      if( !gethostname(b,sizeof(b)) ) {
        fprintf(f,"%s %d\n",b,*rank);
      }
      fclose(f);
    }
  }

}


static void UsageError()
{
  Vprint(SEV_ERROR,"Usage: Set the following env vars correctly:\n");
  Vprint(SEV_ERROR,"\tMCW_DB_PATH\n");
  Vprint(SEV_ERROR,"\tMCW_DB_PREFIX\n");
  Vprint(SEV_ERROR,"\tMCW_DB_COUNT\n");
  Vprint(SEV_ERROR,"\tMCW_DB_FILES\n");
  Vprint(SEV_ERROR,"\tMCW_Q_FILE\n");
  Vprint(SEV_ERROR,"\tMCW_Q_WUM\n");
  Vprint(SEV_ERROR,"\tMCW_S_TIMELIMIT\n");
  Vprint(SEV_ERROR,"\tMCW_S_EXE\n");
  Vprint(SEV_ERROR,"\tMCW_S_LINE\n");
  Vprint(SEV_ERROR,"\tMCW_S_DUP2\n");
  Vprint(SEV_ERROR,"\tMCW_O_FILES\n");
  Abort(1);
}


static void Parse_Environment(int procs)
{
  char *p;


  // Read DB settings
  if( !(p=getenv("MCW_DB_PATH")) ) {
    UsageError();
  }
  args.db_path = strdup(p);
  if( !(p=getenv("MCW_DB_PREFIX")) ) {
    UsageError();
  }
  args.db_prefix = strdup(p);
  if( !(p=getenv("MCW_DB_COUNT")) ) {
    UsageError();
  }
  if( sscanf(p,"%d",&(args.ndbs)) != 1 ) {
    UsageError();
  }
  if( (procs)%args.ndbs ) {
    Vprint(SEV_ERROR,"(MPI processes) %c nDBs != 0.\n",'%');
    UsageError();
  }
  if( !(p=getenv("MCW_DB_FILES")) ) {
    UsageError();
  }
  args.db_files = strdup(p);

  // Read Query settings
  if( !(p=getenv("MCW_Q_FILE")) ) {
    UsageError();
  }
  args.queryf = strdup(p);
  if( (p=getenv("MCW_Q_COUNT")) ) {
    if( sscanf(p,"%d",&(args.nq_files)) != 1 ) {
      UsageError();
    }
  } else {
    args.nq_files = 1;
  }
  if( !(p=getenv("MCW_Q_WUM")) ) {
    UsageError();
  }
  if( sscanf(p,"%d",&(args.wum)) != 1 ) {
    UsageError();
  }

  // Read Output settings
  if( !(p=getenv("MCW_O_FILES")) ) {
    UsageError();
  }
  args.out_files = strdup(p);

  // Read search settings
  if( !(p=getenv("MCW_S_TIMELIMIT")) ) {
    UsageError();
  }
  if( sscanf(p,"%ld",&(args.time_limit)) != 1 ) {
    UsageError();
  }
  if( !(p=getenv("MCW_S_EXES")) ) {
    UsageError();
  }
  args.exes = strdup(p);
  if( !(p=getenv("MCW_S_RANK_EXE")) ) {
    UsageError();
  }
  args.rank_exe = strdup(p);
  if( !(p=getenv("MCW_S_LINE")) ) {
    UsageError();
  }
  args.mode = strdup(p);
  if( !(p=getenv("MCW_S_DUP2")) ) {
    UsageError();
  }
  args.dup2 = strdup(p);
}


static void cleanup()
{
}


static void sighndler(int arg)
{
  UNUSED(arg);
  // Be verbose
  if( !Rank ) {
    write(2,"ms: Caught signal; cleaning up.\n",32);
  }

  Master_Flush();
  cleanup();
  kill(getpid(), SIGKILL);
}

void sigusr_forkunlock(int arg)
{
  UNUSED(arg);
  fork_unlock();
}

static void exitfunc()
{
  int i;
  char shmname[256];

  // Be verbose
  if( !Rank ) {
    write(2,"ms: atexit(); cleaning up.\n",27);
  }

  // Free SHMs
  for (i=0; i<file_sizes->nfiles; ++i) {
    if (file_is_shm[i]) {
      snprintf(shmname, 256, "/mcw.%d.%d", getpid(), i);
      shm_unlink(shmname);
    }
  }
  
  // Free index SHM
  snprintf(shmname, 256, "/mcw.%d.file_sizes", getpid());
  shm_unlink(shmname);

  cleanup();
}


void* killtimer(void *arg)
{
  UNUSED(arg);
  long st;

  // Record start time
  st = time(NULL);

  // Wait until our time limit is exceeded
  while( (time(NULL)-st) <= args.time_limit ) {
    sleep(1);
  }

  // Be a little verbose
  Vprint(SEV_ERROR,"Master noticed time limit exceeded.  Terminating.\n\n");

  // Terminate our process in a way the MPI subsystem will pick up on
	Master_Flush();
  cleanup();
  kill(getpid(), SIGKILL);
  Abort(1);

  // This will be ignored
  return NULL;
}


int main(int argc, char **argv)
{
  struct timeval  st,et;
  float           lt,ct;
  int             processes,rank;


  // Be sure we can catch some basic signals to ensure
  // all SHMs can be removed, even in case of error.
  atexit(exitfunc);
  signal(SIGTERM, sighndler);
  signal(SIGUSR1, sigusr_forkunlock);
  signal(SIGPIPE, SIG_IGN);

  // Initialize basic MPI subsystem
  gettimeofday(&st, NULL);
  Init_MPI(&processes, &rank, &argc, &argv);

  // Let the master take a look at the args first;
  // no need to see thousands of error messages.
  if( !rank ) {
    Parse_Environment(processes);
  }

  // Everyone else needs to parse args too.
  MPI_Barrier(MPI_COMM_WORLD);
  Parse_Environment(processes);
  MPI_Barrier(MPI_COMM_WORLD);

  // Record this portion of init time
  gettimeofday(&et, NULL);
  init_time = ((et.tv_sec*1000000+et.tv_usec) -
              (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;

  // Start timer now that we know the limit from the command line
  if( !rank ) {
    pthread_attr_t attr;
    pthread_t      thread;

    // Setup kill timer thread properties
    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    // Start kill timer threads
    if( pthread_create(&thread, &attr, killtimer, NULL) )  { 
      Vprint(SEV_ERROR,"Master failed to start kill timer thread. Terminating.\n");
      Abort(1);
    }
  }

  // The master will record some timings for us
  if( !rank ) {
    memset(&MasterInfo,0,sizeof(masterinfo_t));
    MasterInfo.st = time(NULL);
  }

  // Initialize DB and make data available to all nodes
  if( !rank ) {
    Vprint(SEV_NRML,"Distributing DB files to nodes.\n");
  }
  Vprint(SEV_DEBUG,"Rank %d calling Init_DB()\n", rank);
  Init_DB(processes,rank,&lt,&ct);
  Vprint(SEV_DEBUG,"Rank %d done with Init_DB()\n", rank);

  // Wait for all the ranks to finish DB loading
  Vprint(SEV_NRML,"Waiting for ranks to finish DB load\n");
  MPI_Barrier(MPI_COMM_WORLD);

  // Master reports DB load time
  if( !rank ) {
    Vprint(SEV_NRML,"Finished with DB load in %.3f seconds (%.3f I/O).\n",
           ct+lt,lt);
  }

  // Which role do we play?
  if( !rank ) {
    // We will hand out work units (queries) to the workers
    Master(processes,rank);
  } else {
    // We will request and receive work units from the master
    Slave(processes,rank);
  }

  // Wait for all the ranks to get ready before cleanup
  MPI_Barrier(MPI_COMM_WORLD);

  // Free any SHM segments
  cleanup();

  // Done with MPI
  MPI_Finalize();
  return 0;
}

// vim: ts=8:sts=2:sw=2
