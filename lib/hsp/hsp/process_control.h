#ifndef HSP_PROCESS_CONTROL_H__
#define HSP_PROCESS_CONTROL_H__

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>

#define MAX_PROCESSES  128
#define MAX_DB_FILES   (512)
#define MAX_FILE_PATH  256

#define PS_CTL_SHM_NAME "ps_ctl"
#define PID_ENVVAR "HSPWRAP_PID"
#define WORKER_ID_ENVVAR "HSPWRAP_WID"

// TODO: We can probably remove this with a better N:M mapping for files
typedef int16_t wid_t;
#define PRI_WID PRIu16
#define SCN_WID SCNu16
#define BAD_WID 0xFFFF

enum file_type {
  FTE_INPUT,
  FTE_OUTPUT,
  FTE_SHARED
};

/**
 * File description for a virtual file
 */
struct file_table_entry {
  // Public
  size_t shm_size;            // Size of the shm (needed for detach)
  int    shm_fd;              // File descriptor of the shm

  wid_t  wid;                 // Worker ID owning this file, -1 if shared TODO: N:M mapping
  size_t size;                // Size of the actual file data within the SHM
  char   name[MAX_FILE_PATH]; // Virtual name (path) of the file 
  enum file_type type;        // Type of mapping (in/out/shared)

  // Private (TODO: Move outside of file_table)
  char  *shm;                 // Pointer to shared data
};

/**
 * Table of all available virtual files
 */
struct file_table {
  int nfiles;
  struct file_table_entry file[MAX_DB_FILES];
};

// Prefix enum values?
// Kill all typedefs?

/**
 * Different states a process may be in (set by process)
 */
enum process_state {
  IDLE,      // Nothing actually running
  RUNNING,   // Process in standard running mode
  EOD,       // End of data, give me more data if you have it 
  NOSPACE,   // An output buffer is full and needs to be flushed/swapped 
  FAILED,    // Processor failed unexpectedly 
  DONE       // Processor completed running and terminated for whatever reason
};

/**
 * Command issued by scheduler
 */
enum process_cmd {
  NO_CMD,    // No command is currently provided
  RUN,       // More data is available, do something with it
  SUSPEND,   // Free as much resources as possible (kill processes, free buffers)
  RESTORE,   // Reinitialize after a suspend
  QUIT       // Free all resources and terminate
};

/**
 * Shared process control structure.  Layout of SHM.
 */
struct process_control {
  unsigned nprocesses;               // Number of processes

  // Between input layer and processes
  pthread_mutex_t    lock;
  pthread_cond_t     need_service;
  pthread_cond_t     process_ready[MAX_PROCESSES];
  enum process_state process_state[MAX_PROCESSES];
  enum process_cmd   process_cmd[MAX_PROCESSES];
  
  //sem_t sem_empty;                 // Signal exhausted data to parent process (work request)
  //sem_t sem_avail[MAX_PROCESSES];  // Signal available data to child processes (continue)
  //sem_t sem_wait_queue;            // Lock wait queue
  //wid_t wait_queue[MAX_PROCESSES]; // Processes with empty data buffers

  struct file_table ft;            // File descriptors
};


struct process_control *ps_ctl_init (unsigned nprocesses, int *fd);
void *ps_ctl_add_file (struct process_control *ps_ctl, wid_t wid,
                       const char *name, size_t sz, enum file_type type);
int   ps_ctl_all_done (struct process_control *ps_ctl);
int   ps_ctl_all_running (struct process_control *ps_ctl);
int   ps_ctl_all_waiting (struct process_control *ps_ctl);
void  ps_ctl_print (struct process_control *ps_ctl, FILE *f);

#endif // HSP_PROCESS_CONTROL_H__
