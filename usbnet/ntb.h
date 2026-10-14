/* CDC NCM transfer blocks, 16-bit format (NTB16) */
#ifndef NTB_H
#define NTB_H

#include <stddef.h>
#include <stdint.h>

#define NTB_SIZE	2048	/* both ways, as /dev/ncm */
#define NTB_OVERHEAD	28	/* NTH16 + an NDP16 with one datagram */

/*
 * Make ntb an NTB with one datagram of len bytes, an Ethernet frame the
 * caller has put at ntb + NTB_OVERHEAD; the NTB's length.
 */
size_t ntb_wrap(uint8_t *ntb, size_t len, uint16_t seq);

/* Call fn for each datagram in the n bytes of ntb; -1 if malformed. */
int ntb_unwrap(const uint8_t *ntb, size_t n,
	       void (*fn)(const uint8_t *frame, size_t len, void *arg),
	       void *arg);

#endif
