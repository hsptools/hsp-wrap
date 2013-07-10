#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "hspwrap.h"
#include "writer.h"

static void * writer_main (void *arg);


static void *
writer_main (void *arg)
{
  struct writer_ctx *ctx = arg;

  int fd;
  // TODO: Consider O_NONBLOCK or O_SYNC
  fd = open(ctx->name, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR |S_IRGRP | S_IROTH);
  if (fd == -1) {
    fprintf(stderr, "writer: Could not open output file\n");
    exit(EXIT_FAILURE);
  }

  while (ctx->running) {
    // Flush if buffer is at least half full
    pthread_mutex_lock(&ctx->lock);
    // FIXME: this isn't the best waiting condition...
    while (ctx->running && ctx->avail > ctx->size/2) {
      pthread_cond_wait(&ctx->data_pending, &ctx->lock);
    }

    // Swap buffers
    char *tmp     = ctx->buf;
    ctx->buf      = ctx->back_buf;
    ctx->back_buf = tmp;
    ctx->back_len = ctx->size - ctx->avail;
    ctx->avail    = ctx->size;
    ctx->ptr      = ctx->buf;

    // Done with public state, unlock and signal completion
    pthread_mutex_unlock(&ctx->lock);
    pthread_cond_signal(&ctx->space_avail);

    // Now, actually write the output file
    ssize_t bytes = write(fd, ctx->back_buf, ctx->back_len);
    if (bytes == -1) {
      fprintf(stderr, "Couldn't write output: %s\n", strerror(errno));
    } else {
      trace("Wrote %zd of %zu bytes\n", bytes, ctx->back_len);
      // FIXME: Loop until everything is actually written (bytes < back_len)
    }
    fsync(fd);
    ctx->back_len = 0;
  }

  close(fd);
  return 0;
}


int
writer_start (struct writer_ctx *ctx, const char *name, size_t buff_size)
{
  // TODO: error checking
  ctx->name  = name;
  ctx->size  = buff_size;
  ctx->avail = ctx->size;
  ctx->buf   = malloc(ctx->size);
  ctx->ptr   = ctx->buf;
  ctx->back_buf = malloc(ctx->size);
  ctx->back_len = 0;
  ctx->running  = 1;

  if (!ctx->buf || !ctx->back_buf) {
    fprintf(stderr, "writer: Could not allocate buffers\n");
    exit(EXIT_FAILURE);
  }

  pthread_mutex_init(&ctx->lock, NULL);
  pthread_cond_init(&ctx->data_pending, NULL);
  pthread_cond_init(&ctx->space_avail, NULL);
  pthread_create(&ctx->thread, NULL, writer_main, ctx);
  return 0;
}


void
writer_write (struct writer_ctx *ctx, const void *buf, size_t count)
{
  // Make sure we have enough room to write the data
  pthread_mutex_lock(&ctx->lock);
  if (ctx->avail < count) {
    pthread_cond_signal(&ctx->data_pending);
  }
  while (ctx->avail < count) {
    pthread_cond_wait(&ctx->space_avail, &ctx->lock);
  }

  // .. and write it
  memcpy(ctx->ptr, buf, count);
  ctx->ptr   += count;
  ctx->avail -= count;
  pthread_mutex_unlock(&ctx->lock);
}
