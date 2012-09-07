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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <zlib.h>
#include <stdint.h>


#define CHUNK 16384

static int
inflate_block(FILE *dest, FILE *source, long bsz) {
  int ret;
  unsigned have;
  z_stream strm;
  unsigned char in[CHUNK];
  unsigned char out[CHUNK];

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

  return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}


int
zutil_inf2(FILE *dest, FILE *source, int *blks) {
  const size_t block_hdr_size = sizeof(uint32_t) + sizeof(uint32_t);

  uint32_t block_size;
  uint64_t data_size;
  uint16_t block_cnt;
  int      ret;

  *blks = 0;

  while (1) {
    // Read block size
    if (fread(&block_size,sizeof(block_size),1,source) != 1) {
      if (feof(source)) {
        // Ending at a file position reserved for a block
        // size indicates completion.
        return Z_OK;
      }
      return Z_DATA_ERROR;
    }

    // Read block count
    if (fread(&block_cnt,sizeof(block_cnt),1,source) != 1) {
      return Z_DATA_ERROR;
    }

    // Skip block header
    fseek(source, block_hdr_size * block_cnt, SEEK_CUR);
    if (feof(source)) {
      return Z_DATA_ERROR;
    }

    // Read data size
    if (fread(&data_size,sizeof(data_size),1,source) != 1) {
      return Z_DATA_ERROR;
    }

    ret = inflate_block(dest, source, data_size);
    if (ret != Z_OK) {
      return ret;
    }

    // Increment number of blocks count
    (*blks)++;
  }

  return Z_OK;
}


int
zutil_inf(FILE *dest, FILE *source, int *blks) {
  int ret;
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

    // Inflate block
    ret = inflate_block(dest, source, bsz);
    if (ret != Z_OK) {
      return ret;
    }

    // Increment number of blocks count
    (*blks)++;
  }

  return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}


/**
 * Count blocks in file.  Could be extended to return avg blk size etc...
 */
int
zutil_blk_cnt(FILE *source, int *blks) {
  long bsz;

  *blks = 0;
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

    // Now advance read pos to hopefully the next block (or EOF)
    if (fseek(source, bsz, SEEK_CUR)) {
      return Z_DATA_ERROR;
    }
    // Increment number of blocks count
    (*blks)++;
  }

  return Z_OK;
}


/**
 * Iterate over block descriptors.
 * TODO: instead of int *bsz, use a struct where we can track multiple stats
 *       as well as flag error cases
 */
int
zutil_blk_iter(FILE *source, long *bsz) {
  // Read block size
  if (fread(bsz, sizeof(*bsz), 1, source) != 1) {
    if (feof(source)) {
      // End of file, done
      return 0;
    }
    // Error, done (TODO: Flag error!)
    return 0;
  }

  // Now advance read pos to hopefully the next block (or EOF)
  if (fseek(source, *bsz, SEEK_CUR)) {
    // Error, done (TODO: Flag error!)
    return 0;
  }

  // Not done yet
  return 1;
}


// TODO: Replace error messages with status codes.
// TODO: Replace exit() with return()
// TODO: Add utility function for status code to string
void
zutil_compress_write(FILE *dest, void *source, int sz, int level) {
  z_stream       strm;
  unsigned char  in[CHUNK],out[CHUNK];
  int            blk,tw=sz;
  int            rv,flush;
  unsigned       ndata;
  long           lenpos,len;

  // Init zlib state
  strm.zalloc = Z_NULL;
  strm.zfree  = Z_NULL;
  strm.opaque = Z_NULL;
  rv = deflateInit(&strm, level);
  if( rv != Z_OK ) {
    fprintf(stderr,"Failed to init zlib.  Terminating.\n");
    exit(1);
  }

  // Write a placeholder long that will eventually hold the 
  // compressed block size.
  if( (lenpos=ftell(dest)) < 0 ) {
    fprintf(stderr,"Failed to find block size pos.  Terminating.\n");
    exit(1);
  }
  len = 0;
  if( fwrite(&len,sizeof(len),1,dest) != 1 ) {
    fprintf(stderr,"Failed to stub block size.  Terminating.\n");
    exit(1);
  }

  // Compress while there is still data to write
  do {
    // Setup input
    blk = ((tw < CHUNK)?(tw):(CHUNK));
    memcpy(in, source+(sz-tw), blk);
    strm.avail_in = blk;
    strm.next_in  = in;
    flush = ((!(tw-blk))?(Z_FINISH):(Z_NO_FLUSH));
    do {
      // Setup output and compress
      strm.avail_out = CHUNK;
      strm.next_out  = out;
      rv = deflate(&strm, flush);
      if( rv == Z_STREAM_ERROR ) {
        fprintf(stderr,"Failed to compress output block.  Terminating.\n");
        exit(1);
      }
      // Write compressed data to destination
      ndata = CHUNK - strm.avail_out;
      if( (fwrite(out, 1, ndata, dest) != ndata) || ferror(dest) ) {
        deflateEnd(&strm);
        fprintf(stderr,"Failed to compress output block.  Terminating.\n");
        exit(1);
      }
      len += ndata;
    } while( strm.avail_out == 0 );
    // Sanity check
    if( strm.avail_in != 0 ) {
      fprintf(stderr,"Did not fully compress block.  Terminating.\n");
      exit(1);
    }
    // Update "to write" count
    tw -= blk;
  } while( flush != Z_FINISH );

  // Another sanity check
  if( rv != Z_STREAM_END ) {
    fprintf(stderr,"Didn't finish compression properly.  Terminating.\n");
    exit(1);
  }

  // Cleanup
  deflateEnd(&strm);

  // Now that the compressed data is written, write the
  // size of the compressed block at the front of the block
  if( fseek(dest,lenpos,SEEK_SET) ) {
    fprintf(stderr,"Could not seek to len pos.  Terminating.\n");
    exit(1);
  }
  if( fwrite(&len,sizeof(len),1,dest) != 1 ) {
    fprintf(stderr,"Failed to write len.  Terminating.\n");
    exit(1);
  }
  //fprintf(stderr,"insz:\t%d\ncmpzsz:\t %ld\n",sz,len);
  if( fseek(dest,0,SEEK_END) ) {
    fprintf(stderr,"Could not seek to end.  Terminating.\n");
    exit(1);
  }

}


