#ifndef HSP_ZUTILS_H__
#define HSP_ZUTILS_H__

#include <stdio.h> // FILE
#include <zlib.h>  // Z_* return codes

int zutil_inf(FILE *dest, FILE *source, int *blks);

#endif
