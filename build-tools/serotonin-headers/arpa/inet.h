/* arpa/inet.h — Serotonin stub for compilation compatibility */
#ifndef _ARPA_INET_H_
#define _ARPA_INET_H_

#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

in_addr_t inet_addr(const char *cp);
char     *inet_ntoa(struct in_addr in);
int       inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

#ifdef __cplusplus
}
#endif

#endif /* _ARPA_INET_H_ */
