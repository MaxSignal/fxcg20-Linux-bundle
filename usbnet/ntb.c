/* CDC NCM transfer blocks, 16-bit format (NTB16) */
#include "ntb.h"

#define NTH16_SIGN	0x484d434e	/* "NCMH" */
#define NDP16_SIGN	0x304d434e	/* "NCM0" */

static unsigned int get16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }
static void put16(uint8_t *p, unsigned int v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

size_t ntb_wrap(uint8_t *ntb, size_t len, uint16_t seq)
{
	size_t total = NTB_OVERHEAD + len;

	/* NTH16 */
	put32(ntb, NTH16_SIGN);
	put16(ntb + 4, 12);
	put16(ntb + 6, seq);
	put16(ntb + 8, total);
	put16(ntb + 10, 12);		/* the NDP */
	/* NDP16: one datagram, then the (0, 0) end */
	put32(ntb + 12, NDP16_SIGN);
	put16(ntb + 16, 16);
	put16(ntb + 18, 0);		/* no next NDP */
	put16(ntb + 20, NTB_OVERHEAD);
	put16(ntb + 22, len);
	put32(ntb + 24, 0);
	return total;
}

int ntb_unwrap(const uint8_t *ntb, size_t n,
	       void (*fn)(const uint8_t *frame, size_t len, void *arg),
	       void *arg)
{
	size_t block, ndp;
	int hops = 0;

	if (n < 12 || get32(ntb) != NTH16_SIGN || get16(ntb + 4) != 12)
		return -1;
	block = get16(ntb + 8);
	if (block < 12 || block > n)
		block = n;	/* a short packet may end it early */
	ndp = get16(ntb + 10);
	while (ndp) {
		size_t len, e;

		if (ndp % 4 || ndp + 16 > block || ++hops > 8 ||
		    get32(ntb + ndp) != NDP16_SIGN)
			return -1;
		len = get16(ntb + ndp + 4);
		if (len < 16 || ndp + len > block)
			return -1;
		for (e = ndp + 8; e + 4 <= ndp + len; e += 4) {
			size_t off = get16(ntb + e), dlen = get16(ntb + e + 2);

			if (!off || !dlen)
				break;
			if (off + dlen <= block)
				fn(ntb + off, dlen, arg);
		}
		ndp = get16(ntb + ndp + 6);
	}
	return 0;
}
