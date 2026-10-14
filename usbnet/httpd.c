/*
 * HTTP/1.0 for files: GET a file or a directory listing, PUT a file
 * (Content-Length required). Paths are under a root directory and may
 * not contain "..".
 *
 *	curl http://192.168.7.1/etc/profile
 *	curl -T notes.txt http://192.168.7.1/tmp/notes.txt
 */
#define _GNU_SOURCE	/* strcasestr */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "lwip/tcp.h"
#include "services.h"

#define HDR_MAX	1024

struct http {
	struct tcp_pcb *pcb;
	char hdr[HDR_MAX];
	size_t hlen;
	enum { H_REQUEST, H_PUT, H_REPLY } st;
	int fd;			/* the file sent (H_REPLY) or written (H_PUT) */
	long left;		/* bytes of the PUT body still to come */
	char *out;		/* a reply built in memory, then the file */
	size_t outlen, outpos;
};

static const char *root = "/";

static void http_free(struct http *h, bool close_tcp)
{
	if (h->pcb) {
		tcp_arg(h->pcb, NULL);
		tcp_recv(h->pcb, NULL);
		tcp_sent(h->pcb, NULL);
		tcp_err(h->pcb, NULL);
		if (close_tcp)
			tcp_close(h->pcb);
	}
	if (h->fd >= 0)
		close(h->fd);
	free(h->out);
	free(h);
}

/* Queue what fits; close once the reply and the file are all queued. */
static void send_more(struct http *h)
{
	char buf[1024];

	while (tcp_sndbuf(h->pcb) > 0 &&
	       tcp_sndqueuelen(h->pcb) < TCP_SND_QUEUELEN - 2) {
		size_t room = tcp_sndbuf(h->pcb);

		if (h->outpos < h->outlen) {
			size_t n = h->outlen - h->outpos;

			if (n > room)
				n = room;
			if (tcp_write(h->pcb, h->out + h->outpos, n,
				      TCP_WRITE_FLAG_COPY) != ERR_OK)
				break;
			h->outpos += n;
		} else if (h->fd >= 0) {
			ssize_t n = read(h->fd, buf, room < sizeof(buf) ? room : sizeof(buf));

			if (n <= 0) {
				close(h->fd);
				h->fd = -1;
				continue;
			}
			if (tcp_write(h->pcb, buf, n, TCP_WRITE_FLAG_COPY) != ERR_OK) {
				lseek(h->fd, -n, SEEK_CUR);
				break;
			}
		} else {
			tcp_output(h->pcb);
			http_free(h, true);	/* lwIP sends the rest, then FIN */
			return;
		}
	}
	tcp_output(h->pcb);
}

static void reply(struct http *h, const char *status, const char *body,
		  long length)
{
	size_t n = 160 + (body ? strlen(body) : 0);

	free(h->out);
	h->out = malloc(n);
	if (!h->out) {
		h->outlen = 0;
		return;
	}
	h->outlen = snprintf(h->out, n,
		"HTTP/1.0 %s\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n%s",
		status, body ? (long)strlen(body) : length, body ? body : "");
	h->outpos = 0;
	h->st = H_REPLY;
}

static void error(struct http *h, const char *status)
{
	char body[64];

	snprintf(body, sizeof(body), "%s\n", status);
	reply(h, status, body, 0);
}

/* "/a%20b" -> root + "/a b"; NULL if it climbs out with ".." */
static char *local_path(const char *url)
{
	size_t rl = strlen(root);
	char *p = malloc(rl + strlen(url) + 2), *d;

	if (!p)
		return NULL;
	memcpy(p, root, rl);
	d = p + rl;
	if (d > p && d[-1] == '/')
		d--;
	if (*url != '/')
		*d++ = '/';
	for (; *url && *url != '?'; url++) {
		if (*url == '%' && url[1] && url[2]) {
			char hex[3] = { url[1], url[2], 0 };

			*d++ = strtol(hex, NULL, 16);
			url += 2;
		} else {
			*d++ = *url;
		}
	}
	*d = 0;
	if (strstr(p + rl, "/../") || !strcmp(d - (d - p >= 3 ? 3 : 0), "/..")) {
		free(p);
		return NULL;
	}
	return p;
}

static void listing(struct http *h, const char *path)
{
	DIR *dir = opendir(path);
	struct dirent *e;
	size_t cap = 512, len = 0;
	char *body;

	if (!dir) {
		error(h, "403 Forbidden");
		return;
	}
	body = malloc(cap);
	while (body && (e = readdir(dir))) {
		size_t n = strlen(e->d_name) + 2;

		if (len + n + 1 > cap) {
			char *b = realloc(body, cap *= 2);

			if (!b) {
				free(body);
				body = NULL;
				break;
			}
			body = b;
		}
		len += sprintf(body + len, "%s%s\n", e->d_name,
			       e->d_type == DT_DIR ? "/" : "");
	}
	closedir(dir);
	if (!body) {
		error(h, "500 Internal Server Error");
		return;
	}
	body[len] = 0;
	reply(h, "200 OK", body, 0);
	free(body);
}

static void put_done(struct http *h)
{
	close(h->fd);
	h->fd = -1;
	error(h, "201 Created");
}

static void put_data(struct http *h, const char *p, size_t n)
{
	if (n > (size_t)h->left)
		n = h->left;
	if (h->fd >= 0 && write(h->fd, p, n) != (ssize_t)n) {
		close(h->fd);
		h->fd = -1;
	}
	h->left -= n;
}

/* The request line and headers are in; act on them. */
static void request(struct http *h, size_t body_start)
{
	char *method = h->hdr, *url, *path, *cl;
	struct stat st;

	h->hdr[h->hlen] = 0;
	url = strchr(method, ' ');
	if (!url) {
		error(h, "400 Bad Request");
		return;
	}
	*url++ = 0;
	url[strcspn(url, " \r\n")] = 0;
	cl = strcasestr(url + strlen(url) + 1, "\ncontent-length:");
	path = local_path(url);
	if (!path) {
		error(h, "403 Forbidden");
		return;
	}
	if (!strcmp(method, "GET")) {
		if (stat(path, &st) < 0)
			error(h, "404 Not Found");
		else if (S_ISDIR(st.st_mode))
			listing(h, path);
		else if ((h->fd = open(path, O_RDONLY | O_CLOEXEC)) < 0)
			error(h, "403 Forbidden");
		else
			reply(h, "200 OK", NULL, st.st_size);
	} else if (!strcmp(method, "PUT")) {
		if (!cl) {
			error(h, "411 Length Required");
		} else if ((h->fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
					 0644)) < 0) {
			error(h, "403 Forbidden");
		} else {
			h->left = strtol(cl + 16, NULL, 10);
			h->st = H_PUT;
			put_data(h, h->hdr + body_start, h->hlen - body_start);
			if (h->left == 0)
				put_done(h);
		}
	} else {
		error(h, "501 Not Implemented");
	}
	free(path);
}

static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	struct http *h = arg;
	struct pbuf *q;

	(void)err;
	if (!p) {
		if (h->st == H_REPLY)
			return ERR_OK;	/* the client is done sending */
		http_free(h, true);
		return ERR_OK;
	}
	tcp_recved(pcb, p->tot_len);
	for (q = p; q; q = q->next) {
		char *d = q->payload;
		size_t n = q->len, i;

		if (h->st == H_REQUEST) {
			char *end;

			i = h->hlen + n < HDR_MAX - 1 ? n : HDR_MAX - 1 - h->hlen;
			memcpy(h->hdr + h->hlen, d, i);
			h->hlen += i;
			h->hdr[h->hlen] = 0;
			end = strstr(h->hdr, "\r\n\r\n");
			if (!end) {
				if (h->hlen == HDR_MAX - 1)
					error(h, "431 Request Header Fields Too Large");
				continue;
			}
			/* bytes of this pbuf after the copied part: body too */
			request(h, end + 4 - h->hdr);
			d += i;
			n -= i;
		}
		if (h->st == H_PUT && n) {
			put_data(h, d, n);
			if (h->left == 0)
				put_done(h);
		}
	}
	pbuf_free(p);
	if (h->st == H_REPLY)
		send_more(h);
	return ERR_OK;
}

static err_t http_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
	(void)pcb; (void)len;
	if (arg)
		send_more(arg);
	return ERR_OK;
}

static void http_err(void *arg, err_t err)
{
	struct http *h = arg;

	(void)err;
	if (h) {
		h->pcb = NULL;	/* already freed by lwIP */
		http_free(h, false);
	}
}

static err_t http_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
	struct http *h;

	(void)arg;
	if (err != ERR_OK || !(h = calloc(1, sizeof(*h)))) {
		tcp_abort(pcb);
		return ERR_ABRT;
	}
	h->pcb = pcb;
	h->fd = -1;
	tcp_arg(pcb, h);
	tcp_recv(pcb, http_recv);
	tcp_sent(pcb, http_sent);
	tcp_err(pcb, http_err);
	return ERR_OK;
}

void httpd_init(const char *dir)
{
	struct tcp_pcb *pcb = tcp_new();

	if (dir)
		root = dir;
	tcp_bind(pcb, IP4_ADDR_ANY, 80);
	pcb = tcp_listen(pcb);
	tcp_accept(pcb, http_accept);
}
