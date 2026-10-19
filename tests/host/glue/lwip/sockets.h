#pragma once
/* Host glue stubs (1.2.37): setsockopt() of main/spectrum_http_cache.c goes to glue_setsockopt() — counts TCP_NODELAY on/off, touches no socket. */
#include <sys/types.h>
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
#ifndef TCP_NODELAY
#define TCP_NODELAY 1
#endif
#define setsockopt glue_setsockopt
int glue_setsockopt(int fd, int level, int optname, const void *optval, unsigned optlen);
extern int glue_nodelay_on;       /* setsockopt(IPPROTO_TCP, TCP_NODELAY, !=0) calls */
extern int glue_nodelay_off;      /* setsockopt(IPPROTO_TCP, TCP_NODELAY, ==0) calls: must stay 0 (the flag is never cleared after a send) */
extern int glue_nodelay_other;    /* any other setsockopt call */
extern int glue_setsockopt_fail;  /* != 0: glue_setsockopt() returns -1 (check: a setsockopt failure does not break the send) */
