/* lwIP options: IPv4 over one Ethernet interface, no threads */
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
#define LWIP_RAW		NCM_TEST_PC	/* the test pings the calculator */
#define LWIP_UDP		1
#define LWIP_TCP		1
#define LWIP_DHCP		1
#define LWIP_DHCP_DOES_ACD_CHECK 0
/* 169.254.x.x when no DHCP server answers */
#define LWIP_AUTOIP		1
#define LWIP_DHCP_AUTOIP_COOP	1
#define LWIP_DHCP_AUTOIP_COOP_TRIES 3
#define LWIP_DNS		1
#define DNS_TABLE_SIZE		2
#define DNS_MAX_NAME_LENGTH	128
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_IGMP		0
#define IP_REASSEMBLY		0
#define IP_FRAG			0
#define LWIP_STATS		0
#define LWIP_NOASSERT		1

#if NCM_TEST_PC
/* the computer's side in the test has a DHCP server: requests come from 0.0.0.0 */
#define LWIP_IP_ACCEPT_UDP_PORT(port)	((port) == PP_NTOHS(67))
#endif

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
