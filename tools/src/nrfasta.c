#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>


////////////////////////////////////////////////////////////////////////////////
//                              Types and Defines                             //
////////////////////////////////////////////////////////////////////////////////


// Information the master needs (presistent)
typedef struct st_info {
  char       *queries;      // Pointer to start of input queries
  char       *cquery;       // Current input query
  char       *equery;       // One byte past last valid byte of query
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
//                  Application Entry / High Level Code                       //
////////////////////////////////////////////////////////////////////////////////


int main(int argc, char **argv)
{
  char   *seq,*p,fn[256];
  int     i,j,splits;
  size_t  sz,fs;
  FILE   *f;
  GHashTable *gis = g_hash_table_new(g_str_hash, g_str_equal);

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

  // Find the size of the file
  {
    struct stat buf;

    if( stat(argv[1], &buf) < 0 ) {
      fprintf(stderr,"Could not stat input file. Terminating.\n");
      exit(1);
    }
    fs = (int)buf.st_size;
  }

  
  // Loop over sequences
  while((seq=Get_Sequence())) {
    char *gi_beg, *gi_end;
    char *gi;

    int i;

    gi_beg = strchr(seq,    '|') + 1;
    gi_end = strchr(gi_beg, '|');
    gi = strndup(gi_beg, gi_end-gi_beg);

    // Report error, or output sequence and remember it
    if (g_hash_table_lookup_extended(gis, gi, NULL, NULL)) {
      fprintf(stderr, "Duplicate found with gid=%s.  Rejecting.\n", gi);
    } else {
      g_hash_table_replace(gis, gi, gi);
      fwrite(seq, 1, Sequence_Length(seq), stdout);
    }
  }

  g_hash_table_destroy(gis);
  return 0;
}
