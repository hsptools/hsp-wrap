#ifndef HSP_IOUTILS_H__
#define HSP_IOUTILS_H__

#include <sys/types.h>

int ioutil_open_w (const char *fpath, int force, int append);

void *ioutil_mmap_r (char *fpath, off_t *size);

#endif
