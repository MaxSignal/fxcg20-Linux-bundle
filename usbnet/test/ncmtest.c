/*
 * Test of ncmd, on the build machine or on the calculator. ncmd runs as
 * a child with two pipes instead of /dev/ncm; this program is the
 * computer on the other end of the cable, with its own lwIP: a DHCP
 * server, a DNS server that knows "pc.test", a TCP echo server (port
 * 7). It pings the calculator, and runs its tcp client.
 *
 *	ncmtest NCMD DIR	(DIR: for ncmd's pipes)
 */
#define _GNU_SOURCE	/* pipe2 */
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "lwip/inet_chksum.h"
#include "lwip/ip4.h"
#include "lwip/prot/icmp.h"
#include "lwip/raw.h"
#include "lwip/sys.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "link.h"
#include "ncmtest.h"

static const uint8_t pc_mac[6] = { 0x02, 0xfc, 0x00, 0x00, 0x00, 0x01 };
static int link_fd;
static const char *ncmd, *dir;

/* run lwIP for ms milliseconds, or until done(); extra() when extra_fd is ready */
static bool run(bool (*done)(void), u32_t ms, int extra_fd, void (*extra)(void))
{
	u32_t end = sys_now() + ms;

	while (!done()) {
		struct pollfd p[2] = {
			{ .fd = link_fd, .events = POLLIN },
			{ .fd = extra_fd, .events = POLLIN },
		};

		if ((s32_t)(sys_now() - end) > 0)
			return false;
		poll(p, 2, 20);
		if (p[0].revents & POLLIN)
			link_input();
		if (p[1].revents)
			extra();
		sys_check_timeouts();
	}
	return true;
}

/* ------------------------------------------------- the computer's servers */

/* DNS: an A record for pc.test, "no such name" for anything else */
static void dns_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
		     const ip_addr_t *addr, u16_t port)
{
	static const uint8_t qname[] = "\2pc\4test";
	static const uint8_t answer[12] = {
		0xc0, 12, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4	/* A, 60 s, 4 bytes */
	};
	uint8_t m[512];
	u16_t len = pbuf_copy_partial(p, m, sizeof(m) - 16, 0), q = 12;
	ip4_addr_t pc;
	struct pbuf *r;

	(void)arg;
	pbuf_free(p);
	if (len < 17)
		return;
	while (q < len && m[q])
		q += m[q] + 1;
	q += 5;				/* the 0, QTYPE, QCLASS */
	if (q > len)
		return;
	m[2] = 0x81;			/* a response, recursion desired */
	m[3] = 0x80;			/* recursion available */
	memset(m + 6, 0, 6);
	if (!memcmp(m + 12, qname, sizeof(qname)) && m[q - 3] == 1) {
		m[7] = 1;
		memcpy(m + q, answer, sizeof(answer));
		ip4addr_aton(PC_IP, &pc);
		memcpy(m + q + 12, &pc, 4);
		q += 16;
	} else {
		m[3] |= 3;		/* no such name */
	}
	r = pbuf_alloc(PBUF_TRANSPORT, q, PBUF_RAM);
	if (!r)
		return;
	pbuf_take(r, m, q);
	udp_sendto(pcb, r, addr, port);
	pbuf_free(r);
}

static err_t echo_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	struct pbuf *q;

	(void)arg; (void)err;
	if (!p) {
		tcp_close(pcb);
		return ERR_OK;
	}
	for (q = p; q; q = q->next)
		tcp_write(pcb, q->payload, q->len, TCP_WRITE_FLAG_COPY);
	tcp_output(pcb);
	tcp_recved(pcb, p->tot_len);
	pbuf_free(p);
	return ERR_OK;
}

static err_t echo_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
	(void)arg; (void)err;
	tcp_recv(pcb, echo_recv);
	return ERR_OK;
}

/* pings to the calculator */
static struct raw_pcb *icmp_pcb;
static int replies;

static u8_t icmp_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p,
		      const ip_addr_t *addr)
{
	struct icmp_echo_hdr e;
	u16_t hlen = IPH_HL_BYTES((struct ip_hdr *)p->payload);

	(void)arg; (void)pcb; (void)addr;
	if (pbuf_copy_partial(p, &e, sizeof(e), hlen) != sizeof(e) ||
	    ICMPH_TYPE(&e) != ICMP_ER || e.id != PP_HTONS(0x4e43))
		return 0;
	replies++;
	pbuf_free(p);
	return 1;
}

static void ping_calc(int seq)
{
	struct pbuf *p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr) + 32, PBUF_RAM);
	struct icmp_echo_hdr *e = p->payload;
	ip_addr_t calc;

	memset(e, 0, p->len);
	ICMPH_TYPE_SET(e, ICMP_ECHO);
	e->id = PP_HTONS(0x4e43);
	e->seqno = lwip_htons(seq);
	e->chksum = inet_chksum(e, p->len);
	ipaddr_aton(CALC_IP, &calc);
	raw_sendto(icmp_pcb, p, &calc);
	pbuf_free(p);
}

static void servers_init(void)
{
	struct udp_pcb *dns = udp_new();
	struct tcp_pcb *echo = tcp_new();

	dhcpd_init();
	udp_bind(dns, IP4_ADDR_ANY, 53);
	udp_recv(dns, dns_recv, NULL);
	tcp_bind(echo, IP4_ADDR_ANY, 7);
	echo = tcp_listen(echo);
	tcp_accept(echo, echo_accept);
	icmp_pcb = raw_new(IP_PROTO_ICMP);
	raw_recv(icmp_pcb, icmp_recv, NULL);
}

/* --------------------------------------------- the calculator's programs */

static char status[256];

static bool has_address(void)
{
	char path[256];
	FILE *f;
	size_t n;

	snprintf(path, sizeof(path), "%s/status", dir);
	f = fopen(path, "r");
	if (!f)
		return false;
	n = fread(status, 1, sizeof(status) - 1, f);
	status[n] = 0;
	fclose(f);
	return strstr(status, "address " CALC_IP "\n") != NULL;
}

static int want;

static bool replied(void) { return replies == want; }

/* one client run: its output (and errors) and when it ended */
static struct {
	int out;
	char text[4096];
	size_t len;
	bool done;
} cl;

static void client_output(void)
{
	ssize_t n = 0;

	if (cl.len < sizeof(cl.text) - 1)
		n = read(cl.out, cl.text + cl.len, sizeof(cl.text) - 1 - cl.len);

	if (n <= 0) {
		cl.done = true;
		return;
	}
	cl.len += n;
	cl.text[cl.len] = 0;
}

static bool client_done(void) { return cl.done; }

/* "tcp HOST PORT" with input on its stdin; its exit status */
static int tcp_client(const char *input, const char *host, const char *port)
{
	int in[2], out[2], st = -1;
	pid_t pid;

	memset(&cl, 0, sizeof(cl));
	if (pipe2(in, O_CLOEXEC) < 0 || pipe2(out, O_CLOEXEC) < 0)
		return -1;
	pid = vfork();
	if (pid == 0) {
		dup2(in[0], 0);
		dup2(out[1], 1);
		dup2(out[1], 2);
		execl(ncmd, "tcp", host, port, (char *)NULL);
		_exit(127);
	}
	close(in[0]);
	close(out[1]);
	if (write(in[1], input, strlen(input)) < 0)
		return -1;
	close(in[1]);
	cl.out = out[0];
	run(client_done, 20000, cl.out, client_output);
	close(cl.out);
	waitpid(pid, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int failures;

static void check(bool ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok" : "FAIL", what);
	failures += !ok;
}

int main(int argc, char **argv)
{
	int to[2], from[2];		/* to ncmd, from ncmd */
	char arg[24];
	pid_t pid;
	int st, i;

	if (argc != 3) {
		fprintf(stderr, "usage: ncmtest NCMD DIR\n");
		return 2;
	}
	ncmd = argv[1];
	dir = argv[2];
	setenv("NCM_DIR", dir, 1);
	/* ours close on exec, ncmd's not */
	if (pipe2(to, O_CLOEXEC) < 0 || pipe2(from, O_CLOEXEC) < 0)
		return 1;
	fcntl(to[0], F_SETFD, 0);
	fcntl(from[1], F_SETFD, 0);
	snprintf(arg, sizeof(arg), "%d,%d", to[0], from[1]);
	pid = vfork();
	if (pid == 0) {
		execl(ncmd, "ncmd", "-P", arg, (char *)NULL);
		_exit(127);
	}
	close(to[0]);
	close(from[1]);
	link_fd = from[0];
	link_init(from[0], to[1], pc_mac, PC_IP, NULL);
	servers_init();

	check(run(has_address, 20000, -1, NULL), "DHCP: the calculator got " CALC_IP);
	check(strstr(status, "router " PC_IP "\n") && strstr(status, "dns " PC_IP "\n"),
	      "router and DNS server");

	for (i = 1; i <= 3; i++) {	/* one at a time: the first waits for ARP */
		want = i;
		ping_calc(i);
		run(replied, 2000, -1, NULL);
	}
	check(replies == 3, "the calculator answers pings");

	st = tcp_client("hello-42\n", "pc.test", "7");
	check(st == 0 && !strcmp(cl.text, "hello-42\n"), "tcp: echo through a name");

	st = tcp_client("", "nosuch.test", "7");
	check(st == 1 && strstr(cl.text, "unknown host"), "tcp: an unknown name");

	st = tcp_client("", PC_IP, "9");
	check(st == 1 && strstr(cl.text, "refused"), "tcp: a closed port");

	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
	printf("%s\n", failures ? "FAIL" : "PASS");
	return failures != 0;
}
