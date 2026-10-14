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

LWIP_SRC = core/init.c core/def.c core/inet_chksum.c core/ip.c core/mem.c \
	core/memp.c core/netif.c core/pbuf.c core/tcp.c core/tcp_in.c \
	core/tcp_out.c core/timeouts.c core/udp.c core/ipv4/etharp.c \
	core/ipv4/icmp.c core/ipv4/ip4.c core/ipv4/ip4_addr.c netif/ethernet.c
SRC = link.c ntb.c $(addprefix $(LWIP)/src/,$(LWIP_SRC))
INC = -I. -I$(LWIP)/src/include

$(O)/ncmd: ncmd.c dhcpd.c telnetd.c httpd.c $(SRC) *.h
	$(CC) $(CFLAGS) $(INC) -DNCM_TEST_CLIENT=0 -o $@ ncmd.c dhcpd.c \
		telnetd.c httpd.c $(SRC) $(LDFLAGS)

$(O)/ncmtest: test/ncmtest.c $(SRC) *.h
	$(CC) $(CFLAGS) $(INC) -DNCM_TEST_CLIENT=1 -o $@ test/ncmtest.c $(SRC) \
		$(LWIP)/src/core/ipv4/dhcp.c $(LWIP)/src/core/ipv4/acd.c $(LDFLAGS)

test: $(O)/ncmd $(O)/ncmtest
	rm -rf $(O)/www && mkdir -p $(O)/www
	$(O)/ncmtest $(O)/ncmd $(O)/www

.PHONY: test
