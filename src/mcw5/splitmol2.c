#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
//#include <mpi.h>
#include <errno.h>
#include <stdint.h>

#define NFILE 1	//how many files in a directory
#define SPLITSTRING "@<TRIPOS>MOLECULE"

#define debug 1

////////////////////////////////////////////////////////////////////////////////
//                              Types and Defines                             //
////////////////////////////////////////////////////////////////////////////////


// Information the master needs (presistent)
typedef struct st_info {
  char       *queries;      // Pointer to start of input queries
  char       *cquery;       // Current input query
  char       *equery;       // One byte past last valid byte of query
  char      **sequences;    // Array of sorted (at some point) inputs
  size_t     *sequencels;   // Array of sorted (at some point) inputs
  size_t      nsequences;   // Number of known sequences
} info_t;


// This is a small until; I'll just make this a global
info_t nfo;


////////////////////////////////////////////////////////////////////////////////
//                                Fasta Code                                  //
////////////////////////////////////////////////////////////////////////////////


// Like strcmp, but respects some custom bounds
static int seqcmp(char *s, char *m)
{
  while(s < nfo.equery) {
    if( !(*m) ) {
      return 0;
    } else {
      if( (*s) < (*m) ) {
	return -1;
      } else if( (*s) > (*m) ) {
	return 1;
      } else {
	s++;
	m++;
      }
    }
  }
  if( (*m) ) {
    return 1;
  } else {
    return 0;
  }
}


static int Sequence_Length(char *seq)
{
  char *p = seq;

  if( seq >= nfo.equery ) {
    return 0;
  }

  // Look forward until end of sequences are found or
  // new sequence start sequence is found.
  do {
    for(p++; (p < nfo.equery) && (*p != '@'); p++);
    if( p >= nfo.equery ) {
      // End of all sequences found
      if( p > seq+1 ) {
	// Some bytes were found before end..
	return nfo.equery-seq;
      } else {
	return 0;
      }
    }
  } while( seqcmp(p,"@<TRIPOS>MOLECULE") );

  // Found a match at p
  return p-seq;
}


// Returns a pointer to the start of the next sequence
static char* Get_Sequence()
{
  char *c;
  int   len;
  
  if( (nfo.cquery >= nfo.equery) || !(len=Sequence_Length(nfo.cquery)) ) {
    // No more queries, return NULL
    return NULL;
  } else {
    c = nfo.cquery;
    // Advance to the next sequence
    nfo.cquery += len;
    // Return the current sequence
    return c;
  }
}


// Opens and maps the input query file
static void Init_Sequences(char *fn)
{
  struct stat statbf;
  int         f;

  // Open and memory map the input sequence file
  if( (f = open(fn,O_RDONLY)) < 0 ) {
    fprintf(stderr,"Could not open() query file. Terminating.\n");
    exit(1);
  }
  if( fstat(f, &statbf) < 0 ) {
    close(f);
    fprintf(stderr,"Could not fstat() opened query file. Terminating.\n");
    exit(1);
  }
  nfo.queries = mmap(NULL,statbf.st_size,PROT_READ,MAP_SHARED,f,0);
  close(f);
  if( nfo.queries == MAP_FAILED ) {
    f = errno;
    fprintf(stderr,"Could not mmap() opened query file.  Terminating. (errno:%d -> %s)\n",
	    f,strerror(f));
    exit(1);
  }

  // Query file is mapped; setup pointers to iterate through the sequences
  nfo.cquery = nfo.queries;
  nfo.equery = nfo.queries+statbf.st_size;
}


////////////////////////////////////////////////////////////////////////////////
//                  Application Entry / High Level Code                       //
////////////////////////////////////////////////////////////////////////////////


int main(int argc, char **argv)
{
  char   *seq, *p, fn[256];
  int     i, j, k, splits;
  size_t  sz, fs;	//fs: file size
  FILE   *f;
  
  // Clear the info struct
  memset(&nfo,0,sizeof(info_t));
  
  // Check command line args
  if( argc != 3 ) {
    fprintf(stderr,"usage:\n\tsplitmol2 <mol2_file> <split_count>\n");
    exit(1);
  }

  // Get split count
  if( sscanf(argv[2],"%d",&splits) != 1 ) {
    fprintf(stderr,"usage:\n\tsplitmol2 <mol2_file> <split_count>\n");
    exit(1);
  }
  
  // Get access to the query sequence file so that we can
  // iterate through the list
  Init_Sequences(argv[1]);

  // Find the size of the file
  {
    struct stat buf;

    if( stat(argv[1], &buf) < 0 ) {
      fprintf(stderr,"Could not stat input file. Terminating.\n");
      exit(1);
    }
    fs = buf.st_size;
  }
  
  // Get a block of sequences to put in output file
  for(i=0,seq=NULL; !i || seq; i++) {
    // Get a block
    for(sz=0; (sz < (fs/splits)) && (seq=Get_Sequence()); sz+=nfo.sequencels[nfo.nsequences-1]) {
      nfo.nsequences++;
      if( !(nfo.sequences = realloc(nfo.sequences,nfo.nsequences*sizeof(char*))) ) {
	fprintf(stderr,"Could not grow sequence pointer array. Terminating.\n");
	exit(1);
      }
      if( !(nfo.sequencels = realloc(nfo.sequencels,nfo.nsequences*sizeof(size_t))) ) {
	fprintf(stderr,"Could not grow sequence pointer array. Terminating.\n");
	exit(1);
      }
      nfo.sequences[nfo.nsequences-1]  = seq;
      nfo.sequencels[nfo.nsequences-1] = Sequence_Length(seq);
    }
    
    // Write the block to the file
    sprintf(fn,"q%d.mol2", i);
    if( !(f=fopen(fn,"w")) ) {
      // Error
      fprintf(stderr,"Could not open \"%s\" for writing. Terminating.\n", fn);
    }
    for(j=0; j<nfo.nsequences; j++) {
      for(k=0; k<nfo.sequencels[j]; k++) {
	fprintf(f,"%c",nfo.sequences[j][k]);
      }
    }
    fclose(f);

    // Get ready for the next block
    nfo.nsequences = 0;
    free(nfo.sequences);
    free(nfo.sequencels);
    nfo.sequences = NULL;
    nfo.sequencels = NULL;
  }

  return 0;
}
