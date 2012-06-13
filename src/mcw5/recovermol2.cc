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

#define SPLITSTRING "@<TRIPOS>MOLECULE"

using namespace std;

multiset<string> success;
multiset<string> failed;

char *cquery; // Current query
char *equery; // End of queries

////////////////////////////////////////////////////////////////////////////////
//                                MOL2  Code (TODO: Reuse this code)          //
////////////////////////////////////////////////////////////////////////////////

// Like strcmp, but respects some custom bounds
static int seqcmp(const char *s, const char *m)
{
	while(s < equery) {
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



// Finds the length of the query sequence seq
static int sequence_length(char *seq)
{
	char *p = seq;

	if( seq >= equery ) {
		return 0;
	}

	// Look forward until end of sequences are found or
	// new sequence start sequence is found.
	do {
		for(p++; (p < equery) && (*p != '@'); p++);
		if( p >= equery ) {
			// End of all sequences found
			if( p > seq+1 ) {
				// Some bytes were found before end..
				return equery-seq;
			} else {
				return 0;
			}
		}
	} while( seqcmp(p,"@<TRIPOS>MOLECULE") );

	// Found a match at p
	return p-seq;
}


// Returns a pointer to the start of the next sequence
static char* get_sequence()
{
	char *c;
	int   len;

	if( (cquery >= equery) || !(len=sequence_length(cquery)) ) {
		// No more queries, return NULL
		return NULL;
	} else {
		c = cquery;
		// Advance to the next sequence
		cquery += len;
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
	ofstream of_resume("recovery-resume.mol2");
	ofstream of_failed("recovery-failed.mol2");

	// Query file is mapped; setup pointers to iterate through the sequences
	cquery = queries;
	equery = queries+st.st_size;

	// Scan through input file, find and remove from set if exists
	char *seq;
	int nsuccess = 0, nfailed = 0, nresume = 0, j;
	set<string>::iterator i;

	char log_max, log_id[33];

	while ((seq = get_sequence())) {
		// Formulate seq_id
		log_max = (equery-seq > 32) ? 32 : equery-seq;
		for( j=0; j<log_max; ++j) {
			if( seq[j] == '\n' || seq[j] == '\r' ) {
				log_id[j] = ' ';
			}
			else {
				log_id[j] = seq[j];
			}
		}
		log_id[j] = 0;

		i = success.find(log_id);
		if (i != success.end()) {
			// Successful, remove from list of outstanding sequences
			success.erase(i);
			++nsuccess;
		}
		else {
			// Wasn't successful, perhaps it failed
			i = failed.find(log_id);
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
