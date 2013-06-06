#ifndef MASTER_H__
#define MASTER_H__

void master_init ();
void master_broadcast_file (const char *path);
int  master_main (int nslaves);

#endif // MASTER_H__
