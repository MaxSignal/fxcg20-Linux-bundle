/* The calculator's network services */
#ifndef SERVICES_H
#define SERVICES_H

#include <poll.h>

#define CALC_IP		"192.168.7.1"
#define HOST_IP		"192.168.7.2"

void dhcpd_init(void);
void httpd_init(const char *root);
void telnetd_init(void);
/* the shell's output, while there is room to send it */
int telnetd_pollfd(struct pollfd *pfd);
void telnetd_poll(const struct pollfd *pfd);

#endif
