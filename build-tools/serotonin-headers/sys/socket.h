/* sys/socket.h — Serotonin stub for compilation compatibility */
#ifndef _SYS_SOCKET_H_
#define _SYS_SOCKET_H_

#include <sys/types.h>

typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

#define AF_UNIX   1
#define AF_INET   2
#define AF_INET6  10
#define AF_UNSPEC 0

#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define SOL_SOCKET 1
#define SO_REUSEADDR 2

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

#define MSG_NOSIGNAL 0x4000

#ifdef __cplusplus
extern "C" {
#endif

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int shutdown(int sockfd, int how);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SOCKET_H_ */
