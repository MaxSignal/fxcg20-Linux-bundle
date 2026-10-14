/*
 * ncmd: the network over USB, in user space
 *
 * /dev/ncm (the kernel's ncmdev gadget) gives the CDC NCM transfer
 * blocks the host sends and takes the ones for it; lwIP is the network
 * stack. The calculator is 192.168.7.1 and its DHCP server gives the
 * host 192.168.7.2. Services: telnet (a shell), HTTP (files).
 *
 *	ncmd [-f DEVICE | -P RFD,WFD] [-r HTTP_ROOT]
 *
 * -P: two pipes instead of /dev/ncm, for the test (test/ncmtest.c).
 */
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "lwip/timeouts.h"
#include "link.h"
#include "services.h"

static const uint8_t calc_mac[6] = { 0x02, 0xfc, 0x00, 0x00, 0x00, 0x02 };

int main(int argc, char **argv)
{
	const char *dev = "/dev/ncm", *root = "/";
	int fd = -1, wfd = -1, opt;
	char *end;

	while ((opt = getopt(argc, argv, "f:P:r:")) != -1) {
		switch (opt) {
		case 'f': dev = optarg; break;
		case 'P':
			fd = strtol(optarg, &end, 10);
			wfd = *end == ',' ? strtol(end + 1, NULL, 10) : -1;
			break;
		case 'r': root = optarg; break;
		default:
			fprintf(stderr, "usage: ncmd [-f DEVICE | -P RFD,WFD] [-r HTTP_ROOT]\n");
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

	link_init(fd, wfd, calc_mac, CALC_IP, HOST_IP);
	dhcpd_init();
	telnetd_init();
	httpd_init(root);

	for (;;) {
		struct pollfd pfd[2] = { { .fd = fd, .events = POLLIN } };
		int n = 1 + telnetd_pollfd(&pfd[1]);
		u32_t wait = sys_timeouts_sleeptime();

		poll(pfd, n, wait > 250 ? 250 : wait);
		if (pfd[0].revents & POLLIN)
			link_input();
		if (n > 1)
			telnetd_poll(&pfd[1]);
		sys_check_timeouts();
	}
}
