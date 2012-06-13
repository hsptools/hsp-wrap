#include <iostream>
#include <iomanip>
#include <fstream>
#include <set>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdlib.h>
#include <string.h>


using namespace std;

multiset<string> success;
multiset<string> failed;

char *cquery; // Current query
char *equery; // End of queries

////////////////////////////////////////////////////////////////////////////////
//                                Fasta Code (TODO: Reuse this code)          //
////////////////////////////////////////////////////////////////////////////////

// Finds the length of the query sequence seq
static int sequence_length(char *seq)
{
  char *p, *lp;

	// Verify input
	if (seq >= nfo.equery) {
		return 0;
	}

  // Look forward until end of sequences are found or
  // new sequence start '>' is found.
  for (p=seq+1, lp=seq; p<equery; lp = p++) {
    if (*p == '>' && (*lp == '\n' || *lp == '\r'))
      break;
  }

  // Return the diff of start and end
  return ((int)(p-seq));
}


// Returns a pointer to the start of the next sequence
static char* get_sequence()
{
  char *c;
  
  if( cquery >= equery ) {
    // No more queries, return NULL
    return NULL;
  } else {
    c = cquery;
    // Advance to the next sequence
    cquery += sequence_length(cquery);
    // Return the current sequence
    return c;
  }
}


////////////////////////////////////////////////////////////////////////////////
//                             Recovery Code                                  //
////////////////////////////////////////////////////////////////////////////////


static int read_log(const char *fpath)
{
	cout << "Found logfile: " << fpath << endl;

	ifstream is(fpath);

	string l;
	for(int ln=1; getline(is, l); ++ln) {
		if (l.length() >=1) {
			if (l[0] == '#') {
				// Comment, get next line
			}
			else if (l.length() < 5) {
				cerr << "Malformed input on line " << ln << ". Ignoring." << endl;
			}
			else if (l[0] == 'S') {
				success.insert(l.substr(5));
			}
			else if (l[0] == 'E') {
				failed.insert(l.substr(5));
			}
			else {
				cerr << "Malformed input on line " << ln << ". Ignoring." << endl;
			}
		}
	}

	is.close();
  return 0;
}


static int peek_dir(const char *fpath, const struct stat *sb,
                int tflag, struct FTW *ftwbuf)
{
  if (tflag == FTW_F && strcmp(fpath+ftwbuf->base, "log") == 0) {
		read_log(fpath);
	}
  return 0;
}


int main(int argc, char **argv) {
	
	// Load log entries
  nftw((argc == 2) ? "." : argv[2], peek_dir, 100, 0);

  int fd;
  char *queries;
  struct stat st;

	// Open and map input queries
	if ((fd = open(argv[1], O_RDONLY)) < 0) {
    fprintf(stderr,"Could not open() query file. Terminating.\n");
    exit(1);
  }
	
	if (fstat (fd,&st) < 0) {
    close(fd);
    fprintf(stderr,"Could not fstat() opened query file. Terminating.\n");
    exit(1);
  }

  queries = (char*)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (queries == MAP_FAILED) {
    fprintf(stderr,"Could not mmap() opened query file. Terminating.\n");
    exit(1);
  }

	// Open output files
	ofstream of_resume("recovery-resume.fasta");
	ofstream of_failed("recovery-failed.fasta");

  // Query file is mapped; setup pointers to iterate through the sequences
  cquery = queries;
  equery = queries+st.st_size;

	// Scan through input file, find and remove from set if exists
  char *seq;
	int nsuccess = 0, nfailed = 0, nresume = 0;
  set<string>::iterator i;
  
	while ((seq = get_sequence())) {
		// Formulate seq_id
		size_t nl_pos = string(seq).find('\n');
		if (nl_pos != string::npos) {
			nl_pos = 32;
		}
		string seq_id(seq, nl_pos);

		i = success.find(seq_id);
		if (i != success.end()) {
      // Successful, remove from list of outstanding sequences
      success.erase(i);
			++nsuccess;
    }
    else {
      // Wasn't successful, perhaps it failed
      i = failed.find(seq_id);
      if (i != failed.end()) {
        // Failed, report and remove
				of_failed.write(seq, sequence_length(seq));
        failed.erase(i);
				++nfailed;
      }
      else {
        // Didn't fail either, must have never been reached. Report.
				of_resume.write(seq, sequence_length(seq));
				++nresume;
      }
			// TODO: Do we need to worry about the case that a sequence doesn't have a newline at the end?
    }
		cout << "Successful: " << setw(6) << nsuccess 
			   << " Failed: " << setw(6) << nfailed
				 << " Resumable: " << setw(6) << nresume
				 << '\r';
	}

	of_resume.close();
	of_failed.close();

	cout << endl;
  return 0;
}
