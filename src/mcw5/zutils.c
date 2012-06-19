// I'll keep the comments below just as evidence that there are no
// copyright issues with me using or modifying this.  It was released
// into the public domain.         ~Aaron Vose. 

/* zpipe.c: example of proper use of zlib's inflate() and deflate()
   Not copyrighted -- provided to the public domain
   Version 1.4  11 December 2005  Mark Adler */

/* Version history:
   1.0  30 Oct 2004  First version
   1.1   8 Nov 2004  Add void casting for unused return values
                     Use switch statement for inflate() return values
   1.2   9 Nov 2004  Add assertions to document zlib guarantees
   1.3   6 Apr 2005  Remove incorrect assertion in inf()
   1.4  11 Dec 2005  Add hack to avoid MSDOS end-of-line conversions
                     Avoid some compiler warnings for input and output buffers
   1.AV              Hacked to pieces by Aaron Vose; mostly to add input
                     support from files, and to allow the extraction of
		     concatenated compressed blocks.
   1.PG              Move to a utility library
*/

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <zlib.h>


#define CHUNK 16384

/* Decompress from file source to file dest until stream ends or EOF.
   inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_DATA_ERROR if the deflate data is
   invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
   the version of the library linked do not match, or Z_ERRNO if there
   is an error reading or writing the files. */
int
zutil_inf(FILE *dest, FILE *source, int *blks) {
  int ret;
  unsigned have;
  z_stream strm;
  unsigned char in[CHUNK];
  unsigned char out[CHUNK];
  long          bsz;

  (*blks) = 0;
  while (1) {

    // Read block size
    if (fread(&bsz,sizeof(bsz),1,source) != 1) {
      if (feof(source)) {
        // Ending at a file position reserved for a block
        // size indicates completion.
        return Z_OK;
      }
      return Z_DATA_ERROR;
    }

    // Init inflate state
    strm.zalloc   = Z_NULL;
    strm.zfree    = Z_NULL;
    strm.opaque   = Z_NULL;
    strm.avail_in = 0;
    strm.next_in  = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK) {
      return ret;
    }

    // Decompress until we have processed bsz bytes
    do {
      strm.avail_in = fread(in, 1, ((bsz>=CHUNK)?(CHUNK):(bsz)), source);
      if (ferror(source)) {
        inflateEnd(&strm);
        return Z_ERRNO;
      }
      if (strm.avail_in == 0) {
        return Z_DATA_ERROR;
      }
      strm.next_in = in;
      bsz -= strm.avail_in;
      /* run inflate() on input until output buffer not full */
      do {
        strm.avail_out = CHUNK;
        strm.next_out = out;

        ret = inflate(&strm, Z_NO_FLUSH);
        switch (ret) {
        case Z_NEED_DICT:
          ret = Z_DATA_ERROR;
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
          inflateEnd(&strm);
          return ret;
        case Z_STREAM_ERROR:
          return Z_STREAM_ERROR;
        }

        have = CHUNK - strm.avail_out;
        if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
          inflateEnd(&strm);
          return Z_ERRNO;
        }
      } while (strm.avail_out == 0);

      // Done when inflate() says it's done
    } while (ret != Z_STREAM_END);

    // clean up
    inflateEnd(&strm);

    // If the stream ended before using all the data
    // in the block, return error.
    if (bsz) {
      return Z_DATA_ERROR;
    }

    // Increment number of blocks count
    (*blks)++;
  }

  return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}
