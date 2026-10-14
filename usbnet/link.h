/* lwIP's network interface on NCM transfer blocks */
#ifndef LINK_H
#define LINK_H

#include <stdint.h>
#include "lwip/netif.h"

extern struct netif link_netif;

/*
 * /dev/ncm reads and writes whole NTBs: rfd == wfd, non-blocking. The
 * test uses two pipes instead (rfd != wfd, blocking), where NTBs are
 * found by their length.
 */
void link_init(int rfd, int wfd, const uint8_t mac[6], const char *ip,
	       const char *gw);
void link_input(void);

#endif
