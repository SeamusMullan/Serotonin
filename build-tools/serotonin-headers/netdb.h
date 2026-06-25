/* netdb.h — Serotonin stub for compilation compatibility */
#ifndef _NETDB_H_
#define _NETDB_H_

#include <netinet/in.h>

struct hostent {
    char  *h_name;
    char **h_aliases;
    int    h_addrtype;
    int    h_length;
    char **h_addr_list;
};

struct addrinfo {
    int              ai_flags;
    int              ai_family;
    int              ai_socktype;
    int              ai_protocol;
    socklen_t        ai_addrlen;
    struct sockaddr *ai_addr;
    char            *ai_canonname;
    struct addrinfo *ai_next;
};

#define AI_PASSIVE 0x01

#ifdef __cplusplus
extern "C" {
#endif

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int errcode);
struct hostent *gethostbyname(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* _NETDB_H_ */
