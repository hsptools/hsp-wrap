#ifndef MCW_H
#define MCW_H


////////////////////////////////////////////////////////////////////////////////
//                                 Settings                                   //
////////////////////////////////////////////////////////////////////////////////


// Number of cores/threads to utilize
#define NCORES  (12)


// Size of the I/O buffers
#define QUERYBUFF_SIZE      (1L<<20)                            // 1   MiB
#define RESULTBUFF_SIZE     (1L<<27)                            // 128 MiB
#define RESULTBUFF_GBL_SIZE (RESULTBUFF_SIZE*NCORES)
#define RESULTBUFF_SHM_SIZE (RESULTBUFF_GBL_SIZE+sizeof(int))  // + header


// Flag to use compression (zlib) on output data
#define COMPRESSION 1


// Verbosity (level of debugging prints)
#define DBGTIME     1
#define SEV_ERROR (-1)
#define SEV_NRML    0
#define SEV_WARN    1
#define SEV_DEBUG   2
#define VERBOSITY   SEV_DEBUG


// Maximum number of DB files
#define MAX_DB_FILES    (256)


// Many options are set from env vars, which are
// put in this struct, rather than constants.
typedef struct st_args {
  char *exe;             // Path to blastall executable
  char *exe_base;        // argv[0] of child process
  char *db_path;         // Path to DBs
  char *db_prefix;       // Path to DBs
  char *db_files;        // Files in the DB
  int   ndbs;            // Number of DBs to use
  int   wum;             // Work unit count multiplier
  char *mode;            // Blastall mode (e.g. blastp)
  char *queryf;          // Query file
  char *dup2;            // FD mappings for child
  long  time_limit;      // Runtime limit
  char *job_name;        // User-specified name for the run
} args_t;


////////////////////////////////////////////////////////////////////////////////
//                              Types and Defines                             //
////////////////////////////////////////////////////////////////////////////////


// A compressed block
typedef struct {
  long  len;
  char  data;
} compressedb_t;

////////////////////////////////////////////////////////////


// Types of work units
#define WU_TYPE_EXIT 0   // Exit flag for workers
#define WU_TYPE_SEQS 1   // WU is sequence data
#define WU_TYPE_SEQD 2   // WU is sequence data
#define WU_TYPE_SEQF 3   // WU is sequence data


// Tag for sed/recv work units and sequence data
#define TAG_WORKUNIT 0
#define TAG_SEQDATA  1


// For now, there will be one fixed master
#define MASTER_RANK 0


// Work unit sent from master to slaves
typedef struct st_workunit {
  int    type;      // Member of WU_TYPE_*
  int    len;       // Length of all query sequences
  char  *data;      // Pointer to work unit data
} workunit_t;


// Sequence data to follow work unit
typedef char* seq_data_t;


// Stores file sizes so one doesn't need to stat() as much.
typedef struct {
  char *shm;
  long  shmsize;
  long  size;
  int   fd;
  char  name[256];
} filesize_t;
typedef struct {
  int         nfiles;
  filesize_t  fs[MAX_DB_FILES];
} filesizes_t;


// Struct represents the SHM-based result buffer in memory.
typedef struct st_resultbuff {
  volatile long  count;
  volatile char  buff[RESULTBUFF_GBL_SIZE];
} resultbuff_t;


////////////////////////////////////////////////////////////


// Types of requests from slaves
#define RQ_WU 1   // Slave requests a work unit/command


// Tag for send/recv requests
#define TAG_REQUEST 2


// Request type from slaves to master
typedef struct st_request {
  int    type;      // Request type
  int    count;     // When type==RQ_WU, count is the number of blocks wanted
} request_t;


// Struct for the master to keep track of a slave
typedef struct st_slave {
  int         rank;       // Rank of the slave process
  int         sflg;       // Send flag; 1 => WU send in flight
  request_t   request;    // Data to hold incomming request from slave
  workunit_t  workunit;   // Space to hold work unit struct on way to slave
  seq_data_t  sequence;   // Space to hold sequence on way to slave
} slave_t;


////////////////////////////////////////////////////////////////////////////////


// Information the master needs (presistent)
typedef struct st_masterinfo {
  MPI_Request    *mpi_req;      // MPI request status for receiving WU requests
  slave_t        *slaves;       // Array of slave management structs
  void           *qmap;         // Raw mmap() of input file
  compressedb_t **queries;      // Array of pointers input blocks
  int             nqueries;     // Total number of query sequences
  int             nslaves;      // Number of slave processes
  int             nprocs;       // Total number of processes
  int             rank;         // Rank of the master
  long            st;           // Global start time
} masterinfo_t;


// Information the slave needs (persistent)
typedef struct st_slaveinfo {
  seq_data_t       seq_data;        // Space to hold query sequence data from master
  int              seq_data_len;    // Size of allocated memory for query seq data
  int              nprocs;          // Total number of processes
  int              rank;            // Rank of this slave
  volatile int     done;            // Exit flag for the slave
  tscq_t          *wq;              // Work queue
  tscq_t          *rq;              // Request queue
  volatile int     worker_error;    // Error flag for worker threads
  pthread_t        workers[NCORES]; // Array of handles to worker threads
  pthread_mutex_t  resultb_lock;    // Lock for the result buffer
  pthread_cond_t   resultb_nfull;   // Condition for not full
  pthread_cond_t   resultb_nempty;  // Condition for not empty
  pthread_mutex_t  time_lock;       // Lock for the cumulative timing stats
  pthread_mutex_t  fork_lock;       // Lock for experimenting with fork
  float            t_i;             // Real input time
  float            t_o;             // Real output time
  float            t_ic;            // Input decompression time
  float            t_oc;            // Output compression time
  float            t_vi;            // Virtual input time (as seen by wrapped prog)
  float            t_vo;            // Virtual output time (as seen by wrapped prog)
  unsigned long    b_i;             // Input (compressed) byte size
  unsigned long    b_o;             // Output (compressed) byte size
  unsigned long    b_id;            // Input (decompressed) byte size
  unsigned long    b_od;            // Output (uncompressed) byte size
} slaveinfo_t;


#endif
