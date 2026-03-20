#ifndef _5HT_SYS_SOCKET_H
#define _5HT_SYS_SOCKET_H

#include <stdint.h>
#include <stddef.h>

#define AF_UNIX       1
#define SOCK_STREAM   1
#define SOCK_DGRAM    2

#define UNIX_PATH_MAX 108

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

struct sockaddr_un {
    uint16_t sun_family;
    char     sun_path[UNIX_PATH_MAX];
};

struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

typedef uint32_t socklen_t;

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr_un *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr_un *addr, socklen_t *addrlen);
int connect(int sockfd, const struct sockaddr_un *addr, socklen_t addrlen);
int send(int sockfd, const void *buf, size_t len, int flags);
int recv(int sockfd, void *buf, size_t len, int flags);
int shutdown(int sockfd, int how);
int socketpair(int domain, int type, int protocol, int sv[2]);

#endif
