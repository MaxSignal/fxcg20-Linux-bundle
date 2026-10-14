/*
 * flashdump: send the calculator's NOR flash to the computer on the USB
 * cable (DEBUG=1 builds; run from the USB shell).
 *
 *   flashdump [OFFSET [LENGTH]]	(bytes, default: all 32 MiB)
 *
 * Read only. Without an MMU, user space reaches the flash at its physical
 * address 0 (U0, cached). The output is raw: for each 64 KiB block the
 * data, then its CRC-32 (big-endian), so the computer can check every block.
 */
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define FLASH_SIZE	(32u << 20)
#define BLOCK		0x10000u

static unsigned int crc_table[256];

static void crc_init(void)
{
	unsigned int i, j, c;

	for (i = 0; i < 256; i++) {
		for (c = i, j = 0; j < 8; j++)
			c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
		crc_table[i] = c;
	}
}

static unsigned int crc32(const unsigned char *p, unsigned int n)
{
	unsigned int c = 0xffffffffu;

	while (n--)
		c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
	return c ^ 0xffffffffu;
}

static int write_all(const void *buf, unsigned int n)
{
	const char *p = buf;

	while (n) {
		ssize_t k = write(1, p, n);

		if (k <= 0)
			return -1;
		p += k;
		n -= k;
	}
	return 0;
}

int main(int argc, char **argv)
{
	unsigned int off = argc > 1 ? strtoul(argv[1], 0, 0) : 0;
	unsigned int len = argc > 2 ? strtoul(argv[2], 0, 0) : FLASH_SIZE - off;
	struct termios t, saved;
	int raw = 0;

	if (off >= FLASH_SIZE || len > FLASH_SIZE - off)
		return 2;
	if (tcgetattr(1, &saved) == 0) {
		t = saved;
		cfmakeraw(&t);
		raw = tcsetattr(1, TCSANOW, &t) == 0;
	}
	crc_init();
	while (len) {
		const unsigned char *p = (const unsigned char *)(unsigned long)off;
		unsigned int n = len < BLOCK ? len : BLOCK;
		unsigned int c = crc32(p, n);
		unsigned char crc[4] = { c >> 24, c >> 16, c >> 8, c };

		if (write_all(p, n) || write_all(crc, 4))
			break;
		off += n;
		len -= n;
	}
	if (raw)
		tcsetattr(1, TCSADRAIN, &saved);
	return len ? 1 : 0;
}
