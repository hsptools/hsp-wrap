#ifndef TSCQ_H
#define TSCQ_H

#include <pthread.h>


typedef struct st_tscq {
  int               size;    // Maximum entries in queue
  int               nfree;   // Number of free entries
  void            **free;    // Array of free entries
  int               start;   // Start position in queue
  int               nqueue;  // Number of entries in queue
  void            **queue;   // Array of queued entries
  pthread_mutex_t   fmtx;    // Free list mutex lock
  pthread_cond_t    fcnd;    // Free list empty condition variable
  pthread_mutex_t   qmtx;    // Queue mutex lock
  pthread_cond_t    qcnd;    // Queue empty condition variable
} tscq_t;


#ifndef TSCQ_C

extern tscq_t* tscq_new(const int count, const size_t sz);
extern void    tscq_free(tscq_t *q);

extern void*   tscq_entry_new(tscq_t *q);
extern void    tscq_entry_free(tscq_t *q, void *e);

extern void*   tscq_entry_get(tscq_t *q);
extern void    tscq_entry_put(tscq_t *q, void *e);
#endif


#endif
