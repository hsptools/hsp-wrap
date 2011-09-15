#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define TSCQ_C
#include "tscq.h"


void tscq_free(tscq_t *q)
{
  int i;

  if( q ) {
    // Destroy the free list
    if( q->free ) {
      for(i=0; i<q->nfree; i++) {
	free(q->free[i]);
      }
      free(q->free);
    }
    // Destroy the queue
    if( q->queue ) {
      free(q->queue);
    }
    free(q);
  }
}


tscq_t* tscq_new(const int count, const size_t sz)
{
  tscq_t *q;
  int     i;

  // Allocate any needed memory
  if( !(q=malloc(sizeof(tscq_t))) ) {
    return NULL;
  }
  if( !(q->free=malloc(count*sizeof(void*))) ) {
    tscq_free(q);
    return NULL;
  }
  if( !(q->queue=malloc(count*sizeof(void*))) ) {
    tscq_free(q);
    return NULL;
  }
  for(i=0; i<count; i++) {
    if( !(q->free[i]=malloc(sz)) ) {
      tscq_free(q);
      return NULL;
    }
    // Zero to be clean
    memset(q->free[i],0,sz);
    q->queue[i] = NULL;
  }

  // Init pthread objects
  if( pthread_mutex_init(&(q->fmtx), NULL) ||
      pthread_mutex_init(&(q->qmtx), NULL) ||
      pthread_cond_init(&(q->fcnd), NULL)  ||
      pthread_cond_init(&(q->qcnd), NULL)      ) {
    tscq_free(q);
    return NULL;
  }

  // Init values
  q->size   = count;
  q->nfree  = count;
  q->start  = 0;
  q->nqueue = 0;
  
  // Return the newly created queue object
  return q;
}


void* tscq_entry_new(tscq_t *q)
{
  void *e;

  // Lock the queue
  pthread_mutex_lock(&(q->fmtx));

  // Wail until there is a free entry available
  while( !q->nfree ) {
    pthread_cond_wait(&(q->fcnd), &(q->fmtx));
  }

  // Get the free entry
  e = q->free[--(q->nfree)];

  // Release our lock on the queue
  pthread_mutex_unlock(&(q->fmtx));

  // Return the free entry retreived from queue
  return e;
}


void tscq_entry_free(tscq_t *q, void *e)
{
  // Lock the queue
  pthread_mutex_lock(&(q->fmtx));

  // Put the entry on the free list
  q->free[q->nfree++] = e;

  // Signal that the free list is nonempty
  pthread_cond_signal(&(q->fcnd));

  // Release our lock on the queue
  pthread_mutex_unlock(&(q->fmtx));  
}


void* tscq_entry_get(tscq_t *q)
{
  void *e;
  
  // Lock the queue
  pthread_mutex_lock(&(q->qmtx));

  // Wait for a queue entry
  while( !q->nqueue ) {
    pthread_cond_wait(&(q->qcnd),&(q->qmtx));
  }

  // Take the entry off the queue
  e = q->queue[q->start++];
  q->nqueue--;
  if( q->start == q->size ) {
    q->start = 0;
  }

  // Release our lock on the queue
  pthread_mutex_unlock(&(q->qmtx));

  // Return the item taken off the queue
  return e;
}


void tscq_entry_put(tscq_t *q, void *e)
{
  int i;
  
  // Lock the queue
  pthread_mutex_lock(&(q->qmtx));

  // Put entry into queue
  if( (i=q->start+q->nqueue++) >= q->size ) {
    i -= q->size;
  }
  q->queue[i] = e;

  // Signal that the queue is nonempty
  pthread_cond_signal(&(q->qcnd));

  // Release our lock on the queue
  pthread_mutex_unlock(&(q->qmtx));
}
