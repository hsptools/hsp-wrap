#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <syscall.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>


int main(void)
{
  int st;  // Wait return
  int pid;  // Child PID

  switch (pid = fork()) {
    case -1:
      // Error
      perror("fork");
      break;

    case 0:
      // Child
      ptrace(PTRACE_TRACEME, 0, 0, 0);
      execl("/bin/ls", "ls", NULL);
      break;

    default:
      // Parent

      // Wait for ptrace to break initially
      wait(&st); 

      // Wait for syscall (exec)
      if (ptrace(PTRACE_SYSCALL, pid, 0, 0) != 0)
        perror("ptrace");
      wait(&st); 
      printf("At exec()\n");

      // Now wait for first instruction of exec'd program
      if (ptrace(PTRACE_SINGLESTEP, pid, 0, 0) != 0)
        perror("ptrace2");
      wait(&st); 
      printf("At first instruction\n");

      // We are done
      if (ptrace(PTRACE_DETACH, pid, 0, 0) != 0)
        perror("ptrace3");

      printf("Done with safety, waiting for termination...\n");
      wait(&st); 
      printf("Done.\n");

  }
  return 0;
}
