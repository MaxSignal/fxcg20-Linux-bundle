/*
 * usb-shell: a shell for the computer on the USB cable, started only once
 * somebody uses it.
 *
 * A BusyBox shell waiting on /dev/ttyGS0 would hold some 50 KiB of the
 * calculator's RAM all the time, and its prompt would block until a
 * computer opens the port. This program stays a few KiB: it waits for a
 * line (Enter) on the port and only then becomes a login shell there.
 * init runs it as a session leader without a terminal, so opening the
 * port makes it the controlling terminal (job control, ^C).
 */
#include <fcntl.h>
#include <unistd.h>

#define PORT "/dev/ttyGS0"

int main(void)
{
	static const char hello[] = "\r\nLinux on the Casio fx-CG20 (USB shell)\r\n";
	char buf[64];
	int fd;

	fd = open(PORT, O_RDWR);
	if (fd < 0) {
		sleep(60);	/* no USB gadget: init would respawn us at once */
		return 1;
	}
	if (read(fd, buf, sizeof(buf)) <= 0)
		return 1;
	write(fd, hello, sizeof(hello) - 1);
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);
	if (fd > 2)
		close(fd);
	execl("/bin/sh", "-sh", (char *)0);
	return 127;
}
