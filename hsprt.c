#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>

/* Close the file descriptor FD.  */
int
close (int fd)
{
  static int (*real_close)(int);
  if (!real_close) {
    real_close = (int(*)(int)) dlsym(RTLD_NEXT, "close");
  }

  printf("HSPRT close: (fd %d)\n", fd);
  return real_close(fd);
}

/* Open FILE and return a new file descriptor for it, or -1 on error.
   OFLAG determines the type of access used.  If O_CREAT is on OFLAG,
   the third argument is taken as a `mode_t', the mode of the created file.  */
int
open (const char *file, int oflag, ...)
{
  static int (*real_open)(const char *, int, ...);
  if (!real_open) {
    real_open = (int(*)(const char *, int, ...)) dlsym(RTLD_NEXT, "open");
  }

  va_list argp;
  int fd, mode;

  if (oflag & O_CREAT) {
    va_start(argp, oflag);
    mode = va_arg(argp, int);
    va_end(argp);

    fd = real_open(file, oflag, mode);
  } else {
    fd = real_open(file, oflag);
  }
  
  printf("HSPRT open: %s (fd %d)\n", file, fd);
  return fd;
}

/* Read NBYTES into BUF from FD.  Return the
   number read, -1 for errors or 0 for EOF.  */
ssize_t
read (int fd, void *buf, size_t nbytes)
{
  static ssize_t (*real_read)(int, void *, size_t);
  if (!real_read) {
    real_read = (ssize_t(*)(int, void *, size_t)) dlsym(RTLD_NEXT, "read");
  }

  printf("HSPRT read: (fd %d)\n", fd);
  return real_read(fd, buf, nbytes);
}

/* Write N bytes of BUF to FD.  Return the number written, or -1. */
ssize_t
write (int fd, const void *buf, size_t nbytes)
{
  static ssize_t (*real_write)(int, const void *, size_t);
  if (!real_write) {
    real_write = (ssize_t(*)(int, const void *, size_t)) dlsym(RTLD_NEXT, "write");
  }

  printf("HSPRT write: (fd %d)\n", fd);
  return real_write(fd, buf, nbytes);
}



// TODO
#if 0 
#ifdef __USE_LARGEFILE64
extern int open64 (__const char *file, int oflag, ...);
#endif

/* Test for access to NAME using the real UID and real GID.  */
extern int access (__const char *name, int type);

/* Move FD's file position to OFFSET bytes from the
   beginning of the file (if WHENCE is SEEK_SET),
   the current position (if WHENCE is SEEK_CUR),
   or the end of the file (if WHENCE is SEEK_END).
   Return the new file position.  */
#ifndef __USE_FILE_OFFSET64
extern __off_t lseek (int fd, __off_t offset, int whence);
#else
extern __off64_t lseek (int fd, __off64_t offset, int whence);
#endif
#ifdef __USE_LARGEFILE64
extern __off64_t lseek64 (int fd, __off64_t offset, int whence);
#endif

/* Create and open FILE, with mode MODE.  This takes an `int' MODE
   argument because that is what `mode_t' will be widened to.

   This function is a cancellation point and therefore not marked with
   __THROW.  */
extern int creat (__const char *file, mode_t mode);
#ifdef __USE_LARGEFILE64
extern int creat64 (__const char *file, mode_t mode);
#endif

/* Return information about the filesystem on which FILE resides.  */
#ifndef __USE_FILE_OFFSET64
extern int statfs (__const char *file, struct statfs *buf);
#else
extern int statfs (__const char *file, struct statfs64 *buf);
#endif
#ifdef __USE_LARGEFILE64
extern int statfs64 (__const char *file, struct statfs64 *buf);
#endif

/* Return information about the filesystem containing the file FILDES
   refers to.  */
#ifndef __USE_FILE_OFFSET64
extern int fstatfs (int fildes, struct statfs *buf);
#else
extern int fstatfs (int fildes, struct statfs64 *buf);
#endif
#ifdef __USE_LARGEFILE64
extern int fstatfs64 (int fildes, struct statfs64 *buf);
#endif
	
#ifndef __USE_FILE_OFFSET64
extern int __fxstat (int ver, int fildes, struct stat *stat_buf);
extern int __xstat (int ver, __const char *filename, struct stat *stat_buf);
extern int __lxstat (int ver, __const char *filename, struct stat *stat_buf);
extern int __fxstatat (int ver, int fildes, __const char *filename, struct stat *stat_buf, int flag);
#else
extern int __fxstat64 (int ver, int fildes, struct stat64 *stat_buf);
extern int __xstat64 (int ver, __const char *filename, struct stat64 *stat_buf);
extern int __lxstat64 (int ver, __const char *filename, struct stat64 *stat_buf);
extern int __fxstatat64 (int ver, int fildes, __const char *filename, struct stat64 *stat_buf, int flag);
#endif
#ifdef __USE_LARGEFILE64
extern int __fxstat64 (int ver, int fildes, struct stat64 *stat_buf);
extern int __xstat64 (int ver, __const char *filename, struct stat64 *stat_buf);
extern int __lxstat64 (int ver, __const char *filename, struct stat64 *stat_buf);
extern int __fxstatat64 (int ver, int fildes, __const char *filename, struct stat64 *stat_buf, int flag);
#endif

#endif 

// TODO: Directory interface from dirent.h

////////////////////////////////////////////////////
// Things we won't care about for quite some time.
////////////////////////////////////////////////////
#if 0

#ifdef __USE_ATFILE
/* Test for access to FILE relative to the directory FD is open on.
   If AT_EACCESS is set in FLAG, then use effective IDs like `eaccess',
   otherwise use real IDs like `access'.  */
extern int faccessat (int fd, __const char *file, int type, int flag);

/* Similar to `open' but a relative path name is interpreted relative to
   the directory for which FD is a descriptor.

   NOTE: some other `openat' implementation support additional functionality
   through this interface, especially using the O_XATTR flag.  This is not
   yet supported here.*/
extern int openat (int fd, __const char *file, int oflag, ...);
# ifdef __USE_LARGEFILE64
extern int openat64 (int fd, __const char *file, int oflag, ...)
     __nonnull ((2));
# endif
#endif

/* Do the file control operation described by CMD on FD.
   The remaining arguments are interpreted depending on CMD.  */
extern int fcntl (int fd, int cmd, ...);

/* Change the owner and group of FILE.  */
extern int chown (__const char *file, __uid_t owner, __gid_t group);

#if defined __USE_BSD || defined __USE_XOPEN_EXTENDED || defined __USE_XOPEN2K8
/* Change the owner and group of the file that FD is open on.  */
extern int fchown (int fd, __uid_t owner, __gid_t group);


/* Change owner and group of FILE, if it is a symbolic
   link the ownership of the symbolic link is changed.  */
extern int lchown (__const char *file, __uid_t owner, __gid_t group);

#endif /* Use BSD || X/Open Unix.  */

#ifdef __USE_ATFILE
/* Change the owner and group of FILE relative to the directory FD is open
   on.  */
extern int fchownat (int fd, __const char *file, __uid_t owner,
		     __gid_t group, int flag);
#endif /* Use GNU.  */

/* Change the process's working directory to PATH.  */
extern int chdir (__const char *path);

#if defined __USE_BSD || defined __USE_XOPEN_EXTENDED || defined __USE_XOPEN2K8
/* Change the process's working directory to the one FD is open on.  */
extern int fchdir (int fd);
#endif

/* Get the pathname of the current working directory,
   and put it in SIZE bytes of BUF.  Returns NULL if the
   directory couldn't be determined or SIZE was too small.
   If successful, returns BUF.  In GNU, if BUF is NULL,
   an array is allocated with `malloc'; the array is SIZE
   bytes long, unless SIZE == 0, in which case it is as
   big as necessary.  */
extern char *getcwd (char *buf, size_t __size);

#ifdef	__USE_GNU
/* Test for access to NAME using the effective UID and GID
   (as normal file operations use).  */
extern int euidaccess (__const char *name, int type);

/* An alias for `euidaccess', used by some other systems.  */
extern int eaccess (__const char *name, int type);

/* Return a malloc'd string containing the current directory name.
   If the environment variable `PWD' is set, and its value is correct,
   that value is used.  */
extern char *get_current_dir_name (void);

/* Make all changes done to all files on the file system associated
   with FD actually appear on disk.  */
extern int syncfs (int fd);
#endif

#if defined __USE_UNIX98 || defined __USE_XOPEN2K8
# ifndef __USE_FILE_OFFSET64
/* Read NBYTES into BUF from FD at the given position OFFSET without
   changing the file pointer.  Return the number read, -1 for errors
   or 0 for EOF.  */
extern ssize_t pread (int fd, void *buf, size_t nbytes, __off_t offset);

/* Write N bytes of BUF to FD at the given position OFFSET without
   changing the file pointer.  Return the number written, or -1.  */
extern ssize_t pwrite (int fd, __const void *buf, size_t n, __off_t offset);
# else
extern ssize_t pread (int fd, void *buf, size_t nbytes, __off64_t offset);
extern ssize_t pwrite (int fd, __const void *buf, size_t n, __off64_t offset);
# endif
# ifdef __USE_LARGEFILE64
extern ssize_t pread64 (int fd, void *buf, size_t nbytes, __off64_t offset);
extern ssize_t pwrite64 (int fd, __const void *buf, size_t n, __off64_t offset);
# endif
#endif


#if (defined __USE_XOPEN_EXTENDED && !defined __USE_XOPEN2K8) \
    || defined __USE_BSD
/* Put the absolute pathname of the current working directory in BUF.
   If successful, return BUF.  If not, put an error message in
   BUF and return NULL.  BUF should be at least PATH_MAX bytes long.  */
extern char *getwd (char *buf);
#endif

#if defined __USE_BSD || defined __USE_XOPEN || defined __USE_XOPEN2K
/* Make all changes done to FD actually appear on disk.  */
extern int fsync (int fd);
#endif /* Use BSD || X/Open || Unix98.  */

#if defined __USE_BSD || defined __USE_XOPEN_EXTENDED
/* Make all changes done to all files actually appear on disk.  */
extern void sync (void) __THROW;
#endif

#if defined __USE_BSD || defined __USE_XOPEN_EXTENDED || defined __USE_XOPEN2K8
/* Truncate FILE to LENGTH bytes.  */
# ifndef __USE_FILE_OFFSET64
extern int truncate (__const char *file, __off_t length);
# else
extern int truncate (__const char *file, __off64_t length);
# endif
# ifdef __USE_LARGEFILE64
extern int truncate64 (__const char *file, __off64_t length);
# endif
#endif

#if defined __USE_BSD || defined __USE_XOPEN_EXTENDED || defined __USE_XOPEN2K
/* Truncate the file FD is open on to LENGTH bytes.  */
# ifndef __USE_FILE_OFFSET64
extern int ftruncate (int fd, __off_t length);
# else
extern int ftruncate (int fd, __off64_t length);
# ifdef __USE_LARGEFILE64
extern int ftruncate64 (int fd, __off64_t length);
# endif
#endif

#if defined __USE_MISC || defined __USE_XOPEN_EXTENDED
# ifndef __USE_FILE_OFFSET64
extern int lockf (int fd, int cmd, __off_t len);
# else
extern int lockf (int fd, int cmd, __off64_t len);
# endif
# ifdef __USE_LARGEFILE64
extern int lockf64 (int fd, int cmd, __off64_t len);
# endif
#endif

#if defined __USE_POSIX199309 || defined __USE_UNIX98
/* Synchronize at least the data part of a file with the underlying
   media.  */
extern int fdatasync (int fildes);
#endif /* Use POSIX199309 */

#ifdef __USE_XOPEN2K
/* Advice the system about the expected behaviour of the application with
   respect to the file associated with FD.  */
# ifndef __USE_FILE_OFFSET64
extern int posix_fadvise (int fd, __off_t offset, __off_t len, int advise);
# else
extern int posix_fadvise (int fd, __off64_t offset, __off64_t len, int advise);
# endif
# ifdef __USE_LARGEFILE64
extern int posix_fadvise64 (int fd, __off64_t offset, __off64_t len, int advise);
# endif


/* Reserve storage for the data of the file associated with FD. */
# ifndef __USE_FILE_OFFSET64
extern int posix_fallocate (int fd, __off_t offset, __off_t len);
# else
extern int posix_fallocate (int fd, __off64_t offset, __off64_t len);
# endif
# ifdef __USE_LARGEFILE64
extern int posix_fallocate64 (int fd, __off64_t offset, __off64_t len);
# endif
#endif

#endif // stray endif somewhere above
#endif // Stuff we don't need
