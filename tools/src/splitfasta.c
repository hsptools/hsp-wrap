#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>


////////////////////////////////////////////////////////////////////////////////
//                              Types and Defines                             //
////////////////////////////////////////////////////////////////////////////////


// Information the master needs (presistent)
typedef struct st_info {
  char       *queries;      // Pointer to start of input queries
  char       *cquery;       // Current input query
  char       *equery;       // One byte past last valid byte of query
  char      **sequences;    // Array of sorted (at some point) inputs
  int         nsequences;   // Number of known sequences
} info_t;


// This is a small until; I'll just make this a global
info_t nfo;


////////////////////////////////////////////////////////////////////////////////
//                                Fasta Code                                  //
////////////////////////////////////////////////////////////////////////////////


// Finds the length of the query sequence seq
static int Sequence_Length(char *seq)
{
  char *p;

  // Look forward until end of sequences are found or
  // new sequence start '>' is found.
  for(p=seq+1; (p<nfo.equery) && (*p != '>'); p++);
  
  // Return the diff of start and end
  return ((int)(p-seq));
}


// Returns a pointer to the start of the next sequence
static char* Get_Sequence()
{
  char *c;
  
  if( nfo.cquery >= nfo.equery ) {
    // No more queries, return NULL
    return NULL;
  } else {
    c = nfo.cquery;
    // Advance to the next sequence
    nfo.cquery += Sequence_Length(nfo.cquery);
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
  nfo.queries = mmap(NULL,statbf.st_size,PROT_READ,MAP_PRIVATE,f,0);
  close(f);
  if( nfo.queries == MAP_FAILED ) {
    fprintf(stderr,"Could not mmap() opened query file. Terminating.\n");
    exit(1);
  }

  // Query file is mapped; setup pointers to iterate through the sequences
  nfo.cquery = nfo.queries;
  nfo.equery = nfo.queries+statbf.st_size;
}


////////////////////////////////////////////////////////////////////////////////


static int Compare_Sequences_Length(const void *a, const void *b)
{
  char *seqa = *((char**)a);
  char *seqb = *((char**)b);
  int   lena = Sequence_Length(seqa);
  int   lenb = Sequence_Length(seqb);
  
  if( lena < lenb ) {
    return 1;
  } else if( lena > lenb ) {
    return -1;
  } else {
    return 0;
  }
}


////////////////////////////////////////////////////////////////////////////////
//                  Application Entry / High Level Code                       //
////////////////////////////////////////////////////////////////////////////////


int main(int argc, char **argv)
{
  char   *seq,*p,fn[256];
  size_t  i,j,sz,fs,splits;
  FILE   *f;

  // Clear the info struct
  memset(&nfo,0,sizeof(info_t));

  // Check command line args
  if( argc != 3 ) {
    fprintf(stderr,"Bad command line; expecting query file and split count. Terminating.\n");
    exit(1);
  }

  // Get split count
  if( sscanf(argv[2],"%d",&splits) != 1 ) {
    fprintf(stderr,"Bad command line; could not parse split count. Terminating.\n");
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
    fs = (int)buf.st_size;
  }

  
  // Get a block of sequences to put in output file
  for(i=0,seq=NULL; !i || seq; i++) {

    // Get a block
    for(sz=0; (sz < (fs/splits)) && (seq=Get_Sequence()); sz+=Sequence_Length(seq)) {
      nfo.nsequences++;
      if( !(nfo.sequences = realloc(nfo.sequences,nfo.nsequences*sizeof(char*))) ) {
	fprintf(stderr,"Could not grow sequence pointer array. Terminating.\n");
	exit(1);
      }
      nfo.sequences[nfo.nsequences-1] = seq;
    }
    
    // Write the block to the file
    sprintf(fn,"q%d.fasta",i);
    if( !(f=fopen(fn,"w")) ) {
      // Error
    }
    for(j=0; j<nfo.nsequences; j++) {
      for(p=nfo.sequences[j]; (p<nfo.equery) && ( (p == nfo.sequences[j]) || (*p != '>') ); p++) {
	fprintf(f,"%c",*p);
      }
    }
    fclose(f);

    // Get ready for the next block
    nfo.nsequences = 0;
    free(nfo.sequences);
    nfo.sequences = NULL;
  }

  return 0;
}
