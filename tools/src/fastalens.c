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


static int IsAA(char c)
{
  if( ((c >= 'A') && (c <= 'Z')) ||
      ((c >= 'a') && (c <= 'z'))    ) {
    return 1;
  } else {
    return 0;
  }
}


// Returns a new string as copy of ID in seq
static char* GetID(char *seq)
{
  char buf[1024];

  // Ignore some common invalids
  if( !seq || (seq >= nfo.equery) || (*seq != '>') ) {
    return NULL;
  }

  // Read first word; works for now, later versions
  // may need some more complicated logic.
  if( sscanf(seq+1,"%s",buf) != 1 ) {
    return NULL;
  }

  // Return a copy of the word
  return strdup(buf);
}


// Returns total number of AA in seq
static int CountAA(char *seq)
{
  char *p;
  int   aa=-1;
  
  // Look forward until end of sequences are found or
  // new sequence start '>' is found.
  for(p=seq+1; (p<nfo.equery) && (*p != '>'); p++) {
    // Only start counting after comment/id line.
    if( (aa == -1) && (*p == '\n') ) {
      aa = 0;
    }
    // If AA and past comment line, add to count.
    if( (aa >= 0) && IsAA(*p) ) {
      aa++;
    }
  }

  // Return aa count
  return aa;
}


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
//                  Application Entry / High Level Code                       //
////////////////////////////////////////////////////////////////////////////////


int main(int argc, char **argv)
{
  char *seq,*id;
  int   i;

  // Clear the info struct
  memset(&nfo,0,sizeof(info_t));

  // Check command line args
  if( argc != 2 ) {
    fprintf(stderr,"Bad command line; expecting query file. Terminating.\n");
    exit(1);
  }

  // Get access to the query sequence file so that we can
  // iterate through the list
  Init_Sequences(argv[1]);

  // For each sequence in fasta file
  for(i=0; (seq = Get_Sequence()); i++) {
    // Print the ID name in column 1 and the sequence length in column 2
    id = GetID(seq);
    printf("%s\t%d\n",id,CountAA(seq));
    free(id);
  }

  // Return success
  return 0;
}
