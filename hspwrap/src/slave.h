#ifndef SLAVE_H__
#define SLAVE_H__

void slave_init (int slave_idx, int nslaves, int nprocesses);
void slave_broadcast_file (const char *path);
int  slave_main (const char *cmd);

#endif // SLAVE_H__
