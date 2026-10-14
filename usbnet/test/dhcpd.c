/*
 * The computer's DHCP server in the test, as Internet connection sharing
 * does it: the calculator gets CALC_IP, the computer is its router and
 * DNS server.
 */
#include <string.h>
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "link.h"
#include "ncmtest.h"

#define DISCOVER	1
#define OFFER		2
#define REQUEST		3
#define ACK		5

static struct udp_pcb *pcb;

static uint8_t *option(uint8_t *o, uint8_t code, uint8_t len, const void *v)
{
	*o++ = code;
	*o++ = len;
	memcpy(o, v, len);
	return o + len;
}

static void dhcpd_recv(void *arg, struct udp_pcb *upcb, struct pbuf *p,
		       const ip_addr_t *addr, u16_t port)
{
	static const uint8_t cookie[4] = { 99, 130, 83, 99 };
	static const uint8_t lease[4] = { 0, 1, 0x51, 0x80 };	/* a day */
	uint8_t m[548], *o, type = 0;
	ip4_addr_t yi, si, mask;
	struct pbuf *q;
	u16_t len;

	(void)arg; (void)upcb; (void)addr; (void)port;
	len = pbuf_copy_partial(p, m, sizeof(m), 0);
	pbuf_free(p);
	if (len < 240 || m[0] != 1 || memcmp(m + 236, cookie, 4))
		return;
	o = m + 240;
	while (o < m + len && *o != 255) {
		if (*o == 0) {			/* padding */
			o++;
			continue;
		}
		if (o + 3 > m + len)
			break;
		if (*o == 53 && o[1] == 1)
			type = o[2];
		o += 2 + o[1];
	}
	if (type != DISCOVER && type != REQUEST)
		return;

	ip4addr_aton(CALC_IP, &yi);
	ip4addr_aton(PC_IP, &si);
	ip4addr_aton("255.255.255.0", &mask);
	m[0] = 2;			/* BOOTREPLY */
	m[3] = 0;			/* hops */
	memset(m + 12, 0, 4);		/* ciaddr */
	memcpy(m + 16, &yi, 4);		/* yiaddr */
	memcpy(m + 20, &si, 4);		/* siaddr */
	memset(m + 44, 0, 192);		/* sname, file */
	o = m + 240;
	type = type == DISCOVER ? OFFER : ACK;
	o = option(o, 53, 1, &type);
	o = option(o, 54, 4, &si);
	o = option(o, 51, 4, lease);
	o = option(o, 1, 4, &mask);
	o = option(o, 3, 4, &si);		/* router */
	o = option(o, 6, 4, &si);		/* DNS server */
	*o++ = 255;
	len = o - m;
	if (len < 300)			/* the BOOTP minimum */
		memset(o, 0, 300 - len), len = 300;

	q = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
	if (!q)
		return;
	pbuf_take(q, m, len);
	udp_sendto_if(pcb, q, IP4_ADDR_BROADCAST, 68, &link_netif);
	pbuf_free(q);
}

void dhcpd_init(void)
{
	pcb = udp_new();
	udp_bind(pcb, IP4_ADDR_ANY, 67);
	udp_recv(pcb, dhcpd_recv, NULL);
}
