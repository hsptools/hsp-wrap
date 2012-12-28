#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#define NUM_PROCS 8
#define NUM_JOBS  1000

void print_tod()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	printf("[%ld.%06ld] ", tv.tv_sec, tv.tv_usec);
}

int main(int argc, char **argv)
{
	int forked = 0;
	int st;
	int j;

	for (j=0; j<NUM_JOBS; ++j) {
		
		pid_t pid = fork();
		if (pid == 0) {
			// Child 
			execl("/bin/echo", "echo", "Hello World!", NULL);
		}
		else if (pid > 0) {
			// Parent
			print_tod();
			printf("Child process %d started.\n", pid);

			if (++forked == NUM_PROCS) {
				wait(&st);
				print_tod();
				printf("Proc exited with status %d.\n", st);
				--forked;
			}
		}
		else {
			// Error
		}
	}

	// Done issuing jobs, wait for outstanding procs
	for(; forked>0; --forked) {
		wait(&st);
		print_tod();
		printf("Proc exited with status %d. (winding down)\n", st);
	}

	return 0;
}




