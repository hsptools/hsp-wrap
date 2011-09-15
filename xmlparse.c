#include <stdio.h>
#include <string.h>
#include <expat.h>


////////////////////////////////////////////////////////////////////////////////


typedef struct st_userdata {
  int   depth;
  char *Iteration_query_def;
  char *BlastOutput_query_def;
  char *Hit_id;
  char *Hit_def;
  char *Hsp_evalue;
  char *Hsp_query_from;                                                                                                                                                                                                                
  char *Hsp_query_to;                                                                                                                                                                                                                  
  char *Hsp_hit_from;
  char *Hsp_hit_to;
  char *text;
} userdata_t;


#ifndef iswhite
#define iswhite(c) ( ((c) == ' ') || ((c) == '\t') || ((c) == '\n') )
#endif


////////////////////////////////////////////////////////////////////////////////


char* ClipID(char *src, int force)
{
  char *id=NULL,c;
  int   i;


  // Check to see if there is a portion to clip around
  if( force && !strstr(src,"|") ) {
    if( !(id=strdup("")) ) {
      fprintf(stderr,"xmlparse: could not duplicate portion of Hit_id field.\n");
      exit(1);
    }
    return id;
  }

  if( (strlen(src) >= 4) && (src[0] == 'g') && (src[1] == 'i') && (src[2] == '|') ) {
    // Starts with "gi|"
    for(i=3; i<strlen(src)+1; i++) {
      if( (src[i] == '|') || (src[i] == '\0') ) {
	c = src[i];
	src[i] = '\0';
	if( !(id=strdup(src+3)) ) {
	  fprintf(stderr,"xmlparse: could not duplicate portion of Hit_id field.\n");
	  exit(1);
	}
	src[i] = c;
	break;
      }
    }
  } else {
    // Does not start with "gi|"
    for(i=0; i<strlen(src)+1; i++) {
      if( (src[i] == '|') || (src[i] == '\0') ) {
	c = src[i];
	src[i] = '\0';
	if( !(id=strdup(src)) ) {
	  fprintf(stderr,"xmlparse: could not duplicate portion of Hit_id field.\n");
	  exit(1);
	}
	src[i] = c;
	break;
      }
    }
  }

  // Return duplicated/clipped string
  return id;
}


char* ClipOrganism(char *src)
{
  char *org=NULL;
  int   i,s;


  // Search for organism name
  for(i=s=0; i<strlen(src); i++) {
    if( !s && (src[i] == '[') ) {
      // Start of bracketed region
      s = i+1;
    } 
    if( s && (src[i] == ']') ) {
      // End of bracketed region
      if( !(org=malloc(i-s+1)) ) {
	fprintf(stderr,"xmlparse: could not duplicate portion of Hit_def field.\n");
	exit(1);
      }
      memcpy(org,src+s,i-s);
      org[i-s] = '\0';
      break;
    }
  }


  // Org name not found
  if( !org ) {
    if( !(org=strdup("")) ) {
      fprintf(stderr,"xmlparse: could not duplicate portion of Hit_def field.\n");
      exit(1);
    }
  }

  // Return the organism string
  return org;
}


void PrintHit(userdata_t *ud)
{
  char   *protID,*protID1,*organism,altid[1024];
  double  ev;

  // Only print out hits with a high enough score
  if( sscanf(ud->Hsp_evalue,"%lf",&ev) != 1 ) {
    fprintf(stderr,"xmlparse: E-value does not appear to be a number.\n");
    exit(1);
  }
  if( ev > 0.001 ) {
    return;
  }

  // Clip out some info from userdata fields
  if( ud->Iteration_query_def ) {
    protID   = ClipID(ud->Iteration_query_def, 0);
  } else if( ud->BlastOutput_query_def ) {
    sscanf(ud->BlastOutput_query_def,"%s",altid);
    protID = strdup(altid);
  } else {
    fprintf(stderr,"Could not find a name/ID for query sequence.\n");
    return;
  }
  protID1  = ClipID(ud->Hit_id, 1);
  organism = ClipOrganism(ud->Hit_def);
 
  // Print out the data
  printf("%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
	 protID,             protID1,
	 ud->Hsp_query_from, ud->Hsp_query_to,
	 ud->Hsp_hit_from,   ud->Hsp_hit_to,
	 ud->Hsp_evalue,     organism);

  // Cleanup
  free(protID);
  free(protID1);
  free(organism);
}


////////////////////////////////////////////////////////////////////////////////


void textInElement(void *userData, const XML_Char *s, int len)
{
  userdata_t  *ud   = (userdata_t*)userData;
  static char *buf  = NULL;
  static int   blen = 0;
  int          i;


  // Make room if needed
  if( blen <= len ) {
    if( !(buf=realloc(buf,len*2+1)) ) {
      fprintf(stderr,"xmlparse: could not allocate space for XML text.\n");
      exit(1);
    }
    blen = len*2+1;
  }

  // Find white space info (may skip)
  for(i=0; (i<len) && (iswhite(s[i])); i++);

  if( !(i<len) ) {
    // Skip empty lines
    buf[0] = '\0';
  } else {
    // Copy data
    for(i=0; i<len; i++) {
      buf[i] = s[i];
    }
    buf[i] = '\0';
  }

  // Append to text field
  if( ud->text ) {
    i = strlen(ud->text);
  } else {
    i = 0;
  }
  if( !(ud->text=realloc(ud->text,i+strlen(buf)+1)) ) {
    fprintf(stderr,"xmlparse: could not allocate save space for XML text.\n");
    exit(1);
  }
  memcpy(ud->text+i,buf,strlen(buf)+1);
  
}


void startElement(void *userData, const char *name, const char **atts)
{
  userdata_t *ud = (userdata_t*)userData;

  // Any text is no longer valid for this section
  if( ud->text ) {
    free(ud->text);
    ud->text = NULL;
  }

  ud->depth++;
}


void endElement(void *userData, const char *name)
{
  userdata_t *ud = (userdata_t*)userData;


  // See if we need to save the previous block of text
  if( !strcmp(name,"Iteration_query-def") ) {
    if( ud->Iteration_query_def ) {
      free(ud->Iteration_query_def);
    }
    ud->Iteration_query_def = ud->text;
    ud->text = NULL;
  } else if( !strcmp(name,"BlastOutput_query-def") ) {
    if( ud->BlastOutput_query_def ) {
      free(ud->BlastOutput_query_def);
    }
    ud->BlastOutput_query_def = ud->text;;
    ud->text = NULL;
  } else if( !strcmp(name,"Hit_id") ) {
    if( ud->Hit_id ) {
      free(ud->Hit_id);
    }
    ud->Hit_id = ud->text;;
    ud->text = NULL;
  } else if( !strcmp(name,"Hit_def") ) {
    if( ud->Hit_def ) {
      free(ud->Hit_def);
    }
    ud->Hit_def = ud->text;
    ud->text = NULL;
  } else if( !strcmp(name,"Hsp_evalue") ) {
    if( ud->Hsp_evalue ) {
      free(ud->Hsp_evalue);
    }
    ud->Hsp_evalue = ud->text;
    ud->text = NULL;
  } else if( !strcmp(name,"Hsp_query-from") ) {
    if( ud->Hsp_query_from ) {
      free(ud->Hsp_query_from);
    }
    ud->Hsp_query_from = ud->text;
    ud->text = NULL;
  } else if( !strcmp(name,"Hsp_query-to") ) {
    if( ud->Hsp_query_to ) {
      free(ud->Hsp_query_to);
    }
    ud->Hsp_query_to = ud->text;
    ud->text = NULL;
  } else if( !strcmp(name,"Hsp_hit-from") ) {
    if( ud->Hsp_hit_from ) {
      free(ud->Hsp_hit_from);
    }
    ud->Hsp_hit_from = ud->text;
    ud->text = NULL;
  } else if( !strcmp(name,"Hsp_hit-to") ) {
    if( ud->Hsp_hit_to ) {
      free(ud->Hsp_hit_to);
    }
    ud->Hsp_hit_to = ud->text;
    ud->text = NULL;
  } else if( !strcmp(name,"Hit") ){
    // We have ended a hit; print it out.
    PrintHit(ud);
    if( ud->text ) {
      free(ud->text);
    }
    ud->text = NULL;
  } else {
    if( ud->text ) {
      free(ud->text);
    }
    ud->text = NULL;
  }

  // Decrement depth
  ud->depth--;
}


////////////////////////////////////////////////////////////////////////////////


void ClearUD(userdata_t *ud)
{
  if( ud->Iteration_query_def ) {
    free(ud->Iteration_query_def);
  }
  if( ud->Hit_id ) {
    free(ud->Hit_id);
  }
  if( ud->Hit_def ) {
    free(ud->Hit_def);
  }
  if( ud->Hsp_evalue ) {
    free(ud->Hsp_evalue);
  }
  if( ud->Hsp_query_from ) {
    free(ud->Hsp_query_from);
  }
  if( ud->Hsp_query_to ) {
    free(ud->Hsp_query_to);
  }
  if( ud->Hsp_hit_from ) {
    free(ud->Hsp_hit_from);
  }
  if( ud->Hsp_hit_to ) {
    free(ud->Hsp_hit_to);
  }
  if( ud->text ) {
    free(ud->text);
  }
  memset(ud, 0, sizeof(userdata_t));
}


int main(int argc, char **argv)
{
  XML_Parser  parser;
  userdata_t  ud;
  FILE       *f;
  char        buf[BUFSIZ];
  int         start=1,done=0,len,line;


  // Check command line
  if( argc != 2 ) {
    fprintf(stderr,"usage:\n\txmlparse <xmlfile>\n");
    exit(1);
  }
  if( !(f=fopen(argv[1],"r")) ) {
    fprintf(stderr,"xmlparse: Could not open input file: \"%s\".\n",
	    argv[1]);
    exit(1);
  }

  // Parse
  line = 0;
  memset(&ud,     0, sizeof(ud));
  memset(&parser, 0, sizeof(XML_Parser));
  do {
    // Read a line
    if( !fgets(buf,sizeof(buf),f) ) {
      if( feof(f) ) {
	buf[0] = '\0';
	done = 1;
      } else {
	fprintf(stderr,"xmlparse: fread() failed on XML file.\n");
	exit(1);
      }
    }
    line++;

    // Take off trailing newline if needed
    len = strlen(buf);
    if( len && (buf[len-1] == '\n') ) {
      buf[len-1] = '\0';
      len--;
    }

    // If this line is the start of a new XML block (concatenated file)
    // then free the old parser and reinit a new one.
    if( !strcmp(buf,"<?xml version=\"1.0\"?>") ) {
      if( !start ) {
	XML_ParserFree(parser);
      } 
      start = 0;
      parser = XML_ParserCreate(NULL);
      XML_SetUserData(parser, &ud);
      XML_SetElementHandler(parser, startElement, endElement);
      XML_SetCharacterDataHandler(parser, textInElement);
      ClearUD(&ud);
    }

    // Parse the line
    if( !XML_Parse(parser, buf, len, done) ) {
      fprintf(stderr, "%s at parser line %d\n",
	      XML_ErrorString(XML_GetErrorCode(parser)),
	      XML_GetCurrentLineNumber(parser));
      fprintf(stderr,"Global line: %d\n",line);
      fprintf(stderr, "byte: %ld\ncontext: %s\n",
	      ((long int)ftell(f)), buf);
      return 1;
    }
  } while( !done );

  // Cleanup
  XML_ParserFree(parser);
  fclose(f);

  // Return Success
  return 0;
}
