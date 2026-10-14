/*
 * ncmd: the calculator's network over USB, in user space
 *
 * /dev/ncm (the kernel's ncmdev gadget) carries the CDC NCM transfer
 * blocks and lwIP is the network stack. The address, the router and the
 * DNS server come from the computer's DHCP server (its Internet
 * connection sharing); without one, a 169.254.x.x address.
 *
 * The kernel has no sockets, so programs ask ncmd through named pipes
 * in $NCM_DIR (/tmp/ncm). One binary, named by how it is called:
 *
 *	ncmd [-f DEVICE | -P RFD,WFD]	the daemon (-P: two pipes, for the test)
 *	tcp HOST PORT			stdin and stdout to a TCP connection
 *
 * A client makes the pipes ID.i (to ncmd) and ID.o (from ncmd), opens
 * ID.o, then writes "tcp HOST PORT ID" to ctl; ncmd answers "ok" or
 * "error: ..." on ID.o, then the data follows. $NCM_DIR/status holds
 * the address, router and DNS server. lwIP answers pings itself.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "link.h"

#define NCONN		4

static const uint8_t calc_mac[6] = { 0x02, 0xfc, 0x00, 0x00, 0x00, 0x02 };
static const char *dir = "/tmp/ncm";

static char *path(const char *name)
{
	static char buf[2][128];
	static int i;

	i ^= 1;
	snprintf(buf[i], sizeof(buf[i]), "%s/%s", dir, name);
	return buf[i];
}

/* ---------------------------------------------------------------- daemon */

static struct conn {
	enum { C_FREE, C_RESOLVE, C_CONNECT, C_OPEN } st;
	int in, out;		/* the client's pipes: from it, to it */
	struct tcp_pcb *pcb;
	u16_t port;
	ip_addr_t addr;
	struct pbuf *pending;	/* received, not yet given to the client */
	bool in_eof, remote_eof;
} conns[NCONN];

static void put(int fd, const char *s)
{
	if (write(fd, s, strlen(s)) < 0)
		return;		/* the client is gone; nothing to tell */
}

static void say(struct conn *c, const char *s)
{
	put(c->out, s);
}

static void conn_free(struct conn *c)
{
	if (c->pcb) {
		tcp_arg(c->pcb, NULL);
		tcp_recv(c->pcb, NULL);
		tcp_sent(c->pcb, NULL);
		tcp_err(c->pcb, NULL);
		if (tcp_close(c->pcb) != ERR_OK)
			tcp_abort(c->pcb);
	}
	if (c->pending)
		pbuf_free(c->pending);
	close(c->in);
	close(c->out);
	memset(c, 0, sizeof(*c));
}

static void fail(struct conn *c, const char *why)
{
	char msg[96];

	snprintf(msg, sizeof(msg), "error: %s\n", why);
	say(c, msg);
	conn_free(c);
}

/* Give the client what came in, as far as its pipe takes it. */
static void flush(struct conn *c)
{
	char buf[1024];

	while (c->pending) {
		u16_t n = pbuf_copy_partial(c->pending, buf, sizeof(buf), 0);
		ssize_t k = write(c->out, buf, n);

		if (k < 0) {
			if (errno != EAGAIN)
				conn_free(c);	/* the client went away */
			return;
		}
		c->pending = pbuf_free_header(c->pending, k);
		if (c->pcb)
			tcp_recved(c->pcb, k);
	}
	if (c->remote_eof)
		conn_free(c);		/* all given: the client sees the end */
}

static err_t tcp_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	struct conn *c = arg;

	(void)pcb; (void)err;
	if (!p)
		c->remote_eof = true;
	else if (c->pending)
		pbuf_cat(c->pending, p);
	else
		c->pending = p;
	flush(c);
	return ERR_OK;
}

static void tcp_err_cb(void *arg, err_t err)
{
	struct conn *c = arg;

	c->pcb = NULL;		/* already freed by lwIP */
	if (c->st == C_CONNECT)
		fail(c, err == ERR_RST ? "connection refused" : "no connection");
	else
		conn_free(c);
}

static err_t tcp_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
	struct conn *c = arg;

	(void)pcb; (void)err;
	c->st = C_OPEN;
	say(c, "ok\n");
	return ERR_OK;
}

static void start(struct conn *c)
{
	c->pcb = tcp_new();
	if (!c->pcb) {
		fail(c, "out of memory");
		return;
	}
	c->st = C_CONNECT;
	tcp_arg(c->pcb, c);
	tcp_recv(c->pcb, tcp_recv_cb);
	tcp_err(c->pcb, tcp_err_cb);
	if (tcp_connect(c->pcb, &c->addr, c->port, tcp_connected_cb) != ERR_OK) {
		tcp_abort(c->pcb);
		c->pcb = NULL;
		fail(c, "no route");
	}
}

static void dns_found_cb(const char *name, const ip_addr_t *addr, void *arg)
{
	struct conn *c = arg;

	(void)name;
	if (c->st != C_RESOLVE)
		return;
	if (!addr) {
		fail(c, "unknown host");
		return;
	}
	c->addr = *addr;
	start(c);
}

/* "tcp HOST PORT ID" */
static void request(char *line)
{
	char *w[4], name[24];
	int n = 0, i;
	struct conn *c = NULL;

	for (char *t = strtok(line, " "); t && n < 4; t = strtok(NULL, " "))
		w[n++] = t;
	if (n != 4 || strcmp(w[0], "tcp"))
		return;
	for (i = 0; i < NCONN; i++)
		if (conns[i].st == C_FREE)
			c = &conns[i];
	snprintf(name, sizeof(name), "%s.o", w[3]);
	if (!c) {	/* tell the client if we can */
		int fd = open(path(name), O_WRONLY | O_NONBLOCK | O_CLOEXEC);

		if (fd >= 0) {
			put(fd, "error: busy\n");
			close(fd);
		}
		return;
	}
	c->out = open(path(name), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (c->out < 0)
		return;		/* the client is gone */
	snprintf(name, sizeof(name), "%s.i", w[3]);
	c->in = open(path(name), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (c->in < 0) {
		close(c->out);
		return;
	}
	c->port = atoi(w[2]);
	c->st = C_RESOLVE;
	if (!netif_ip4_addr(&link_netif)->addr) {
		fail(c, "no address yet");
		return;
	}
	switch (dns_gethostbyname(w[1], &c->addr, dns_found_cb, c)) {
	case ERR_OK:
		start(c);
		break;
	case ERR_INPROGRESS:
		break;
	default:
		fail(c, "unknown host");
	}
}

static void read_requests(int ctl)
{
	static char buf[256];
	static size_t len;
	ssize_t n = read(ctl, buf + len, sizeof(buf) - 1 - len);
	char *nl;

	if (n <= 0)
		return;
	len += n;
	buf[len] = 0;
	while ((nl = strchr(buf, '\n'))) {
		*nl = 0;
		request(buf);
		len -= nl + 1 - buf;
		memmove(buf, nl + 1, len + 1);
	}
	if (len == sizeof(buf) - 1)
		len = 0;	/* a line too long: drop it */
}

/* data from a client for its connection */
static void client_input(struct conn *c)
{
	char buf[1024];
	size_t room = tcp_sndbuf(c->pcb);
	ssize_t n = read(c->in, buf, room < sizeof(buf) ? room : sizeof(buf));

	if (n > 0) {
		tcp_write(c->pcb, buf, n, TCP_WRITE_FLAG_COPY);
		tcp_output(c->pcb);
	} else if (n == 0) {
		c->in_eof = true;
		tcp_shutdown(c->pcb, 0, 1);	/* the end of what we send */
	}
}

static void write_status(struct netif *n)
{
	char *tmp = path("status.new");
	FILE *f = fopen(tmp, "w");

	if (!f)
		return;
	if (netif_ip4_addr(n)->addr) {
		fprintf(f, "address %s\n", ip4addr_ntoa(netif_ip4_addr(n)));
		fprintf(f, "router %s\n", ip4addr_ntoa(netif_ip4_gw(n)));
		fprintf(f, "dns %s\n", ipaddr_ntoa(dns_getserver(0)));
	} else {
		fprintf(f, "no address\n");
	}
	fclose(f);
	rename(tmp, path("status"));
}

static int daemon_main(int argc, char **argv)
{
	const char *dev = "/dev/ncm";
	int fd = -1, wfd = -1, ctl, opt, i;
	char *end;

	while ((opt = getopt(argc, argv, "f:P:")) != -1) {
		switch (opt) {
		case 'f':
			dev = optarg;
			break;
		case 'P':
			fd = strtol(optarg, &end, 10);
			wfd = *end == ',' ? strtol(end + 1, NULL, 10) : -1;
			break;
		default:
			fprintf(stderr, "usage: ncmd [-f DEVICE | -P RFD,WFD]\n");
			return 2;
		}
	}
	if (fd < 0) {
		fd = wfd = open(dev, O_RDWR | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) {
			perror(dev);
			return 1;
		}
	}
	signal(SIGPIPE, SIG_IGN);
	mkdir(dir, 0700);
	unlink(path("ctl"));
	if (mkfifo(path("ctl"), 0600) < 0 ||
	    (ctl = open(path("ctl"), O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0 ||
	    open(path("ctl"), O_WRONLY | O_CLOEXEC) < 0) {	/* never at EOF */
		perror(path("ctl"));
		return 1;
	}

	link_init(fd, wfd, calc_mac, NULL, NULL);
	netif_set_status_callback(&link_netif, write_status);
	write_status(&link_netif);
	dhcp_start(&link_netif);

	for (;;) {
		struct pollfd pfd[2 + 2 * NCONN] = {
			{ .fd = fd, .events = POLLIN },
			{ .fd = ctl, .events = POLLIN },
		};
		struct conn *who[2 + 2 * NCONN];
		int n = 2;
		u32_t wait = sys_timeouts_sleeptime();

		for (i = 0; i < NCONN; i++) {
			struct conn *c = &conns[i];

			if (c->st == C_OPEN && !c->in_eof && tcp_sndbuf(c->pcb) >= 512 &&
			    tcp_sndqueuelen(c->pcb) < TCP_SND_QUEUELEN - 4) {
				who[n] = c;
				pfd[n].fd = c->in;
				pfd[n++].events = POLLIN;
			}
			if (c->st != C_FREE && c->pending) {
				who[n] = c;
				pfd[n].fd = c->out;
				pfd[n++].events = POLLOUT;
			}
		}
		poll(pfd, n, wait > 250 ? 250 : wait);
		if (pfd[0].revents & POLLIN)
			link_input();
		if (pfd[1].revents & POLLIN)
			read_requests(ctl);
		for (i = 2; i < n; i++) {
			if (!pfd[i].revents || who[i]->st == C_FREE)
				continue;
			if (pfd[i].events == POLLIN)
				client_input(who[i]);
			else
				flush(who[i]);
		}
		sys_check_timeouts();
	}
}

/* ---------------------------------------------------------------- client */

static char in_name[24], out_name[24];

static void cleanup(void)
{
	unlink(path(in_name));
	unlink(path(out_name));
}

static int client_main(const char *req)
{
	char line[200], buf[1024];
	int out, in = -1, ctl, n = 0;
	struct pollfd p[2];
	bool stdin_open;

	snprintf(in_name, sizeof(in_name), "%d.i", (int)getpid());
	snprintf(out_name, sizeof(out_name), "%d.o", (int)getpid());
	if (mkfifo(path(in_name), 0600) < 0 || mkfifo(path(out_name), 0600) < 0) {
		perror(dir);
		cleanup();
		return 1;
	}
	atexit(cleanup);
	out = open(path(out_name), O_RDONLY | O_NONBLOCK);
	ctl = open(path("ctl"), O_WRONLY | O_NONBLOCK);
	if (out < 0 || ctl < 0) {
		fprintf(stderr, "ncmd is not running\n");
		return 1;
	}
	snprintf(line, sizeof(line), "%s %d\n", req, (int)getpid());
	if (write(ctl, line, strlen(line)) < 0)
		return 1;
	close(ctl);

	/* the answer: "ok" or "error: ..." */
	p[0].fd = out;
	p[0].events = POLLIN;
	while (n < (int)sizeof(line) - 1) {
		if (poll(p, 1, 30000) <= 0) {
			fprintf(stderr, "ncmd does not answer\n");
			return 1;
		}
		if (read(out, line + n, 1) == 1 && line[n++] == '\n')
			break;
	}
	line[n] = 0;
	if (strcmp(line, "ok\n")) {
		fputs(line, stderr);
		return 1;
	}
	in = open(path(in_name), O_WRONLY);
	cleanup();

	stdin_open = in >= 0;
	for (;;) {
		p[0].fd = out;
		p[0].events = POLLIN;
		p[1].fd = stdin_open ? 0 : -1;
		p[1].events = POLLIN;
		if (poll(p, 2, -1) < 0)
			return 1;
		if (p[0].revents) {
			n = read(out, buf, sizeof(buf));
			if (n == 0 || (n < 0 && errno != EAGAIN))
				return 0;	/* the end */
			if (n > 0 && write(1, buf, n) < 0)
				return 1;
		}
		if (p[1].revents) {
			n = read(0, buf, sizeof(buf));
			if (n <= 0 || write(in, buf, n) < 0) {
				close(in);	/* EOF: the end of what we send */
				stdin_open = false;
			}
		}
	}
}

int main(int argc, char **argv)
{
	const char *name = strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0];
	char req[160];

	if (getenv("NCM_DIR"))
		dir = getenv("NCM_DIR");
	if (!strcmp(name, "tcp")) {
		if (argc != 3) {
			fprintf(stderr, "usage: tcp HOST PORT\n");
			return 2;
		}
		snprintf(req, sizeof(req), "tcp %s %s", argv[1], argv[2]);
		return client_main(req);
	}
	return daemon_main(argc, argv);
}
