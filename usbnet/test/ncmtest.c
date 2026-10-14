/*
 * Test of ncmd, on the build machine or on the calculator: ncmd runs as
 * a child with two pipes instead of /dev/ncm; this program is the host
 * on the other end, with its own lwIP: DHCP, telnet, HTTP PUT/GET.
 *
 *	ncmtest NCMD HTTP_ROOT
 */
#define _GNU_SOURCE	/* pipe2 */
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "lwip/dhcp.h"
#include "lwip/sys.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "link.h"
#include "services.h"

static const uint8_t host_mac[6] = { 0x02, 0xfc, 0x00, 0x00, 0x00, 0x01 };
static int fd;

/* run lwIP until done() or ms milliseconds */
static bool run(bool (*done)(void), u32_t ms)
{
	u32_t end = sys_now() + ms;

	while (!done()) {
		struct pollfd p = { .fd = fd, .events = POLLIN };

		if ((s32_t)(sys_now() - end) > 0)
			return false;
		poll(&p, 1, 20);
		if (p.revents & POLLIN)
			link_input();
		sys_check_timeouts();
	}
	return true;
}

/* one TCP exchange: send req, collect the reply until the server closes */
static struct {
	struct tcp_pcb *pcb;
	const char *req;
	size_t reqlen, sent;
	char reply[16384];
	size_t len;
	bool closed, failed;
	const char *close_after;	/* send "exit" once this was received */
} x;

static void push(void)
{
	while (x.pcb && x.sent < x.reqlen && tcp_sndbuf(x.pcb)) {
		size_t n = x.reqlen - x.sent;

		if (n > tcp_sndbuf(x.pcb))
			n = tcp_sndbuf(x.pcb);
		if (tcp_write(x.pcb, x.req + x.sent, n, TCP_WRITE_FLAG_COPY) != ERR_OK)
			break;
		x.sent += n;
	}
	if (x.pcb)
		tcp_output(x.pcb);
}

static err_t x_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	(void)arg; (void)err;
	if (!p) {
		x.closed = true;
		tcp_close(pcb);
		x.pcb = NULL;
		return ERR_OK;
	}
	if (x.len + p->tot_len < sizeof(x.reply))
		x.len += pbuf_copy_partial(p, x.reply + x.len, p->tot_len, 0);
	x.reply[x.len] = 0;
	tcp_recved(pcb, p->tot_len);
	pbuf_free(p);
	if (x.close_after && strstr(x.reply, x.close_after)) {
		x.close_after = NULL;
		tcp_write(pcb, "exit\r\n", 6, TCP_WRITE_FLAG_COPY);
		tcp_output(pcb);
	}
	return ERR_OK;
}

static err_t x_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
	(void)arg; (void)pcb; (void)len;
	push();
	return ERR_OK;
}

static void x_err(void *arg, err_t err)
{
	(void)arg; (void)err;
	x.pcb = NULL;
	x.failed = true;
}

static err_t x_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
	(void)arg; (void)pcb; (void)err;
	push();
	return ERR_OK;
}

static bool x_done(void) { return x.closed || x.failed; }

static const char *exchange(u16_t port, const char *req, size_t len,
			    const char *close_after)
{
	ip_addr_t calc;

	memset(&x, 0, sizeof(x));
	x.req = req;
	x.reqlen = len;
	x.close_after = close_after;
	ipaddr_aton(CALC_IP, &calc);
	x.pcb = tcp_new();
	tcp_recv(x.pcb, x_recv);
	tcp_sent(x.pcb, x_sent);
	tcp_err(x.pcb, x_err);
	tcp_connect(x.pcb, &calc, port, x_connected);
	if (!run(x_done, 10000) || x.failed)
		return NULL;
	return x.reply;
}

static bool dhcp_done(void) { return dhcp_supplied_address(&link_netif); }

static int failures;

static void check(bool ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok" : "FAIL", what);
	failures += !ok;
}

int main(int argc, char **argv)
{
	int to[2], from[2];		/* to ncmd, from ncmd */
	pid_t pid;
	char arg[24], *data, *req;
	const char *r;
	size_t n = 5000, i;

	if (argc != 3) {
		fprintf(stderr, "usage: ncmtest NCMD HTTP_ROOT\n");
		return 2;
	}
	/* ours close on exec, ncmd's not */
	if (pipe2(to, O_CLOEXEC) < 0 || pipe2(from, O_CLOEXEC) < 0)
		return 1;
	fcntl(to[0], F_SETFD, 0);
	fcntl(from[1], F_SETFD, 0);
	snprintf(arg, sizeof(arg), "%d,%d", to[0], from[1]);
	pid = vfork();
	if (pid == 0) {
		execl(argv[1], argv[1], "-P", arg, "-r", argv[2], (char *)NULL);
		_exit(127);
	}
	close(to[0]);
	close(from[1]);
	fd = from[0];

	link_init(from[0], to[1], host_mac, NULL, NULL);
	dhcp_start(&link_netif);
	check(run(dhcp_done, 15000), "DHCP");
	check(!strcmp(ip4addr_ntoa(netif_ip4_addr(&link_netif)), HOST_IP),
	      "the host got " HOST_IP);

	r = exchange(23, "echo hello-$((6*7))\r\n", 21, "hello-42");
	check(r && strstr(r, "hello-42"), "telnet: a shell command");

	data = malloc(n);
	req = malloc(n + 100);
	for (i = 0; i < n; i++)
		data[i] = 'a' + i % 26;
	i = sprintf(req, "PUT /ncmtest.txt HTTP/1.0\r\nContent-Length: %zu\r\n\r\n", n);
	memcpy(req + i, data, n);
	r = exchange(80, req, i + n, NULL);
	check(r && strstr(r, " 201 "), "HTTP PUT");

	r = exchange(80, "GET /ncmtest.txt HTTP/1.0\r\n\r\n", 29, NULL);
	r = r ? strstr(r, "\r\n\r\n") : NULL;
	check(r && strlen(r + 4) == n && !memcmp(r + 4, data, n), "HTTP GET");

	r = exchange(80, "GET / HTTP/1.0\r\n\r\n", 18, NULL);
	check(r && strstr(r, "\nncmtest.txt\n"), "HTTP directory listing");

	r = exchange(80, "GET /../etc/passwd HTTP/1.0\r\n\r\n", 32, NULL);
	check(r && strstr(r, " 403 "), "HTTP: no way out of the root");

	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
	printf("%s\n", failures ? "FAIL" : "PASS");
	return failures != 0;
}
