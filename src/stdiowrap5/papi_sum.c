#include <stdlib.h>
#include <stdio.h>
#include <gmp.h>

// e.g. lines like:
//
// PAPI_FP_OPS 3157397
// PAPI_TOT_INS 107851921141
// PAPI_TLB_DM 1720891

int main(int argc, char **argv) 
{
  long long v1,v2,v3;
  mpz_t     papi_fp_ops,papi_tot_ins,papi_tlb_dm;
  char      line1[1024],line2[1024],line3[1024];
  FILE     *f;
  int       i;


  // Check command line args
  if( argc <= 1 ) {
    fprintf(stderr,"usage:\n\tpapi_sum <profile_file_0> <profile_file_1> <...>\n");
    exit(1);
  }

  // Init counters
  mpz_init(papi_fp_ops);
  mpz_init(papi_tot_ins);
  mpz_init(papi_tlb_dm);
  
  // For each input file
  for(i=1; i<argc; i++) {
    // Open input file
    fprintf(stderr,"Processing Input: \"%s\".\n",argv[i]);
    if( !(f=fopen(argv[i],"r")) ) {
      fprintf(stderr,"Could not open input file \"%s\"\n",argv[i]);
      exit(1);
    }
    while(1) {
      // Read three lines at a time
      if( !fgets(line1,sizeof(line1),f) ) {
	if( feof(f) ) {
	  break;
	} else {
	  fprintf(stderr,"Error reading three lines for PAPI_FP_OPS, PAPI_TOT_INS, and PAPI_TLB_DM\n");
	  exit(1);
	}
      }
      if( !fgets(line2,sizeof(line2),f) || !fgets(line3,sizeof(line3),f)    ) {
	fprintf(stderr,"Error reading three lines for PAPI_FP_OPS, PAPI_TOT_INS, and PAPI_TLB_DM\n");
	exit(1);
      }
      // Parse the three lines
      if( sscanf(line1,"PAPI_FP_OPS %lld\n",&v1) != 1 ) {
	fprintf(stderr,"Failed to parse PAPI_FP_OP line.\n");
	exit(1);
      }
      if( sscanf(line2,"PAPI_TOT_INS %lld\n",&v2) != 1 ) {
	fprintf(stderr,"Failed to parse PAPI_TOT_INS line.\n");
	exit(1);
      }
      if( sscanf(line3,"PAPI_TLB_DM %lld\n",&v3) != 1 ) {
	fprintf(stderr,"Failed to parse PAPI_TLB_DM line.\n");
	exit(1);
      }
      // Add to totals
      mpz_add_ui(papi_fp_ops, papi_fp_ops, v1);
      mpz_add_ui(papi_tot_ins, papi_tot_ins, v2);
      mpz_add_ui(papi_tlb_dm, papi_tlb_dm, v3);
    }
    // Close input file
    fclose(f);
  }

  // Report Totals
  printf("PAPI_FP_OPS ");
  mpz_out_str(stdout, 10, papi_fp_ops);
  printf("\nPAPI_TOT_INS ");
  mpz_out_str(stdout, 10, papi_tot_ins);
  printf("\nPAPI_TLB_DM ");
  mpz_out_str(stdout, 10, papi_tlb_dm);
  printf("\n");

  // Return success
  return 0;
}
