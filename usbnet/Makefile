# ncmd for the calculator:
#	make LWIP=DIR CC=sh4eb-linux-muslfdpic-gcc O=OUTDIR
# the test on the build machine (ncmd and ncmtest with its gcc):
#	make test LWIP=DIR O=OUTDIR
# ncmtest for the calculator: make LWIP=DIR CC=... O=OUTDIR OUTDIR/ncmtest

LWIP ?= lwip
O ?= .
CC ?= cc
CFLAGS ?= -Os -Wall -ffunction-sections -fdata-sections
LDFLAGS ?= -Wl,--gc-sections

LWIP_SRC = core/init.c core/def.c core/dns.c core/inet_chksum.c core/ip.c \
	core/mem.c core/memp.c core/netif.c core/pbuf.c core/raw.c core/tcp.c \
	core/tcp_in.c core/tcp_out.c core/timeouts.c core/udp.c \
	core/ipv4/acd.c core/ipv4/autoip.c core/ipv4/dhcp.c core/ipv4/etharp.c \
	core/ipv4/icmp.c core/ipv4/ip4.c core/ipv4/ip4_addr.c netif/ethernet.c
SRC = link.c ntb.c $(addprefix $(LWIP)/src/,$(LWIP_SRC))
INC = -I. -I$(LWIP)/src/include

$(O)/ncmd: ncmd.c $(SRC) *.h
	$(CC) $(CFLAGS) $(INC) -DNCM_TEST_PC=0 -o $@ ncmd.c $(SRC) $(LDFLAGS)

$(O)/ncmtest: test/ncmtest.c test/dhcpd.c test/ncmtest.h $(SRC) *.h
	$(CC) $(CFLAGS) $(INC) -Itest -DNCM_TEST_PC=1 -o $@ test/ncmtest.c \
		test/dhcpd.c $(SRC) $(LDFLAGS)

test: $(O)/ncmd $(O)/ncmtest
	rm -rf $(O)/net && mkdir -p $(O)/net
	$(O)/ncmtest $(O)/ncmd $(O)/net

.PHONY: test
