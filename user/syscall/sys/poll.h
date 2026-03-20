#ifndef _5HT_SYS_POLL_H
#define _5HT_SYS_POLL_H

#include <stdint.h>
/*
 * We need struct timeval but sys/time.h may also define FD_* macros.
 * Include it, then override below with our own definitions.
 */
#include <sys/time.h>

/* poll() event flags */
#define POLLIN     0x0001
#define POLLOUT    0x0004
#define POLLERR    0x0008
#define POLLHUP    0x0010
#define POLLNVAL   0x0020

/* Maximum number of fds for select/poll (matches kernel FD_MAX) */
#define _5HT_FD_SETSIZE 64

struct pollfd {
    int      fd;
    short    events;
    short    revents;
};

typedef unsigned int nfds_t;

/*
 * Our own fd_set that matches the kernel's layout (64 fds, 2 uint32_t words).
 * We use a 5HT-prefixed type internally and typedef it only if the system
 * headers haven't already provided fd_set.
 */
typedef struct {
    uint32_t bits[_5HT_FD_SETSIZE / 32];
} _5ht_fd_set;

/* Override any existing FD_* macros with our 64-fd versions */
#undef FD_SETSIZE
#define FD_SETSIZE _5HT_FD_SETSIZE

#undef FD_ZERO
#undef FD_SET
#undef FD_CLR
#undef FD_ISSET

#define FD_ZERO(s)     do { _5ht_fd_set *_s = (_5ht_fd_set*)(void*)(s); _s->bits[0] = 0; _s->bits[1] = 0; } while(0)
#define FD_SET(fd,s)   ((_5ht_fd_set*)(void*)(s))->bits[(fd)/32] |=  (1U << ((fd)%32))
#define FD_CLR(fd,s)   ((_5ht_fd_set*)(void*)(s))->bits[(fd)/32] &= ~(1U << ((fd)%32))
#define FD_ISSET(fd,s) (((_5ht_fd_set*)(void*)(s))->bits[(fd)/32] &   (1U << ((fd)%32)))

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

/* select() — use _5ht_fd_set* to avoid newlib fd_set conflicts */
int _5ht_select(int nfds, _5ht_fd_set *readfds, _5ht_fd_set *writefds,
                _5ht_fd_set *exceptfds, struct timeval *timeout);
#define select(nfds, r, w, e, t) _5ht_select((nfds), (_5ht_fd_set*)(r), (_5ht_fd_set*)(w), (_5ht_fd_set*)(e), (t))

#endif
