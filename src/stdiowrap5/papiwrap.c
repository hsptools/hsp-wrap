#include <stdio.h>
#include <stdlib.h>
#include <papi.h>
#include "papiwrap.h"
 
#define NUM_EVENTS 3


// These were remapped by stdiowrap (if in use);
// I don't want them wrapped any longer atm.
#undef fprintf
#undef fopen
#undef fclose


void StartPAPI()
{
  char errstring[PAPI_MAX_STR_LEN];
  int  Events[NUM_EVENTS] = {PAPI_FP_OPS, PAPI_TOT_INS, PAPI_TLB_DM};
  int  num_hwcntrs = 0;
  int  retval;


  // Init PAPI
  if((retval = PAPI_library_init(PAPI_VER_CURRENT)) != PAPI_VER_CURRENT ) {
    fprintf(stderr, "StartPAPI(): Error: %d %s\n",retval, errstring);
    exit(1);
  }
  if((num_hwcntrs = PAPI_num_counters()) < PAPI_OK) {
    fprintf(stderr,"StartPAPI(): There are no counters available. \n");
    exit(1);
  }
  if( num_hwcntrs < NUM_EVENTS ) {
    fprintf(stderr,"StartPAPI(): There are %d counters in this system; too many events: %d.\n",
	    num_hwcntrs,NUM_EVENTS);
    exit(1);
  }

  // Start Counters
  if( (retval = PAPI_start_counters(Events, NUM_EVENTS)) != PAPI_OK ) {
    fprintf(stderr,"StartPAPI(): Error starting counters.\n");
    exit(1);
  }
}


void StopPAPI()
{
  long long values[NUM_EVENTS];
  int       retval;

  // Stop the counters and get the values
  if( (retval=PAPI_stop_counters(values, NUM_EVENTS)) != PAPI_OK ) {
    fprintf(stderr,"StopPAPI(): Error stopping counters.\n");
    exit(1);
  }

  // Ouput the values.  For now, just append to a file
  // that is local to our worker.
  {
    char *tc,profname[1024];
    FILE *f;

    if( !(tc=getenv("MCW_WID")) ) {
      fprintf(stderr, "StopPAPI(): Could not read MCW_WID env var.\n");
      exit(1);
    }
    sprintf(profname, "%s-papi.txt", tc);
    if( !(f=fopen(profname,"a")) ) {
      fprintf(stderr, "StopPAPI(): Could not open output profile file for appending.\n");
      exit(1);
    }
    fprintf(f,"PAPI_FP_OPS %lld\n",  values[0]);
    fprintf(f,"PAPI_TOT_INS %lld\n", values[1]);
    fprintf(f,"PAPI_TLB_DM %lld\n",  values[2]);
    fclose(f);
  }
}



