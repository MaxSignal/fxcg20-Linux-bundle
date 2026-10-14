/* lwIP options: IPv4, TCP and UDP over one Ethernet interface, no threads */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS			1
#define SYS_LIGHTWEIGHT_PROT	0
#define LWIP_SOCKET		0
#define LWIP_NETCONN		0
#define LWIP_IPV6		0
#define LWIP_ARP		1
#define LWIP_ETHERNET		1
#define LWIP_ICMP		1
#define LWIP_UDP		1
#define LWIP_TCP		1
#define LWIP_DNS		0
#define LWIP_IGMP		0
#define IP_REASSEMBLY		0
#define IP_FRAG			0
#define LWIP_DHCP		NCM_TEST_CLIENT	/* the host side, in the test */
/* the DHCP server: requests come from 0.0.0.0 */
#define LWIP_IP_ACCEPT_UDP_PORT(port)	((port) == PP_NTOHS(67))
#define LWIP_STATS		0
#define LWIP_NOASSERT		1

/* everything from malloc: memory only while it is used */
#define MEM_LIBC_MALLOC		1
#define MEMP_MEM_MALLOC		1
#define MEM_ALIGNMENT		4

#define TCP_MSS			1460
#define TCP_WND			(4 * TCP_MSS)
#define TCP_SND_BUF		(4 * TCP_MSS)
#define TCP_SND_QUEUELEN	16
#define MEMP_NUM_TCP_PCB	6
#define PBUF_POOL_BUFSIZE	1536

#endif
