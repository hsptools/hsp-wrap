#ifndef PROCESS_POOL_H__
#define PROCESS_POOL_H__

#include <unistd.h> // pid_t

int process_pool_start (pid_t wrapper_pid, int nproc, char *cmd);

#endif // PROCESS_POOL_H__
