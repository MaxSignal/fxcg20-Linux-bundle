/* lwIP's network interface on NCM transfer blocks */
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"
#include "link.h"
#include "ntb.h"

struct netif link_netif;
static int link_rfd, link_wfd;
static uint8_t mac_addr[6];
static uint8_t txbuf[NTB_SIZE], rxbuf[NTB_SIZE];
static uint16_t seq;

u32_t sys_now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static err_t link_output(struct netif *n, struct pbuf *p)
{
	uint8_t *frame = txbuf + NTB_OVERHEAD;
	size_t len = p->tot_len;

	if (len > NTB_SIZE - NTB_OVERHEAD)
		return ERR_IF;
	pbuf_copy_partial(p, frame, len, 0);
	if (len < 60) {			/* the shortest Ethernet frame */
		memset(frame + len, 0, 60 - len);
		len = 60;
	}
	len = ntb_wrap(txbuf, len, seq++);
	/* no room (or no host): drop it, TCP sends it again */
	if (write(link_wfd, txbuf, len) < 0)
		return errno == EAGAIN ? ERR_OK : ERR_IF;
	return ERR_OK;
}

static err_t link_netif_init(struct netif *n)
{
	n->name[0] = 'u';
	n->name[1] = 's';
	n->output = etharp_output;
	n->linkoutput = link_output;
	n->mtu = 1500;
	n->hwaddr_len = 6;
	memcpy(n->hwaddr, mac_addr, 6);
	n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
		   NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;
	return ERR_OK;
}

void link_init(int rfd, int wfd, const uint8_t mac[6], const char *ip,
	       const char *gw)
{
	ip4_addr_t a, m, g;

	link_rfd = rfd;
	link_wfd = wfd;
	memcpy(mac_addr, mac, 6);
	lwip_init();
	ip4addr_aton(ip ? ip : "0.0.0.0", &a);
	ip4addr_aton(ip ? "255.255.255.0" : "0.0.0.0", &m);
	ip4addr_aton(gw ? gw : "0.0.0.0", &g);
	netif_add(&link_netif, &a, &m, &g, NULL, link_netif_init, ethernet_input);
	netif_set_default(&link_netif);
	netif_set_up(&link_netif);
}

static void deliver(const uint8_t *frame, size_t len, void *arg)
{
	struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);

	(void)arg;
	if (!p)
		return;
	pbuf_take(p, frame, len);
	if (link_netif.input(p, &link_netif) != ERR_OK)
		pbuf_free(p);
}

static ssize_t read_all(uint8_t *p, size_t n)
{
	size_t got = 0;

	while (got < n) {
		ssize_t k = read(link_rfd, p + got, n - got);

		if (k <= 0)
			return -1;
		got += k;
	}
	return got;
}

void link_input(void)
{
	ssize_t n;

	if (link_rfd == link_wfd) {
		n = read(link_rfd, rxbuf, sizeof(rxbuf));
	} else {			/* a pipe: the NTB's length is at 8 */
		size_t len;

		if (read_all(rxbuf, 12) < 0)
			return;
		len = rxbuf[8] | rxbuf[9] << 8;
		if (len < 12 || len > sizeof(rxbuf) || read_all(rxbuf + 12, len - 12) < 0)
			return;
		n = len;
	}
	if (n > 0)
		ntb_unwrap(rxbuf, n, deliver, NULL);
}
