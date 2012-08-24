#ifndef HSP_ZUTILS_H__
#define HSP_ZUTILS_H__

#include <stdio.h> // FILE
#include <zlib.h>  // Z_* return codes TODO: abstract

typedef struct
{
  off_t  offset;
  size_t size;
} zutil_block_info_t;

/* Decompress from file source to file dest until stream ends or EOF.
   inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_DATA_ERROR if the deflate data is
   invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
   the version of the library linked do not match, or Z_ERRNO if there
   is an error reading or writing the files. */
int zutil_inf(FILE *dest, FILE *source, int *blks);

/* Count number of blocks in file source until stream ends.
   returns Z_OK on success and Z_DATA_ERROR on erroneous data or file
	 access issues. */
int zutil_blk_cnt(FILE *source, int *blks);

/* Iterate over block descriptors.
 * places block size into bsz, and returns 1 if iterator is still valid
 * otherwise, 0 is returned */
int zutil_blk_iter(FILE *source, long *bsz);

// Writes "sz" bytes from "source" to the stream "dest", compressing
// the data first with zlib compression strength "level".
//
// Special thanks to Mark Adler for providing the non-copyrighted
// public domain example program "zpipe.c", from which this function
// is based (Version 1.4  December 11th, 2005).
int zutil_compress_write(FILE *dest, void *source, int sz, int level);

#endif
