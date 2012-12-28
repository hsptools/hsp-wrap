#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
int
main() {
	pid_t child;
	long orig_eax;
	child = fork();
	if(child == 0) {
		ptrace(PTRACE_TRACEME, 0, NULL, NULL);
		execl("/bin/ls", "ls", NULL);
	} else {
		//wait(NULL);
		if (ptrace(PTRACE_SINGLESTEP, child, 0, 0) != 0) {
			perror("ptrace\n");
		}
		printf("The child made a system call.\n");
		ptrace(PTRACE_CONT, child, NULL, NULL);
	}
	return 0;
}
