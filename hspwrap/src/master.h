#ifndef MASTER_H__
#define MASTER_H__

void master_init ();
ssize_t master_broadcast_file (const char *path);
int  master_main (int nslaves);

#endif // MASTER_H__
