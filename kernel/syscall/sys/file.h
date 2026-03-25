#ifndef _SYS_FILE
#define _SYS_FILE

#include <kernel/syscall/sys/types.h>
#include <kernel/syscall/sys/timespec.h>

#define	_FOPEN		(-1)	/* from sys/file.h, kernel use only */
#define	_FREAD		0x0001	/* read enabled */
#define	_FWRITE		0x0002	/* write enabled */
#define	_FAPPEND	0x0008	/* append (writes guaranteed at the end) */
#define	_FMARK		0x0010	/* internal; mark during gc() */
#define	_FDEFER		0x0020	/* internal; defer for next gc pass */
#define	_FASYNC		0x0040	/* signal pgrp when data ready */
#define	_FSHLOCK	0x0080	/* BSD flock() shared lock present */
#define	_FEXLOCK	0x0100	/* BSD flock() exclusive lock present */
#define	_FCREAT		0x0200	/* open with file create */
#define	_FTRUNC		0x0400	/* open with truncation */
#define	_FEXCL		0x0800	/* error on open if file exists */
#define	_FNBIO		0x1000	/* non blocking I/O (sys5 style) */
#define	_FSYNC		0x2000	/* do all writes synchronously */
#define	_FNONBLOCK	0x4000	/* non blocking I/O (POSIX style) */
#define	_FNDELAY	_FNONBLOCK	/* non blocking I/O (4.2 style) */
#define	_FNOCTTY	0x8000	/* don't assign a ctty on this open */
#define	_FNOINHERIT	0x40000
#define	_FDIRECT	0x80000
#define	_FNOFOLLOW	0x100000
#define	_FDIRECTORY	0x200000
#define	_FEXECSRCH	0x400000

#define S_IFMT   0xF000

#define S_IFREG  0x8000   /* Regular file */
#define S_IFDIR  0x4000   /* Directory */
#define S_IFCHR  0x2000   /* Character device */
#define S_IFBLK  0x6000   /* Block device */
#define S_IFIFO  0x1000   /* FIFO (named pipe) */
#define S_IFLNK  0xA000   /* Symbolic link */
#define S_IFSOCK 0xC000   /* Socket */

#define S_ISUID  0x0800   /* Set user ID on execution */
#define S_ISGID  0x0400   /* Set group ID on execution */
#define S_ISVTX  0x0200   /* Sticky bit */

#define S_IRWXU  0x01C0   /* Owner: rwx */
#define S_IRUSR  0x0100   /* Owner: read */
#define S_IWUSR  0x0080   /* Owner: write */
#define S_IXUSR  0x0040   /* Owner: execute */

#define S_IRWXG  0x0038   /* Group: rwx */
#define S_IRGRP  0x0020   /* Group: read */
#define S_IWGRP  0x0010   /* Group: write */
#define S_IXGRP  0x0008   /* Group: execute */

#define S_IRWXO  0x0007   /* Other: rwx */
#define S_IROTH  0x0004   /* Other: read */
#define S_IWOTH  0x0002   /* Other: write */
#define S_IXOTH  0x0001   /* Other: execute */

#define	O_RDONLY	0		/* +1 == FREAD */
#define	O_WRONLY	1		/* +1 == FWRITE */
#define	O_RDWR		2		/* +1 == FREAD|FWRITE */
#define	O_APPEND	_FAPPEND
#define	O_CREAT		_FCREAT
#define	O_TRUNC		_FTRUNC
#define	O_EXCL		_FEXCL
#define O_SYNC		_FSYNC
#define O_NONBLOCK _FNDELAY
#define O_NOCTTY _FNOCTTY

#define POLLIN     0x0001
#define POLLOUT    0x0004
#define POLLERR    0x0008
#define POLLHUP    0x0010
#define POLLNVAL   0x0020

struct stat
{
    dev_t		st_dev;
    ino_t		st_ino;
    mode_t	st_mode;
    nlink_t	st_nlink;
    uid_t		st_uid;
    gid_t		st_gid;
    dev_t		st_rdev;
    off_t		st_size;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    blksize_t     st_blksize;
    blkcnt_t	st_blocks;
};

#endif
