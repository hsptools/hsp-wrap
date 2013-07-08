#ifndef SLAVE_H__
#define SLAVE_H__

void slave_init (int slave_idx, int nslaves, int nprocesses);
ssize_t slave_broadcast_shared_file (const char *path);
ssize_t slave_broadcast_work_file (const char *path);
int  slave_main (const char *cmd);

#endif // SLAVE_H__
