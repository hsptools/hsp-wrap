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


// I want to know which bin each sequence is in.
// Actually, I want to know which sequences are in each bin (faster).
typedef struct {
  int     count;
  char  **seqs;
} dist_bin_t;


////////////////////////////////////////////////////////////////////////////////
//                                Fasta Code                                  //
////////////////////////////////////////////////////////////////////////////////

static int AA_Count(char *seq)
{
  char *p;
  int   nl,aa;

  // Look forward until end of sequences are found or
  // new sequence start '>' is found.
  for(nl=aa=0,p=seq+1; (p<nfo.equery) && (*p != '>'); p++) {
    if( (*p == '\n') || (*p == '\r') ) {
      nl = 1;
    } else if( nl && (*p != ' ') && (*p != '\t') ) {
      aa++;
    }
  }
  
  // Return the diff of start and end
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
  // Advance to the next sequence
  nfo.cquery += Sequence_Length(nfo.cquery);

  if( nfo.cquery >= (nfo.equery-1) ) {
    // No more queries, return NULL
    return NULL;
  } else {
    // Return the current sequence
    return nfo.cquery;
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


void UsageError()
{
  fprintf(stderr,"Usage:\n\tfastadist <fasta_file> [<split_stride> <split_height> <output_file> <max_out_len>].\n");
  exit(1);
}


int main(int argc, char **argv)
{
  char       *seq,*outfn;
  int         i,j,aac,mlen,max;
  int         stride,height,count,maxoutlen;
  dist_bin_t *dist;
  double      p;
  FILE       *of=NULL;


  // Clear the info struct
  memset(&nfo,0,sizeof(info_t));
  
  // Check command line args
  if( (argc != 2) && (argc != 6) ) {
    UsageError();
  }
  if( argc == 6 ) {
    // Splitting will be done after dist is built.
    // Parse those before building the dist;
    // early error detection is a good thing.
    if( sscanf(argv[2],"%d",&stride) != 1 ) {
      UsageError();
    }
    if( sscanf(argv[3],"%d",&height) != 1 ) {
      UsageError();
    }
    if( sscanf(argv[5],"%d",&maxoutlen) != 1 ) {
      UsageError();
    }
    outfn = argv[4];
    if( !(of=fopen(outfn,"w")) ) {
      fprintf(stderr,"Could not open output file for writing. Terminating.\n");
      exit(1);
    }
  }

  // Get access to the query sequence file.
  Init_Sequences(argv[1]);

  // Build array of pointers to the sequences
  while( (seq = Get_Sequence()) ) {
    nfo.nsequences++;
    if( !(nfo.sequences = realloc(nfo.sequences,nfo.nsequences*sizeof(char*))) ) {
      fprintf(stderr,"Could not grow sequence pointer array. Terminating.\n");
      exit(1);
    }
    nfo.sequences[nfo.nsequences-1] = seq;
  }

  // Build an array for dist
  for(max=mlen=i=0,dist=NULL; i<nfo.nsequences; i++) {
    // Find sequence length
    // !!av: Do I really want total length?
    //       AA count would be a much better thing to use here...
    if( !(aac=AA_Count(nfo.sequences[i])) ) {
      fprintf(stderr,"NULL sequence found. Terminating.\n");
      exit(1);
    }
    // Expand array size if needed
    if( aac > mlen ) {
      if( !(dist=realloc(dist,aac*sizeof(dist_bin_t))) ) {
	fprintf(stderr,"Could not grow dist array. Terminating.\n");
	exit(1);
      }
      memset(dist+mlen,0,(aac-mlen)*sizeof(dist_bin_t));
      mlen = aac;
    }
    // Add to count in dist
    if( (++(dist[aac-1].count)) > max ) {
      max = dist[aac-1].count;
    }
    // Add to array in dist
    if( !(dist[aac-1].seqs = realloc(dist[aac-1].seqs,dist[aac-1].count*sizeof(char*))) ) {
      fprintf(stderr,"Could not grow dist sequence pointer array. Terminating.\n");
      exit(1);
    }
    dist[aac-1].seqs[dist[aac-1].count-1] = nfo.sequences[i];
  }

  // Print out distribution array
  for(i=0; i<mlen; i++) {
    if( dist[i].count ) { 
      fprintf(stderr,"%d %d %f\n",i,dist[i].count,dist[i].count/((float)max));
    }
  }

  // Now that we have a distribution, we can use that to 
  // extract a subset of the input sequences, with a similar
  // distribution of sequence lengths, but with a smaller
  // number of sequences.
  if( argc == 6 ) {
    // Select a number of sequences from every <stride> bin of
    // the distribution, proportional to the number of sequences
    // in each selected bin, such that the number of sequences
    // taken from the largest bin will be <height>.
    for(i=0; (i<mlen) && (i<maxoutlen); i++) {
      if( !(i%stride) ) {
	// Find proportion (compared to max)
	p     = dist[i].count / ((double)max);
	count = p * height;
	// Just select the first count many seqs from this bin.
	for(j=0; (j<count) && (j<dist[i].count); j++) {
	  for(seq=dist[i].seqs[j]+1; (seq<=nfo.equery) && (*seq != '>'); seq++) {
	    if( seq == dist[i].seqs[j]+1 ) {
	      fprintf(of,">");
	    }
	    fprintf(of,"%c",*seq);
	  }
	}
      }
    }
    fclose(of);
  }
  

  return 0;
}
