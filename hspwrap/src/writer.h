#ifndef WRITER_H__
#define WRITER_H__


struct writer_ctx {
  const char *name;                  // Output file name
  char   *buf;                       // Output buffer
  char   *ptr;                       // Current pointer for writer
  size_t  size;                      // Total size of either buffer
  size_t  avail;                     // How much free space?

  char   *back_buf;                  // Back buffer (where we flush from)
  size_t  back_len;                  // Length of valid data in back buffer

  int     running;                   // 1: keep running, 0: exit main loop
  int     nstreams;                  // How many streams are there (number of workers)

  uint64_t *voffsets;                // Virtual offset per stream
  uint64_t  poffset;                 // Physical offset within actual file

  int fd;                            // Primary file descriptor
  int idx;                           // Index file descriptor

  pthread_t       thread;
  pthread_mutex_t lock;              // Lock for entire context
  pthread_cond_t  space_avail;       // Signal to slave that there is space
  pthread_cond_t  data_pending;      // Signal to writer that there is data
};

int  writer_start (struct writer_ctx *ctx, const char *name, size_t buff_size, int nstreams);
void writer_write (struct writer_ctx *ctx, uint16_t stream, const void *buf, size_t count);
void wrtier_stop  (struct writer_ctx *ctx);

#endif // WRITER_H__
