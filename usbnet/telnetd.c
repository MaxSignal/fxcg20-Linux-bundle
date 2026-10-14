/*
 * telnet: one session at a time, a login shell on a pseudo terminal.
 * The server echoes (the terminal does) and the client sends each key
 * as it is typed; the window size comes from the client (NAWS).
 */
#define _GNU_SOURCE	/* posix_openpt and friends */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "lwip/tcp.h"
#include "services.h"

#define IAC	255
#define DONT	254
#define DO	253
#define WONT	252
#define WILL	251
#define SB	250
#define IP	244	/* interrupt process */
#define SE	240
#define ECHO	1
#define SGA	3	/* suppress go ahead */
#define NAWS	31	/* window size */

static struct {
	struct tcp_pcb *pcb;
	pid_t pid;
	int pty;		/* the terminal's master side */
	enum { T_DATA, T_IAC, T_OPT, T_SB, T_SB_IAC } st;
	uint8_t sb[8];		/* a subnegotiation */
	unsigned int sblen;
	bool cr;
} t = { .pty = -1 };

static void end_session(bool close_tcp)
{
	if (t.pcb) {
		tcp_arg(t.pcb, NULL);
		tcp_recv(t.pcb, NULL);
		tcp_err(t.pcb, NULL);
		if (close_tcp)
			tcp_close(t.pcb);
		t.pcb = NULL;
	}
	if (t.pty >= 0)
		close(t.pty);
	t.pty = -1;
	if (t.pid > 0) {
		kill(-t.pid, SIGHUP);
		waitpid(t.pid, NULL, 0);
		t.pid = 0;
	}
}

static void put(uint8_t c)
{
	if (t.pty >= 0 && write(t.pty, &c, 1) < 0)
		return;			/* a full terminal drops it */
}

static void window_size(void)
{
	struct winsize ws = {
		.ws_col = t.sb[1] << 8 | t.sb[2],
		.ws_row = t.sb[3] << 8 | t.sb[4],
	};

	if (t.sblen >= 5 && t.sb[0] == NAWS)
		ioctl(t.pty, TIOCSWINSZ, &ws);
}

static void from_client(uint8_t c)
{
	switch (t.st) {
	case T_DATA:
		if (c == IAC) {
			t.st = T_IAC;
		} else {
			/* CR LF and CR NUL are the Enter key */
			if (!(t.cr && (c == '\n' || c == 0)))
				put(c);
			t.cr = c == '\r';
		}
		break;
	case T_IAC:
		t.st = T_DATA;
		if (c == IAC) {
			put(c);
		} else if (c == IP) {
			put(3);			/* the terminal's ^C */
		} else if (c == SB) {
			t.st = T_SB;
			t.sblen = 0;
		} else if (c >= WILL) {
			t.st = T_OPT;
		}
		break;
	case T_OPT:		/* the replies to our requests: nothing to do */
		t.st = T_DATA;
		break;
	case T_SB:
		if (c == IAC)
			t.st = T_SB_IAC;
		else if (t.sblen < sizeof(t.sb))
			t.sb[t.sblen++] = c;
		break;
	case T_SB_IAC:
		if (c == SE) {
			window_size();
			t.st = T_DATA;
		} else {
			if (t.sblen < sizeof(t.sb))
				t.sb[t.sblen++] = c;	/* a doubled IAC */
			t.st = T_SB;
		}
		break;
	}
}

static err_t telnet_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	struct pbuf *q;
	u16_t i;

	(void)arg; (void)err;
	if (!p) {
		end_session(true);
		return ERR_OK;
	}
	for (q = p; q; q = q->next)
		for (i = 0; i < q->len; i++)
			from_client(((uint8_t *)q->payload)[i]);
	tcp_recved(pcb, p->tot_len);
	pbuf_free(p);
	return ERR_OK;
}

static void telnet_err(void *arg, err_t err)
{
	(void)arg; (void)err;
	t.pcb = NULL;		/* already freed by lwIP */
	end_session(false);
}

static err_t telnet_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
	static const uint8_t hello[] = {
		IAC, WILL, ECHO, IAC, WILL, SGA, IAC, DO, SGA, IAC, DO, NAWS,
	};
	char *slave;

	(void)arg;
	if (err != ERR_OK || t.pcb)
		goto fail;
	t.pty = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (t.pty < 0 || grantpt(t.pty) < 0 || unlockpt(t.pty) < 0 ||
	    !(slave = ptsname(t.pty)))
		goto fail_pty;
	t.pid = vfork();
	if (t.pid == 0) {
		int fd;

		setsid();
		fd = open(slave, O_RDWR);	/* the controlling terminal */
		if (fd < 0)
			_exit(127);
		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		execl("/bin/sh", "-sh", (char *)NULL);
		_exit(127);
	}
	if (t.pid < 0) {
		t.pid = 0;
		goto fail_pty;
	}
	fcntl(t.pty, F_SETFL, O_NONBLOCK);
	t.pcb = pcb;
	t.st = T_DATA;
	t.cr = false;
	tcp_recv(pcb, telnet_recv);
	tcp_err(pcb, telnet_err);
	tcp_write(pcb, hello, sizeof(hello), TCP_WRITE_FLAG_COPY);
	tcp_output(pcb);
	return ERR_OK;
fail_pty:
	end_session(false);
fail:
	tcp_abort(pcb);
	return ERR_ABRT;
}

int telnetd_pollfd(struct pollfd *pfd)
{
	/* room for a full read, every IAC doubled */
	if (t.pty < 0 || !t.pcb || tcp_sndbuf(t.pcb) < 1024 ||
	    tcp_sndqueuelen(t.pcb) > TCP_SND_QUEUELEN - 4)
		return 0;
	pfd->fd = t.pty;
	pfd->events = POLLIN;
	return 1;
}

void telnetd_poll(const struct pollfd *pfd)
{
	uint8_t in[512], out[1024];
	ssize_t n, i, k = 0;

	if (!(pfd->revents & (POLLIN | POLLHUP | POLLERR)))
		return;
	n = read(t.pty, in, sizeof(in));
	if (n <= 0) {
		if (n < 0 && errno == EAGAIN)
			return;
		end_session(true);	/* the shell is gone (EIO) */
		return;
	}
	for (i = 0; i < n; i++) {
		if (in[i] == IAC)
			out[k++] = IAC;
		out[k++] = in[i];
	}
	tcp_write(t.pcb, out, k, TCP_WRITE_FLAG_COPY);
	tcp_output(t.pcb);
}

void telnetd_init(void)
{
	struct tcp_pcb *pcb = tcp_new();

	tcp_bind(pcb, IP4_ADDR_ANY, 23);
	pcb = tcp_listen(pcb);
	tcp_accept(pcb, telnet_accept);
}
